# XrdBlackhole — Roadmap

This document tracks the prioritised plan for making XrdBlackhole a
production-quality, community-useful tool. Items are ordered by risk reduction
and user-visible impact. Each phase should be completed and released before
starting the next.

---

## Phase 1 — Foundation (Stability & CI)

*Goal: make it safe to accept contributions and prevent regressions.*

### 1.1 Unit test framework

No tests exist. This is the single highest-risk gap.

**What to build:**
- CMake test target (`BUILD_TESTS` flag already wired in `CMakeLists.txt`)
- Test harness that instantiates `BlackholeFS` and `XrdBlackholeOssFile`
  without a live XRootD process (mock `XrdSysError`, `XrdOucEnv`)
- Tests covering:
  - `open` / `close` / `unlink` lifecycle
  - Concurrent open + unlink (shared_ptr lifetime)
  - `Read()` at boundary offsets (0, size-1, size, size+1, negative)
  - Write throttle sleep duration at sub-MiB and multi-MiB sizes
  - `create_defaults` path generation
  - `TransferStats` values recorded at `Close()`
  - `StatManager` aggregate accumulation and thread safety

**Framework recommendation:** GoogleTest (already a common choice in XRootD
ecosystem; lightweight; good CMake integration).

### 1.2 CI pipeline

**What to build:**
- GitHub Actions workflow: build on AlmaLinux 9 (matching the Dockerfile),
  run unit tests, fail the PR if tests fail
- Separate workflow for RPM build smoke-test (`packaging/makesrpm.sh`)
- Optional: build matrix for XRootD 5.x minor versions

### 1.3 Dockerfile cleanup

The current `Dockerfile` has a single-stage build with commented-out
multi-stage instructions. Finish the multi-stage split so the runtime image
only contains XRootD + the plugin `.so`, not the full build toolchain.
This reduces the image from ~1.5 GB to ~200 MB.

### 1.4 Packaging fixes

`packaging/rhel/xrootd-blackhole.spec.in` references
`libXrdBlackholePosix.so*` which no longer exists. Remove it from `%files`.
Update the `%description` typo ("perfmance").

### 1.5 Release versioning

Adopt a formal `MAJOR.MINOR.PATCH` version in `VERSION_INFO` and tag releases.
Populate `docs/ReleaseNotes.txt` with a `0.2.0` entry covering all fixes made
to date.

---

## Phase 2 — Feature Completeness

*Goal: pass end-to-end tests with the tools the HEP community actually uses.*

### 2.1 AIO read (`Read(XrdSfsAio*)`)

Currently returns `-ENOTSUP`. XRootD's scheduler uses AIO for parallel chunk
reads — without it, large file reads fall back to sequential single-buffer
calls.

**Implementation:**
```cpp
int XrdBlackholeOssFile::Read(XrdSfsAio *aiop) {
  size_t blen   = aiop->sfsAio.aio_nbytes;
  off_t  offset = aiop->sfsAio.aio_offset;
  void  *buff   = (void*)aiop->sfsAio.aio_buf;
  aiop->Result  = Read(buff, offset, blen);  // reuse sync path
  aiop->doneRead();
  return XrdOssOK;
}
```

### 2.2 Vectored read (`ReadV`)

Used by GFAL2 and XRootD's `xrdcopy --parallel` to pipeline chunk requests.

**Implementation:** iterate the `XrdOucIOVec` array, calling the sync
`Read(buff, offset, blen)` for each entry, accumulate total bytes.

### 2.3 Rename

Required for GFAL2's atomic upload pattern (`open → write → rename`).

**Implementation:** within `BlackholeFS`, acquire the lock, look up `from`,
insert under `to`, erase `from`. Return `-ENOENT` if `from` not found.

### 2.4 Directory listing (`Opendir` / `Readdir`)

Required for `xrdfs ls`, HTTP browse, and any client that stat-walks a tree.

**Implementation:**
- `Opendir(path)`: record path prefix in `XrdBlackholeOssDir`.
- `Readdir(buff, blen)`: iterate `BlackholeFS::m_files` returning entries
  whose path starts with the stored prefix and has no further `/` separators
  (i.e. direct children only).
- `BlackholeFS` needs a new `readdir(prefix, cookie)` method that holds the
  lock during the scan.

### 2.5 Checksum responses

XRootD clients and FTS3 request checksums via `QueryChksum`. The `Stub`
already has `m_checksums map<string,string>` for this purpose.

**Implementation:**
- On write close: compute Adler32 over the (virtual) byte count, or derive a
  deterministic value (e.g. Adler32 of `blen` zero bytes up to the file size).
- Expose via `XrdBlackholeXAttr` or a `QueryChksum` handler in the OFS layer.
- Config directive: `blackhole.checksum <algo>` (default: `adler32`).

### 2.6 StatFS / StatLS accuracy

`StatFS` currently reports `sP.Free` from a zero-initialised `XrdOssVSInfo`.
`StatLS` reports zero space. Configure total/free/used space via directives:

```
blackhole.totalspace  10T    # reported total
blackhole.usedspace   0      # reported used (default 0)
```

