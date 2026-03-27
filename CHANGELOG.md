# Changelog

All notable changes to this project will be documented in this file.
The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

---

## [Unreleased]

---

## [0.3.2] - 2026-03-27

### Added
- Opendir / Readdir: `BlackholeFS::readdir()` takes a snapshot of direct
  children under the lock. `XrdBlackholeOssDir::Opendir()` stores the
  snapshot; `Readdir()` iterates it, signalling end-of-directory with an
  empty name. Enables `xrdfs ls` and HTTP directory browse.
- `blackhole.seedfile` directive: create one or more pre-seeded stubs with
  full control over path, size (`K`/`M`/`G`/`T` suffix), `count=N` (expands
  a `printf` format specifier in the path), and `type=zeros|random`.
  `type=random` fills reads with a deterministic LCG stream seeded by
  `offset ^ st_ino`, enabling client-side checksum verification.
- MkDocs Material documentation site deployed to GitHub Pages via GitHub
  Actions on push to `main`.

### Fixed
- `XrdOucStream::GetMyFirstWord()` silent-skip bug in unit-test context:
  passing a non-null `XrdOucEnv*` when `XRDINSTANCE` is unset caused all
  non-`set`/`setenv` config directives to be silently discarded. Fixed by
  passing `nullptr` for the env parameter — XrdBlackhole config uses no
  `$VARIABLE` substitution.

### Tests
- 29 new tests: 6 `readdir()` and 6 `seed()` in `test_BlackholeFS`; 12
  `OssDir` integration tests in `test_OssDir`; 5 `cfg_seedfile`
  config-parsing tests in `test_OssFile`.

---

## [0.3.1] - 2026-03-26

### Fixed
- `ofs.osslib` and `ofs.xattrlib` directives now use the correct library
  path (`/usr/lib64/`) and omit the `-5` version suffix (XRootD appends it
  automatically; including it caused a warning and a failed double-versioned
  lookup: `libXrdBlackhole-5-5.so`).
- `http.exthandler` retains the full `libXrdBlackholeMetrics-5.so` filename
  (dlopen-loaded directly, no auto-versioning).

---

## [0.3.0] - 2026-03-26

### Added
- AIO read: `Read(XrdSfsAio*)` implemented; delegates to the sync read path,
  sets `aiop->Result` and calls `aiop->doneRead()` before returning
  `XrdOssOK`.
- Vectored read: `ReadV(XrdOucIOVec*, int)` implemented; iterates segments,
  skips zero-size entries, propagates per-segment errors, accumulates totals.
- Rename: `BlackholeFS::rename()` and `XrdBlackholeOss::Rename()` with POSIX
  atomic-replace semantics (destination overwritten if present). Required for
  GFAL2 atomic upload pattern (`open → write → rename`).
- GoogleTest unit test framework (CMake FetchContent, v1.14.0). Three test
  executables: `test_blackholefs` (22 tests), `test_stats` (15 tests),
  `test_ossfile` (40 tests covering Open/Close/Read/Write/AIO/ReadV/Fstat).
- CI test job in GitHub Actions (Rocky 8 + Rocky 9).

---

## [0.2.0] - 2026-02-01

### Added
- GitHub Actions CI pipeline: build and RPM jobs on Rocky Linux 8 and 9.
- Multi-stage Dockerfile: Stage 1 builds an RPM; Stage 2 installs into a
  clean AlmaLinux 9 runtime (~200 MB image).
- Prometheus metrics endpoint: optional `libXrdBlackholeMetrics-5.so`
  (`XrdHttpExtHandler`) exposes `/metrics` in Prometheus text format.
  Counters: `blackhole_transfers_total`, `blackhole_bytes_written_total`,
  `blackhole_bytes_read_total`, `blackhole_errors_total`,
  `blackhole_write_throughput_MiBs_avg`, `blackhole_read_throughput_MiBs_avg`.
- Per-transfer stats: `XrdBlackholeStatsManager` records duration, byte
  counts, op counts, and error count for every completed transfer.
- Read support: `Read(void*, off_t, size_t)`, `Read(off_t, size_t)`, and
  `ReadRaw` return zero-filled data up to the file's virtual size.
- `StatLS` reports 1 PiB free space so FTS3/GFAL2 space-token checks pass.
- `BHTRACE` macro for per-operation I/O tracing (gated on
  `xrootd.trace all`).

### Fixed
- POSC disabled (`XRDXROOTD_NOPOSC=1`) to prevent spurious unlink on close.
- Read boundary guard: `offset >= m_size` and negative offsets return 0
  (prevents `size_t` underflow before subtraction).

### Changed
- Spec file: removed stale `libXrdBlackholePosix.so*` reference; added
  metrics subpackage with filelist-driven `%files` section;
  `_empty_manifest_terminate_build 0` prevents failure when XrdHttp headers
  are absent.

---

## [0.1.0] - initial

### Added
- Initial blackhole OSS plugin: writes discarded, reads return zeros.
- In-memory virtual filesystem (`BlackholeFS`) with open/close/unlink/stat.
- Optional write-speed throttle (`blackhole.writespeedMiBps`).
- Pre-seeded default files (`blackhole.defaultspath`).
