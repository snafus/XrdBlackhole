
```
--------------------------------------------------------------------------------
                   _    _ ______                   _____
                  \ \  / (_____ \             _   (____ \
                   \ \/ / _____) ) ___   ___ | |_  _   \ \
                    )  ( (_____ ( / _ \ / _ \|  _)| |   | |
                   / /\ \      | | |_| | |_| | |__| |__/ /
                  /_/  \_\     |_|\___/ \___/ \___)_____/
--------------------------------------------------------------------------------
```

# XrdBlackhole

An XRootD OSS plugin that provides a **blackhole storage backend** for
benchmarking and integration testing.

- **Writes** are accepted and silently discarded. No data is ever written to
  disk. An optional write-speed throttle can simulate a target storage
  throughput.
- **Reads** return a zero-filled buffer of the correct size. Files written
  in a session are readable back with the size they were written at.
- **Pre-seeded test files** of configurable fixed sizes can be created at
  startup for read benchmarking without first having to write anything.
- **Prometheus metrics** are exposed at `GET /metrics` via an optional
  `XrdHttpExtHandler` plugin when XRootD's HTTP service is enabled.

---

## Contents

1. [Use Cases](#use-cases)
2. [How It Works](#how-it-works)
3. [Supported Operations](#supported-operations)
4. [Configuration Directives](#configuration-directives)
5. [Pre-seeded Test Files](#pre-seeded-test-files)
6. [Build Instructions](#build-instructions)
7. [Deployment](#deployment)
8. [Usage Examples](#usage-examples)
9. [Limitations](#limitations)
10. [License](#license)

---

## Use Cases

| Scenario | How XrdBlackhole Helps |
|---|---|
| **Write throughput benchmarking** | Eliminates disk I/O as a bottleneck. Clients push data at wire speed; the plugin discards it. |
| **Write speed simulation** | Configure `blackhole.writespeedMiBps` to throttle writes and mimic a real storage backend. |
| **Read throughput benchmarking** | Pre-seeded test files of any size allow clients to read at full network speed without needing to write first. |
| **Client pipeline testing** | Test XRootD clients, transfer tools, and middleware against a server that always responds correctly, without needing working storage. |
| **Protocol correctness testing** | Validates client behaviour (open/read/write/close sequencing, AIO callbacks, stat checks) in isolation. |
| **Observability testing** | Scrape the Prometheus `/metrics` endpoint to test monitoring pipelines against a live transfer stream. |

---

## How It Works

### In-memory filesystem

All file state is held in an in-memory map (`BlackholeFS`). Each open file is
tracked by a `Stub` record that holds its size and `struct stat`. The map is
protected by a mutex; file entries persist across open/close cycles until the
file is explicitly unlinked or the server process exits.

### Write path

```
client write ──► XrdBlackholeOssFile::Write()
                  │
                  ├─ optional sleep (writespeedMiBps throttle)
                  ├─ increment byte counter (m_writeBytes / m_writeBytesAIO)
                  └─ return blen  (data discarded)

client close ──► stub->m_size = total bytes written
                 stub->m_stat.st_size updated
                 transfer stats recorded to XrdBlackholeStatsManager
```

Both synchronous (`Write(const void*, off_t, size_t)`) and AIO
(`Write(XrdSfsAio*)`) paths are supported. The AIO path calls
`aiop->doneWrite()` directly before returning.

### Read path

```
client read ──► XrdBlackholeOssFile::Read(void*, off_t, size_t)
                  │
                  ├─ look up file size from stub
                  ├─ bytesremaining = min(blen, size - offset)
                  ├─ if type=zeros:  memset(buff, 0, bytesremaining)
                  │  if type=random: LCG fill seeded by (offset ^ st_ino)
                  └─ return bytesremaining
```

Reads beyond the file's registered size return 0 bytes (EOF). AIO reads
(`Read(XrdSfsAio*)`) delegate to the sync path and call `aiop->doneRead()`
before returning. Vectored reads (`ReadV`) iterate each segment and call the
sync path per entry.

### Transfer statistics

At close, each file handle records a `TransferStats` entry (path, duration,
bytes written/read, op counts, throughput) to a global `XrdBlackholeStatsManager`
singleton. The manager logs a per-transfer line and maintains aggregate counters.
These aggregates are exposed by the optional Prometheus metrics endpoint.

---

## Supported Operations

| Operation | Behaviour |
|---|---|
| `Open` (read) | Succeeds if file exists in the in-memory FS. |
| `Open` (write / O_TRUNC) | Creates a new stub entry; replaces any existing entry. |
| `Close` | Updates stub size from bytes written; records transfer stats. |
| `Read(void*, off_t, size_t)` | Returns zero-filled or deterministic pseudo-random bytes up to file size. |
| `ReadRaw` | Delegates to `Read(void*, off_t, size_t)`. |
| `Read(off_t, size_t)` | Preposition-only; validates stub exists, returns 0. |
| `Read(XrdSfsAio*)` | Async read; delegates to sync path, sets `aiop->Result`, calls `doneRead()`. |
| `ReadV` | Vectored read; iterates segments, calls sync `Read()` per entry. |
| `Write(const void*, off_t, size_t)` | Discards data; optionally throttles; returns `blen`. |
| `Write(XrdSfsAio*)` | Discards data; calls `doneWrite()`; optionally throttles. |
| `Fstat` | Returns stub `struct stat` with `st_mode = 0666 \| S_IFREG`. |
| `Stat` | Returns stub `struct stat` from the OSS layer. |
| `Unlink` | Removes the stub from the in-memory map. |
| `Rename` | Moves the stub to the new path; atomically replaces destination if present. |
| `Mkdir` / `Remdir` | Always returns success (required by POSIX-assuming clients such as GFAL2). |
| `Opendir` / `Readdir` | Returns a snapshot of direct children under the requested path. |
| `Fsync` / `Ftruncate` | Returns `-ENOTSUP`. |
| `Chmod` / `Create` / `Truncate` | Returns `-ENOTSUP`. |
| All XAttr operations | Returns `-ENOTSUP`. |

---

## Configuration Directives

All directives are placed in the standard xrootd configuration file and are
prefixed with `blackhole.`.

### `blackhole.writespeedMiBps <N>`

Throttle the apparent write throughput to `N` MiB/s. Each write call sleeps
for `blen / (N * 1024 * 1024) * 1000` milliseconds before returning.

- `N` must be a positive integer.
- Omit this directive (or set it to 0) for unlimited write speed (the default).
- Throttling is applied per write call, so it is most accurate for large,
  sequential writes. Small random writes will see less precise throttling due
  to integer truncation in the delay calculation.

```
blackhole.writespeedMiBps 500
```

### `blackhole.defaultspath <path>`

Pre-populate the in-memory filesystem with a fixed set of zero-filled test
files rooted at `<path>`. The files are created at plugin startup and are
immediately available for reading without any prior write step.

```
blackhole.defaultspath /test
```

See [Pre-seeded Test Files](#pre-seeded-test-files) for the files that are
created.

### `blackhole.seedfile <path> <size>[K|M|G|T] [count=N] [type=zeros|random]`

Create one or more pre-seeded stubs at startup. Unlike `defaultspath`, this
directive gives full control over path, size, count, and read-data pattern.

- `path` — absolute path of the file to create. When `count=N` is given,
  `path` must contain a `printf` integer format specifier (e.g. `%04d`) which
  is expanded to produce `N` distinct paths numbered `0` through `N-1`.
- `size` — file size in bytes. Accepts `K`, `M`, `G`, or `T` suffixes
  (powers of 1024, case-insensitive).
- `count=N` — create N files by expanding the format specifier in `path`.
  Defaults to 1.
- `type=zeros|random` — fill pattern for reads. `zeros` (default) returns a
  zero-filled buffer. `random` returns a deterministic pseudo-random byte
  stream seeded by the read offset and inode number, allowing checksum
  verification by clients.

```
# Single 4 GiB zero file
blackhole.seedfile /data/4GiB.root  4G

# 100 files of 1 GiB each, named /data/f_0000.root through /data/f_0099.root
blackhole.seedfile /data/f_%04d.root  1G  count=100

# A 1 GiB file whose content is deterministic pseudo-random bytes
blackhole.seedfile /data/rand.root  1G  type=random
```

### `blackhole.readtype <type>`

Controls the data pattern returned by reads for files opened without a
specific readtype. Currently only `zeros` is accepted (the default).
Reserved for future extension.

```
blackhole.readtype zeros
```

---

## Pre-seeded Test Files

There are two ways to pre-seed the in-memory filesystem at startup.

### `blackhole.defaultspath`

When `blackhole.defaultspath /test` is set, the following fixed files are
created automatically:

| Path | Size |
|---|---|
| `/test/testfile_zeros_1MiB` | 1 MiB (1,048,576 bytes) |
| `/test/testfile_zeros_1GiB` | 1 GiB (1,073,741,824 bytes) |
| `/test/testfile_zeros_10GiB` | 10 GiB (10,737,418,240 bytes) |

### `blackhole.seedfile`

For custom paths, sizes, counts, and content patterns, use
`blackhole.seedfile`. Multiple directives may appear in the config file:

```
blackhole.seedfile /data/4GiB.root    4G
blackhole.seedfile /data/f_%04d.root  1G  count=100
blackhole.seedfile /data/rand.root    1G  type=random
```

All pre-seeded files are present for the lifetime of the server process and
return their configured data pattern on every read.

---

## Build Instructions

### Prerequisites

- CMake 3.1 or later
- A C++14-capable compiler (gcc or clang)
- XRootD development headers:
  - `xrootd-devel`
  - `xrootd-private-devel`
  - `xrootd-server`

On RHEL/AlmaLinux, add the XRootD repository and install:

```bash
curl -L https://xrootd.web.cern.ch/xrootd.repo \
     -o /etc/yum.repos.d/xrootd.repo

dnf install xrootd-devel xrootd-private-devel xrootd-server cmake gcc-c++
```

### Build steps

```bash
# 1. Create an out-of-tree build directory
mkdir build && cd build

# 2. Configure (adjust install prefix as needed)
cmake /path/to/xrootd-blackhole -DCMAKE_BUILD_TYPE=RelWithDebInfo \
                                 -DCMAKE_INSTALL_PREFIX=/usr

# 3. Build
make -j$(nproc)

# 4. Install
make install
```

This produces up to three shared libraries:

| Library | Purpose |
|---|---|
| `libXrdBlackhole-5.so` | OSS plugin — load with `ofs.osslib` |
| `libXrdBlackholeXattr-5.so` | XAttr plugin — load with `ofs.xattrlib` (all ops are no-ops) |
| `libXrdBlackholeMetrics-5.so` | Prometheus HTTP handler — load with `http.exthandler` (optional; requires `xrootd-server-devel` with XrdHttp headers) |

The `5` suffix is the XRootD plugin ABI version. It is controlled by
`PLUGIN_VERSION` in `cmake/XRootDDefaults.cmake`.

### Docker

A self-contained AlmaLinux 9 image that builds and runs the plugin:

```bash
docker build -t xrootd-blackhole .
docker run --rm -p 1094:1094 xrootd-blackhole \
    /usr/bin/xrootd -f -c /etc/xrootd/xrootd-blackhole.cfg -l /dev/stderr
```

The Dockerfile uses a two-stage build. Stage 1 compiles and packages the
plugin as an RPM using the XRootD 5.x CERN repository. Stage 2 installs the
RPM into a clean AlmaLinux 9 runtime, producing an image of ~200 MB.

---

## Deployment

### Minimal configuration

Accept writes at unlimited speed; reads return zeros for any file that was
previously written.

```
# /etc/xrootd/xrootd-blackhole.cfg

all.role server
xrd.port 1094

ofs.osslib /usr/lib64/libXrdBlackhole.so

all.export / rw
```

### With write throttle

Simulate a 1 GiB/s storage backend:

```
# /etc/xrootd/xrootd-blackhole.cfg

all.role server
xrd.port 1094

ofs.osslib /usr/lib64/libXrdBlackhole.so
blackhole.writespeedMiBps 1024

all.export / rw
```

### With pre-seeded read test files

Make test files immediately available under `/data` for read benchmarking:

```
# /etc/xrootd/xrootd-blackhole.cfg

all.role server
xrd.port 1094

ofs.osslib /usr/lib64/libXrdBlackhole.so
blackhole.defaultspath /data

all.export / rw
```

### Full configuration with Prometheus metrics

Enable XrdHttp on port 1094 and expose a Prometheus-compatible `/metrics`
endpoint. The metrics handler is an optional subpackage that requires the
`xrootd-blackhole-metrics` RPM (or building with XrdHttp headers present).

```
# /etc/xrootd/xrootd-blackhole.cfg

all.role server
xrd.port 1094
xrd.protocol XrdHttp:1094 libXrdHttp.so

ofs.osslib   /usr/lib64/libXrdBlackhole.so
ofs.xattrlib /usr/lib64/libXrdBlackholeXattr.so

http.exthandler bhmetrics /usr/lib64/libXrdBlackholeMetrics.so

blackhole.writespeedMiBps 500
blackhole.defaultspath /test

all.export / rw
```

### Starting the server

```bash
# Foreground (log to stderr — useful for debugging)
xrootd -f -c /etc/xrootd/xrootd-blackhole.cfg -l /dev/stderr

# As a service (systemd)
systemctl start xrootd@blackhole
```

---

## Usage Examples

The examples below use `xrdcp` and assume the server is running on
`localhost:1094`. Any XRootD-compatible client will work.

### Write benchmarking

Write a 10 GiB file and discard it on the server side:

```bash
# Generate source data on-the-fly from /dev/zero
xrdcp /dev/zero root://localhost//data/bench_10GiB \
      --streams 4 \
      --cksum none

# Or write an existing local file
xrdcp /path/to/local/file root://localhost//data/myfile
```

The server logs the elapsed time and total bytes written at close:

```
[XFER] path=/data/bench_10GiB op=write written=10737418240 read=0 duration_us=9823451 write_MiBs=1043.21 ...
```

### Read benchmarking using pre-seeded files

Requires `blackhole.defaultspath /test` in the server config.

```bash
# Read the 10 GiB test file to /dev/null
xrdcp root://localhost//test/testfile_zeros_10GiB /dev/null \
      --streams 4

# Read with multiple parallel streams for maximum throughput
xrdcp root://localhost//test/testfile_zeros_1GiB /dev/null \
      --streams 16 \
      --chunklist 16
```

### Round-trip test (write then read back)

```bash
# Write 1 GiB
xrdcp /dev/zero root://localhost//data/roundtrip_1GiB

# Read it back — returns zeros, size matches what was written
xrdcp root://localhost//data/roundtrip_1GiB /dev/null
```

### Stat a file

```bash
xrdfs localhost stat /test/testfile_zeros_1GiB
```

Expected output:
```
Path:   /test/testfile_zeros_1GiB
Id:     ...
Flags:  0
Size:   1073741824
MTime:  ...
```

### Unlink a file

```bash
xrdfs localhost rm /data/myfile
```

### Prometheus metrics

With the metrics handler loaded and XrdHttp enabled on port 1094:

```bash
curl http://localhost:1094/metrics
```

Example output:

```
# HELP blackhole_transfers_total Total number of completed transfers
# TYPE blackhole_transfers_total counter
blackhole_transfers_total{op="write"} 42
blackhole_transfers_total{op="read"} 7
# HELP blackhole_bytes_written_total Total bytes accepted for writing
# TYPE blackhole_bytes_written_total counter
blackhole_bytes_written_total 451674817536
# HELP blackhole_bytes_read_total Total bytes returned for reading
# TYPE blackhole_bytes_read_total counter
blackhole_bytes_read_total 7516192768
# HELP blackhole_errors_total Total number of transfer errors
# TYPE blackhole_errors_total counter
blackhole_errors_total 0
# HELP blackhole_write_throughput_MiBs_avg Rolling average write throughput
# TYPE blackhole_write_throughput_MiBs_avg gauge
blackhole_write_throughput_MiBs_avg 987.23
# HELP blackhole_read_throughput_MiBs_avg Rolling average read throughput
# TYPE blackhole_read_throughput_MiBs_avg gauge
blackhole_read_throughput_MiBs_avg 3241.10
```

### Verify the server is alive

```bash
xrdfs localhost ping
```

---

## Limitations

- **No persistence.** The in-memory filesystem is wiped when the server
  process exits. Pre-seeded test files are re-created at next startup if
  `blackhole.defaultspath` or `blackhole.seedfile` directives are configured.

- **No extended attributes.** All `XrdSysXAttr` operations return `-ENOTSUP`.

- **No truncate or fsync.** `Ftruncate` and `Fsync` return `-ENOTSUP`.

- **Write throttle granularity.** The `writespeedMiBps` throttle sleeps
  per write call. For small writes the integer delay may truncate to zero,
  giving higher than configured throughput. The throttle is most accurate
  for large sequential writes (≥ 1 MiB per call).

- **Concurrent file access.** The same file can be opened simultaneously by
  multiple clients, but the byte counters (`m_writeBytes`, `m_writeBytesAIO`)
  are per file-handle, not shared. Concurrent writes to the same path will
  each track their own count independently.

- **Prometheus throughput metrics are lifetime averages**, not a sliding
  window. The `blackhole_*_throughput_MiBs_avg` gauges divide cumulative
  sum-of-throughputs by transfer count since process start.

---

## License

XrdBlackhole is distributed under the GNU Lesser General Public License v3
or later. See [COPYING.LGPL](COPYING.LGPL) for the full text.

Original plugin skeleton by Sebastien Ponce, CERN/IT-DSS.
