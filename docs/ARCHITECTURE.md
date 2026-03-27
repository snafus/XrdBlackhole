# XrdBlackhole — Architecture Reference

## Overview

XrdBlackhole is an XRootD **OSS (Object Storage System) plugin**. It replaces
the default POSIX storage layer with an in-memory virtual filesystem whose
write path discards all data and whose read path synthesises zero-filled
responses. No data is ever persisted to disk.

Primary use cases:
- Storage throughput benchmarking (saturate the network without disk I/O)
- Protocol and middleware correctness testing (GFAL2, XRootD clients, FTS3)
- CI pipeline testing requiring a live XRootD endpoint without storage

---

## XRootD Plugin Architecture

```
xrootd process
│
├── XrdSfs  (XRootD filesystem abstraction)
│   └── XrdOfs  (default OFS layer — handles protocol, auth, permissions)
│       │
│       └── ofs.osslib  ←── XrdBlackholeOss  (replaces XrdOssSys)
│               │
│               ├── newFile()  → XrdBlackholeOssFile   (one per open fd)
│               └── newDir()   → XrdBlackholeOssDir    (Opendir/Readdir from in-memory snapshot)
│
└── ofs.xattrlib  ←── XrdBlackholeXAttr  (stub; all xattr ops -ENOTSUP)
```

XRootD loads the plugin at startup via the `ofs.osslib` directive. The plugin
exports the C symbol `XrdOssGetStorageSystem`, which returns an `XrdOss*`.
XRootD then calls all storage operations through that interface.

---

## Component Map

### `BlackholeFS`  (`BlackholeFS.hh/.cc`)

The in-memory virtual filesystem. Owns all `Stub` objects.

```
BlackholeFS
├── m_files  :  map<string, shared_ptr<Stub>>   // path → file metadata
├── m_fd_last : uint64                           // monotonic fd counter
└── m_mutexFD : mutex                            // serialises all map ops
```

**`Stub`** — one per file in the namespace:
```cpp
struct Stub {
  bool   m_isOpen;           // currently open
  bool   m_isOpenWrite;      // opened for write
  int    m_flags, m_mode;
  size_t m_size;             // logical size (updated on close)
  struct stat m_stat;        // returned by Stat()/Fstat()
  uint64_t m_fd;             // unique inode / fd value
  bool   m_special;          // true for defaultspath pre-seeded files
  string m_readtype;         // "zeros" (only value supported)
  map<string,string> m_checksums; // populated in future
};
```

All `BlackholeFS` methods acquire `m_mutexFD` for their **entire duration**
to prevent TOCTOU races between existence checks and map mutations.

---

### `XrdBlackholeOss`  (`XrdBlackholeOss.hh/.cc`)

Implements `XrdOss`. Entry point for the plugin.

Responsibilities:
- Parse config directives from the XRootD config file
- Implement directory-level operations (`Mkdir`, `Remdir`, `Stat`, `Unlink`, …)
- Factory for `XrdBlackholeOssFile` and `XrdBlackholeOssDir` handles
- Log effective config on startup; call `g_statsManager.logSummary()` on shutdown

Config directives parsed in `Configure()`:

| Directive | Type | Default | Effect |
|---|---|---|---|
| `blackhole.writespeedMiBps` | `unsigned long` | 0 (unlimited) | Throttle write throughput |
| `blackhole.defaultspath` | `string` | (none) | Pre-seed three fixed zero-filled test files |
| `blackhole.seedfile` | `path size [count=N] [type=…]` | (none) | Pre-seed one or more stubs with custom path, size, and fill pattern |
| `blackhole.readtype` | `string` | `zeros` | Default read fill pattern (`zeros` only) |

---

### `XrdBlackholeOssFile`  (`XrdBlackholeOssFile.hh/.cc`)

Implements `XrdOssDF`. One instance per open file descriptor.

**Lifecycle:**

```
newFile(tident)
    │
    ▼
Open(path, flags, mode, env)
    ├── g_blackholeFS.open(path, flags, mode)   // creates/finds Stub
    ├── m_stub = g_blackholeFS.getStub(path)    // shared_ptr keeps Stub alive
    └── initialise atomic counters + m_stats

    │  (zero or more I/O calls — may be concurrent)
    ▼
Read(buff, offset, blen)          Write(buff, offset, blen)
    ├── bounds-check offset            ├── optional throttle sleep
    ├── memset(buff, 0, n)             └── m_writeBytes += blen  (atomic)
    └── m_readBytes += n   (atomic)

Close(retsz)
    ├── update Stub size from m_writeBytes + m_writeBytesAIO
    ├── populate TransferStats from atomics
    ├── g_statsManager.recordTransfer(m_stats)
    ├── g_blackholeFS.close(path)
    └── m_stub.reset()
```

**Atomic counters** (all `std::atomic`):

| Field | Updated by | Purpose |
|---|---|---|
| `m_writeBytes` | `Write(sync)` | Sync write bytes |
| `m_writeBytesAIO` | `Write(AIO)` | AIO write bytes |
| `m_readBytes` | `Read(sync)` | Read bytes |
| `m_writeOps` | `Write(sync)` | Sync write call count |
| `m_writeAioOps` | `Write(AIO)` | AIO write call count |
| `m_readOps` | `Read(sync)` | Read call count |
| `m_errors` | `Read`, `Read(pre)` | Error count |

All counters are reset to zero in `Open()` and read into `TransferStats`
exactly once in `Close()`, so `recordTransfer()` always sees a consistent
snapshot.

