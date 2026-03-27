# XrdBlackhole — Roadmap

This document tracks the prioritised plan for making XrdBlackhole a
production-quality, community-useful tool. Items are ordered by risk reduction
and user-visible impact. Each phase should be completed and released before
starting the next.

---

## Phase 1 — Foundation (Stability & CI)

*Goal: make it safe to accept contributions and prevent regressions.*

### 1.1 Unit test framework ✅ DONE

GoogleTest via CMake FetchContent. Three test executables: `test_blackholefs`,
`test_stats`, `test_ossfile`. CI runs `ctest --output-on-failure` on Rocky 8
and Rocky 9.

### 1.2 CI pipeline ✅ DONE

GitHub Actions workflow on Rocky Linux 8 and Rocky Linux 9 containers:
- `build` job: cmake configure + compile, uploads `.so` artifacts
- `rpm` job: SRPM via `makesrpm.sh` → `dnf builddep` → `rpmbuild --rebuild`, uploads RPMs
- Matrix: `rockylinux:8` (`powertools` CRB) and `rockylinux:9` (`crb` CRB)
- Triggers on push to `main`/`master`/`test-dev` and all PRs

### 1.3 Dockerfile cleanup ✅ DONE

Two-stage build: Stage 1 compiles and packages an RPM; Stage 2 installs it
into a clean AlmaLinux 9 runtime. Runtime image is ~200 MB.

### 1.4 Packaging fixes ✅ DONE

`packaging/rhel/xrootd-blackhole.spec.in` cleaned up:
- Stale `libXrdBlackholePosix.so*` reference removed from `%files`
- `%package metrics` subpackage added with filelist-driven `%files` so the
  package builds cleanly whether or not XrdHttp headers are present
- `_empty_manifest_terminate_build 0` prevents build failure on empty metrics list

### 1.5 Release versioning ✅ DONE

`docs/ReleaseNotes.txt` populated with v0.1.0, v0.2.0, and v0.3.0 entries.
Spec `%changelog` updated. Version is derived from `git tag vX.Y.Z` via
`genversion.sh`; `VERSION_INFO` is populated by `git archive` export-subst.

---

## Phase 2 — Feature Completeness

*Goal: pass end-to-end tests with the tools the HEP community actually uses.*

### 2.1 AIO read (`Read(XrdSfsAio*)`) ✅ DONE

Delegates to the sync `Read(void*, off_t, size_t)` path, sets `aiop->Result`,
calls `aiop->doneRead()`. Tested with 6 new GoogleTest cases.

### 2.2 Vectored read (`ReadV`) ✅ DONE

Iterates the `XrdOucIOVec` array, calls sync `Read()` per entry, accumulates
total bytes. Skips zero-size entries; propagates per-segment errors. Tested
with 7 new GoogleTest cases.

### 2.3 Rename ✅ DONE

`BlackholeFS::rename()` acquires the lock, returns `-ENOENT` if source is
missing, atomically replaces the destination (POSIX semantics), then
re-inserts the stub under the new name and erases the old entry. Tested
with 6 new GoogleTest cases.

### 2.4 Directory listing (`Opendir` / `Readdir`) ✅ DONE

`BlackholeFS::readdir()` takes a snapshot of direct children under the lock.
`XrdBlackholeOssDir::Opendir()` stores the snapshot; `Readdir()` iterates it,
signalling end-of-directory with an empty buffer. Tested with 6 BlackholeFS
unit tests and 12 OssDir integration tests.

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

### 3.2 Prometheus metrics ✅ DONE

`libXrdBlackholeMetrics-5.so` is an optional `XrdHttpExtHandler` plugin that
exposes a Prometheus text format `/metrics` endpoint. Built when
`XrdHttp/XrdHttpExtHandler.hh` is found at configure time; packaged in the
`xrootd-blackhole-metrics` RPM subpackage. Load with:
```
http.exthandler bhmetrics /usr/lib64/xrootd/libXrdBlackholeMetrics-5.so
```
Metrics exposed: `blackhole_transfers_total`, `blackhole_bytes_written_total`,
`blackhole_bytes_read_total`, `blackhole_errors_total`,
`blackhole_write_throughput_MiBs_avg`, `blackhole_read_throughput_MiBs_avg`.

### 3.3 Configurable pre-seeded file set ✅ DONE

`blackhole.seedfile <path> <size>[K|M|G|T] [count=N] [type=zeros|random]`
creates one or N pre-seeded stubs. `count>1` requires a printf integer format
specifier in the path (e.g. `%04d`). `type=random` fills reads with a
deterministic LCG seeded by `offset ^ st_ino`. `defaultspath` is unchanged.

### 3.4 Read throughput throttle

The read path currently returns zeros at memory speed. Add
`blackhole.readspeedMiBps` (same implementation pattern as write throttle)
to simulate slow storage on reads.

### 3.5 Truncate support

`Truncate` is currently `-ENOTSUP`. Implement by resizing `Stub::m_size` and
`m_stat.st_size` under the `BlackholeFS` lock.

### 3.6 Documentation site ✅ DONE

MkDocs Material site built from `docs/` and deployed to GitHub Pages via
a GitHub Actions workflow on push to `main`. Pages: Home (quick start),
Configuration (full directive reference), Architecture, Roadmap, Release Notes.

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
| 0.2.0 | 1 | ✅ CI (Rocky 8+9), ✅ multi-stage Dockerfile, ✅ packaging fixes, ✅ Prometheus metrics; unit tests + versioning still outstanding |
| 0.3.0 | 1+2 | ✅ Unit tests (1.1), ✅ AIO read (2.1), ✅ ReadV (2.2), ✅ Rename (2.3), ✅ Release versioning (1.5) |
| 0.4.0 | 2+3 | Opendir/Readdir (2.4), Checksum (2.5), StatFS accuracy (2.6), per-path QoS (3.1), seed config (3.3) |
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
