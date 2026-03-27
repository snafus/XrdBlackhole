# Release Notes

---

## v0.4.0

New features:

- **Opendir / Readdir** — `BlackholeFS::readdir()` takes a snapshot of direct
  children under the lock. `XrdBlackholeOssDir::Opendir()` stores the snapshot;
  `Readdir()` iterates it, signalling end-of-directory with an empty name.
  Enables `xrdfs ls` and HTTP directory browse.
- **`blackhole.seedfile` directive** — create one or more pre-seeded stubs
  with full control over path, size (`K`/`M`/`G`/`T` suffix), count
  (`count=N` with printf format in path), and content type
  (`type=zeros|random`). `type=random` fills reads with a deterministic LCG
  stream seeded by `offset ^ st_ino`, enabling client-side checksum
  verification.

Testing:

- 18 new unit tests: 6 `rename()` tests, 6 `readdir()` tests, 6 `seed()`
  tests in `test_BlackholeFS`; 12 `OssDir` integration tests in `test_OssDir`;
  5 `cfg_seedfile` config-parsing tests in `test_OssFile`.
- Fixed `XrdOucStream::GetMyFirstWord()` silent-skip bug in test context:
  passing a non-null `XrdOucEnv*` with a null `XRDINSTANCE` caused all
  non-`set`/`setenv` config directives to be skipped silently. Fixed by
  passing `nullptr` for the env parameter (XrdBlackhole config uses no
  `$VARIABLE` substitution).

---

## v0.3.1  (2026-03-26)

Bug fixes:

- `ofs.osslib` and `ofs.xattrlib` directives now use the correct library path
  (`/usr/lib64/`) and omit the `-5` version suffix (XRootD appends it
  automatically; including it caused a warning and a failed double-versioned
  lookup: `libXrdBlackhole-5-5.so`).
- `http.exthandler` retains the full `libXrdBlackholeMetrics-5.so` filename
  (dlopen-loaded directly, no auto-versioning).

---

## v0.3.0  (2026-03-26)

New features:

- **AIO read** — `Read(XrdSfsAio*)` implemented; delegates to the sync read
  path, sets `aiop->Result` and calls `aiop->doneRead()` before returning
  `XrdOssOK`.
- **Vectored read** — `ReadV(XrdOucIOVec*, int)` implemented; iterates
  segments, skips zero-size entries, propagates per-segment errors,
  accumulates totals.
- **Rename** — `BlackholeFS::rename()` and `XrdBlackholeOss::Rename()`
  implemented with POSIX atomic-replace semantics (destination overwritten if
  present). Required for GFAL2 atomic upload pattern (`open → write → rename`).

Testing:

- GoogleTest unit test framework added (CMake FetchContent, v1.14.0).
- Three test executables: `test_blackholefs` (22 tests), `test_stats`
  (15 tests), `test_ossfile` (40 tests covering Open/Close/Read/Write/AIO/
  ReadV/Fstat).
- CI test job added to GitHub Actions (Rocky 8 + Rocky 9).

---

## v0.2.0  (2026-02-01)

CI / packaging:

- GitHub Actions CI pipeline: build and RPM jobs on Rocky Linux 8 and 9.
- Multi-stage Dockerfile: Stage 1 builds an RPM; Stage 2 installs into a
  clean AlmaLinux 9 runtime (~200 MB image).
- Spec file cleanup: removed stale `libXrdBlackholePosix.so*` reference;
  added metrics subpackage with filelist-driven `%files` section;
  `_empty_manifest_terminate_build 0` prevents failure on absent XrdHttp.

New features:

- **Prometheus metrics endpoint** — optional `libXrdBlackholeMetrics-5.so`
  (`XrdHttpExtHandler`) exposes `/metrics` in Prometheus text format.
- **Per-transfer stats** — `XrdBlackholeStatsManager` records duration,
  byte counts, op counts, and error count for every completed transfer.
- **Read support** — `Read(void*, off_t, size_t)`, `Read(off_t, size_t)`,
  and `ReadRaw` return zero-filled data up to the file's virtual size.
- **StatLS** reports 1 PiB free space so FTS3/GFAL2 space-token checks pass.
- **`BHTRACE` macro** for per-operation I/O tracing (gated on
  `xrootd.trace all`).

Bug fixes:

- POSC disabled (`XRDXROOTD_NOPOSC=1`) to prevent spurious unlink on close.
- Read boundary guard: `offset >= m_size` and negative offsets return 0
  (guards `size_t` underflow before subtraction).

---

## v0.1.0  (initial)

- Initial blackhole OSS plugin: writes discarded, reads return zeros.
- In-memory virtual filesystem (`BlackholeFS`) with open/close/unlink/stat.
- Optional write-speed throttle (`blackhole.writespeedMiBps`).
- Pre-seeded default files (`blackhole.defaultspath`).