---

### `XrdBlackholeStatsManager`  (`XrdBlackholeStats.hh/.cc`)

Global singleton (`g_statsManager`). Thread-safe via `m_mutex`.

**`recordTransfer(TransferStats)`** — called from `Close()`:
- Logs a `[XFER]` line with path, op, bytes, duration, throughput, op counts
- Accumulates global aggregate counters under lock

**`logSummary()`** — called from `~XrdBlackholeOss()`:
- Logs one `[STATS]` line with lifetime totals and averages

Log format:
```
[XFER] path=/foo/bar op=write written=1073741824 read=0 duration_us=1048576
       write_MiBs=1024.00 read_MiBs=0.00 write_ops=256 aio_ops=0 read_ops=0 errors=0

[STATS] transfers=42 written=42949672960 read=0 errors=0
        avg_write_MiBs=987.23 avg_read_MiBs=0.00
```

---

## Thread Safety Model

| Resource | Protected by | Notes |
|---|---|---|
| `BlackholeFS::m_files` | `BlackholeFS::m_mutexFD` | Whole-operation lock (no TOCTOU) |
| `Stub` fields | Not locked | Mutated only within `BlackholeFS` lock or by the single owning file handle |
| `XrdBlackholeOssFile` byte/op counters | `std::atomic` | Lock-free concurrent R/W |
| `XrdBlackholeStatsManager` aggregates | `m_mutex` | Per-transfer lock |
| `Stub` lifetime | `shared_ptr` ref count | File handle keeps Stub alive through Close even if unlinked concurrently |

**Key design decision**: `getStub()` returns `shared_ptr<Stub>`. This means
that even if `unlink()` removes the entry from the map between the caller's
null-check and the first field access, the `Stub` object itself is not
destroyed until the last `shared_ptr` drops — preventing use-after-free.

---

## Write Data Flow

```
Client  ──xrootd protocol──►  XrdOfs  ──►  XrdBlackholeOssFile::Write()
                                                    │
                              ┌─────────────────────┘
                              │  if writespeedMiBps > 0:
                              │    sleep( blen / speed )   // µs precision
                              │
                              │  m_writeBytes += blen      // atomic
                              │  m_writeOps++              // atomic
                              │
                              └──► return blen             // success, data discarded
```

## Read Data Flow

```
Client  ──xrootd protocol──►  XrdOfs  ──►  XrdBlackholeOssFile::Read()
                                                    │
                              ┌─────────────────────┘
                              │  if offset >= m_size: return 0   // EOF guard
                              │  n = min(blen, m_size - offset)
                              │  memset(buff, 0, n)              // synthesise zeros
                              │  m_readBytes += n                // atomic
                              │  m_readOps++                     // atomic
                              └──► return n
```

---

## Pre-Seeded Files

Two config mechanisms create stubs at startup that are readable immediately
without any prior write step. Both set `m_special = true` and
`m_isOpenWrite = false`.

### `blackhole.defaultspath`

`BlackholeFS::create_defaults()` inserts three fixed zero-filled stubs:

| Path | Size |
|---|---|
| `<path>/testfile_zeros_1MiB` | 1 MiB |
| `<path>/testfile_zeros_1GiB` | 1 GiB |
| `<path>/testfile_zeros_10GiB` | 10 GiB |

### `blackhole.seedfile`

`BlackholeFS::seed(path, size, readtype)` creates a single stub with:
- `m_size` and `m_stat.st_size` set to the configured size
- `m_readtype` set to `"zeros"` or `"random"`
- For `type=random`: reads use a deterministic LCG seeded by
  `offset ^ st_ino` — the same offset always yields the same bytes

`cfg_seedfile()` in `XrdBlackholeOss::Configure()` parses the directive,
expands `count=N` into N calls to `seed()`, and validates that `count>1`
requires a `printf` format specifier in the path.

---

## Global Singletons

Declared in `XrdBlackholeOss.hh`, defined in `XrdBlackholeOss.cc`:

```cpp
BlackholeFS              g_blackholeFS;    // in-memory namespace
XrdSysError              XrdBlackholeEroute; // XRootD logger
XrdOucTrace              XrdBlackholeTrace;  // trace-gated logger
XrdBlackholeStatsManager g_statsManager;    // defined in XrdBlackholeStats.cc
```

---

## Tracing

Trace-gated I/O logging uses the `BHTRACE(expr)` macro:

```cpp
#define BHTRACE(x) \
  if (XrdBlackholeTrace.What & BHTRACE_IO) { \
    std::ostringstream _bh_oss; _bh_oss << x; \
    XrdBlackholeEroute.Say(_bh_oss.str().c_str()); }
```

Enable at runtime: `xrootd.trace all` in the XRootD config, or set
`XrdBlackholeTrace.What |= BHTRACE_IO` programmatically.

---

## Known Limitations

| Limitation | Impact | Roadmap fix |
|---|---|---|
| No checksum responses | Pipelines requiring checksum verification fail | Phase 2 (2.5) |
| StatFS reports zero free space | FTS3 space-token checks may fail | Phase 2 (2.6) |
| No Truncate | Some workflows can't resize files | Phase 3 (3.5) |
| No per-path throttle | Single global write speed limit | Phase 3 (3.1) |
| No read throughput throttle | Read speed always at memory speed | Phase 3 (3.4) |
| Stub state not persistent | Namespace lost on server restart | Phase 4 (4.1) |
