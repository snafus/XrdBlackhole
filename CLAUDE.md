# CLAUDE.md — XrdBlackhole Development Guide

XrdBlackhole is an XRootD OSS plugin that provides a high-throughput blackhole
storage backend: writes are silently discarded, reads synthesise zero-filled
data. Primary use: storage performance benchmarking and protocol testing.

---

## Build

```bash
mkdir build && cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
```

XRootD headers and runtime must be available. The Dockerfile builds a
self-contained environment on AlmaLinux 9 using XRootD from the CERN repo.

```bash
docker build -t xrd-blackhole .
docker run --rm -p 1094:1094 xrd-blackhole \
  /usr/bin/xrootd -f -c /etc/xrootd/xrootd-blackhole.cfg -l /dev/stderr
```

Output libraries (loaded via `ofs.osslib` / `ofs.xattrlib`):
- `libXrdBlackhole-5.so` — main OSS plugin
- `libXrdBlackholeXattr-5.so` — xattr stub (all -ENOTSUP)

---

## Key Files

| File | Role |
|---|---|
| `src/XrdBlackhole/BlackholeFS.hh/.cc` | In-memory virtual filesystem (`std::map<string, shared_ptr<Stub>>`) |
| `src/XrdBlackhole/XrdBlackholeOss.hh/.cc` | XrdOss implementation; config parsing; entry point |
| `src/XrdBlackhole/XrdBlackholeOssFile.hh/.cc` | XrdOssDF file handle; read/write/stats lifecycle |
| `src/XrdBlackhole/XrdBlackholeOssDir.hh/.cc` | XrdOssDF dir handle (stub, all -ENOTSUP) |
| `src/XrdBlackhole/XrdBlackholeXAttr.hh/.cc` | XrdOucXAttr stub (all -ENOTSUP) |
| `src/XrdBlackhole/XrdBlackholeStats.hh/.cc` | Per-transfer stats + global aggregates |
| `src/XrdBlackhole.cmake` | Library targets and install rules |
| `config/xrootd-blackhole.cfg` | Reference server config |
| `docker/Dockerfile` | AlmaLinux 9 build + run image |

---

## Architecture Summary

```
xrootd process
└── XrdOfs (OFS layer)
    └── XrdBlackholeOss           ← loaded via ofs.osslib
        ├── BlackholeFS            ← thread-safe in-memory namespace
        │   └── map<path, shared_ptr<Stub>>
        ├── XrdBlackholeOssFile   ← one per open file; holds shared_ptr<Stub>
        └── XrdBlackholeStatsManager  ← global singleton; mutex-protected
```

See `docs/ARCHITECTURE.md` for the full read/write data-flow and thread-safety
model.

---

## Code Conventions

### Logging
- **Always-visible messages**: `XrdBlackholeEroute.Emsg(...)` for errors,
  `XrdBlackholeEroute.Say(...)` for important lifecycle events.
- **Trace-gated I/O logging**: `BHTRACE("key=" << val)` — gated on
  `XrdBlackholeTrace.What & BHTRACE_IO`. Enable with `xrootd.trace all`.
- Never use `std::cout`, `std::cerr`, or `std::clog` — XRootD manages all
  log routing through `XrdSysError`.

### Error codes
- Return `-errno` values (e.g. `-ENOENT`, `-EINVAL`) on failure.
- Return `XrdOssOK` (0) on success for `int`-returning methods.
- Return byte count on success for `ssize_t`-returning methods.

### Thread safety
- `BlackholeFS` serialises all map mutations via `m_mutexFD`.
- `XrdBlackholeOssFile` uses `std::atomic` for all per-operation counters
  (`m_writeBytes`, `m_readBytes`, `m_writeOps`, …).
- `XrdBlackholeStatsManager` uses its own mutex for aggregate updates.
- Each `XrdBlackholeOssFile` holds a `shared_ptr<Stub>` from `Open()` to
  `Close()`, preventing use-after-free if the file is unlinked while open.

### Ownership of Stub*
- `BlackholeFS` owns stubs via `shared_ptr<Stub>` in `m_files`.
- File handles capture their stub as `shared_ptr<Stub> m_stub` in `Open()`.
- Never call `delete` on a `Stub`; let `shared_ptr` manage lifetime.

### Config parsing (`XrdBlackholeOss::Configure`)
- Use `if / else if / else` chains — never `if / if`.
- Use `strcmp`, not `strncmp`, to match directive names.
- Catch all `blackhole.*` unknowns with the trailing `else if (!strncmp...)`.
- Log a startup summary of effective config via `Eroute.Say(...)`.

### Commit style
Conventional commits: `fix(oss):`, `feat(oss):`, `docs:`, `refactor(oss):`,
`test:`, `ci:`. One logical change per commit.

---

## Invariants to Preserve

- **No disk I/O** — never open, stat, or write a real filesystem path.
- **POSC disabled** — `XRDXROOTD_NOPOSC=1` must remain set in `Configure()`.
- **Writes always succeed** — `Write()` / `Write(XrdSfsAio*)` must return the
  full byte count (after optional throttle sleep); never return an error for
  valid write calls.
- **Read bounds** — `Read(void*, off_t, size_t)` must guard against
  `offset >= m_size` and negative offsets before the `size_t` subtraction.
- **AIO completions** — `Write(XrdSfsAio*)` must call `aiop->doneWrite()`;
  if AIO reads are ever implemented, call `aiop->doneRead()`.

---

## Supported / Unsupported Operations

| Operation | Status | Notes |
|---|---|---|
| Open (write) | Supported | Creates in-memory Stub |
| Open (read) | Supported | Requires prior write or `defaultspath` |
| Write (sync) | Supported | Optional throttle; bytes discarded |
| Write (AIO) | Supported | Calls doneWrite() |
| Read (sync) | Supported | Returns zeros up to file size |
| Read (AIO) | `-ENOTSUP` | Roadmap item |
| ReadV | `-ENOTSUP` | Roadmap item |
| Stat / Fstat | Supported | Returns Stub metadata |
| Mkdir / Remdir | Silent OK | POSIX clients require these |
| Create | `-ENOTSUP` | Use Open+write instead |
| Rename | `-ENOTSUP` | Roadmap item |
| Truncate | `-ENOTSUP` | Roadmap item |
| Unlink | Supported | Removes stub from map |
| Opendir / Readdir | `-ENOTSUP` | Roadmap item |
| XAttr (all) | `-ENOTSUP` | Stub plugin present |

---

## Roadmap

See `docs/ROADMAP.md` for the prioritised plan. Priority order:

1. **Unit tests** (no tests exist today — highest risk)
2. **AIO read + ReadV** (needed for realistic read benchmarks)
3. **Rename** (needed for GFAL2 atomic-upload compatibility)
4. **Directory listing** (`Opendir`/`Readdir` from in-memory map)
5. **Checksum responses** (populate `m_checksums` in Stub)
6. **Prometheus metrics endpoint**
7. **Per-path QoS throttle rules**
8. **Persistent stub registry**