This allows monitoring tools and FTS3 space tokens to work correctly.

---

## Phase 3 — Production Quality

*Goal: deployable in real HEP infrastructure; observable; configurable.*

### 3.1 Per-path QoS throttle

The global `blackhole.writespeedMiBps` applies to every transfer. Add
glob-based rules:

```
blackhole.qos /atlas/*   writeMiBps=500
blackhole.qos /cms/*     writeMiBps=200  readMiBps=800
blackhole.qos *          writeMiBps=1024
```

Rules are matched in declaration order; first match wins.

**Implementation:** `XrdBlackholeOss` parses rules into a
`vector<{glob, write_speed, read_speed}>`. `XrdBlackholeOssFile::Open()`
walks the list and stores the matched speeds for the lifetime of the handle.

### 3.2 Prometheus metrics

XRootD 5.x ships with a built-in HTTP server. Expose a `/metrics` endpoint
returning Prometheus text format:

```
# HELP blackhole_bytes_written_total Bytes accepted for writing
blackhole_bytes_written_total 42949672960
# HELP blackhole_write_throughput_MiBs_avg Rolling average write throughput
blackhole_write_throughput_MiBs_avg 987.2
blackhole_transfers_total{op="write"} 1024
blackhole_transfers_total{op="read"} 256
blackhole_errors_total 0
```

`XrdBlackholeStatsManager` already accumulates the necessary aggregates;
expose them via an `XrdHttpExtHandler` (or a lightweight `/metrics` path in
`XrdBlackholeXAttr`'s HTTP handler).

### 3.3 Configurable pre-seeded file set

Replace the hard-coded `testfile_zeros_{1MiB,1GiB,10GiB}` triple with a
flexible directive:

```
blackhole.seedfile /test/4GiB   4294967296
blackhole.seedfile /test/100GiB 107374182400
```

### 3.4 Read throughput throttle

The read path currently returns zeros at memory speed. Add
`blackhole.readspeedMiBps` (same implementation pattern as write throttle)
to simulate slow storage on reads.

### 3.5 Truncate support

`Truncate` is currently `-ENOTSUP`. Implement by resizing `Stub::m_size` and
`m_stat.st_size` under the `BlackholeFS` lock.

### 3.6 Documentation site

Publish the `README.md` and `docs/` tree as a GitHub Pages site using
MkDocs or Sphinx. Target audience: storage engineers at WLCG sites who want
to run benchmarks without provisioning real storage.

---

## Phase 4 — Advanced / Community

*Goal: address the long tail of use cases that appear once the tool is adopted.*

### 4.1 Persistent stub registry

Add `blackhole.stubfile /var/lib/xrootd/blackhole-ns.json`. On startup, load
the stub map; on shutdown (or periodically), persist it. This lets an operator
pre-seed a realistic namespace (millions of files with realistic sizes, mtimes,
checksums) that survives server restarts.

Format: newline-delimited JSON, one record per file:
```json
{"path":"/atlas/data/run3/file001.root","size":2147483648,"mtime":1700000000}
```

### 4.2 Multi-instance namespace sharing

For sites running multiple XRootD nodes behind a redirector, provide an option
to share the in-memory namespace over a lightweight gRPC or Redis backend so
all nodes see the same file set (important for read-after-write consistency in
benchmarks).

### 4.3 Error injection

Add a `blackhole.errorrate <fraction>` directive that randomly returns
`-EIO` from `Write()` / `Read()` with the given probability. Useful for
testing client retry and error-handling logic.

```
blackhole.errorrate 0.001   # 0.1% of operations return EIO
```

### 4.4 WLCG integration test suite

A separate repository containing integration tests against a live blackhole
server using:
- `gfal-copy` / `gfal-ls` (GFAL2)
- `xrdcp` with parallel streams
- `xrdfs ls` / `xrdfs stat`
- FTS3 test transfers
- `davix` WebDAV/HTTP

These tests can run in CI against a Docker Compose stack
(`xrootd-blackhole` + test client).

### 4.5 XRootD 4.x compatibility

The plugin currently targets XRootD 5.x. XRootD 4.x is still in production at
some sites. Assess the API differences and, if feasible, add a compatibility
layer or separate build path.

---

## Version Plan

| Version | Phase | Key deliverables |
|---|---|---|
| 0.2.0 | 1 | Tests, CI, Dockerfile fix, packaging fix, versioning |
| 0.3.0 | 2 | AIO read, ReadV, Rename, Opendir/Readdir, Checksum |
| 0.4.0 | 2+3 | StatFS accuracy, per-path QoS, Prometheus, seed config |
| 1.0.0 | 3 | Full Phase 3 complete; stable API; docs site |
| 1.x | 4 | Persistent namespace, error injection, integration tests |

---

## Contributing

Issues and PRs are welcome. For significant changes, open an issue first to
discuss the approach. All PRs must:

1. Include or update unit tests for changed behaviour
2. Pass CI (build + test)
3. Follow the commit convention (`fix(oss):`, `feat(oss):`, etc.)
4. Update `docs/ReleaseNotes.txt`
