
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
                  ├─ memset(buff, 0, bytesremaining)
                  └─ return bytesremaining
```

Reads beyond the file's registered size return 0 bytes (EOF). AIO reads and
vectored reads (`ReadV`) return `-ENOTSUP`.

---

## Supported Operations

| Operation | Behaviour |
|---|---|
| `Open` (read) | Succeeds if file exists in the in-memory FS. |
| `Open` (write / O_TRUNC) | Creates a new stub entry; replaces any existing entry. |
| `Close` | Updates stub size from bytes written; clears open flags. |
| `Read(void*, off_t, size_t)` | Returns zero-filled bytes up to file size. |
| `ReadRaw` | Delegates to `Read(void*, off_t, size_t)`. |
| `Read(off_t, size_t)` | Preposition-only; validates stub exists, returns 0. |
| `Write(const void*, off_t, size_t)` | Discards data; optionally throttles; returns `blen`. |
| `Write(XrdSfsAio*)` | Discards data; calls `doneWrite()`; optionally throttles. |
| `Fstat` | Returns stub `struct stat` with `st_mode = 0666 \| S_IFREG`. |
| `Stat` | Returns stub `struct stat` from the OSS layer. |
| `Unlink` | Removes the stub from the in-memory map. |
| `Mkdir` / `Remdir` | Always returns success (required by POSIX-assuming clients such as GFAL2). |
| `Read(XrdSfsAio*)` | Returns `-ENOTSUP`. |
| `ReadV` | Returns `-ENOTSUP`. |
| `Fsync` / `Ftruncate` | Returns `-ENOTSUP`. |
| `Chmod` / `Create` / `Rename` / `Truncate` | Returns `-ENOTSUP`. |
| `Opendir` / `Readdir` | Returns `-ENOTSUP`. |
| All XAttr operations | Returns `-ENOTSUP`. |

---

## Configuration Directives

Both directives are placed in the standard xrootd configuration file and are
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

---

## Pre-seeded Test Files

When `blackhole.defaultspath /test` is set, the following files are created
in the in-memory filesystem at startup:

| Path | Size |
|---|---|
| `/test/testfile_zeros_1MiB` | 1 MiB (1,048,576 bytes) |
| `/test/testfile_zeros_1GiB` | 1 GiB (1,073,741,824 bytes) |
| `/test/testfile_zeros_10GiB` | 10 GiB (10,737,418,240 bytes) |

All files return zeros on read. They are present for the lifetime of the
server process and cannot be overwritten (they have no write-open path).

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

This produces two shared libraries:

| Library | Purpose |
|---|---|
| `libXrdBlackhole-5.so` | OSS plugin — load with `ofs.osslib` |
| `libXrdBlackholeXattr-5.so` | XAttr plugin — load with `ofs.xattrlib` (all ops are no-ops) |

The `5` suffix is the XRootD plugin ABI version. It is controlled by
`PLUGIN_VERSION` in `cmake/XRootDDefaults.cmake`.

### Docker

A self-contained AlmaLinux 9 image that builds and runs the plugin:

```bash
docker build -t xrootd-blackhole .
docker run --rm -p 1094:1094 xrootd-blackhole \
    /usr/bin/xrootd -f -c /etc/xrootd/xrootd-blackhole.cfg -l /dev/stderr
```

The Dockerfile installs XRootD 5.x from the official CERN repository and
links the built libraries into the standard plugin search path
(`/usr/lib64/xrootd/`).

---

## Deployment

### Minimal configuration

Accept writes at unlimited speed; reads return zeros for any file that was
previously written.

```
# /etc/xrootd/xrootd-blackhole.cfg

all.role server
xrd.port 1094

ofs.osslib /usr/lib64/xrootd/libXrdBlackhole-5.so

all.export / rw
```

### With write throttle

Simulate a 1 GiB/s storage backend:

```
# /etc/xrootd/xrootd-blackhole.cfg

all.role server
xrd.port 1094

ofs.osslib /usr/lib64/xrootd/libXrdBlackhole-5.so
blackhole.writespeedMiBps 1024

all.export / rw
```

### With pre-seeded read test files

Make test files immediately available under `/data` for read benchmarking:

```
# /etc/xrootd/xrootd-blackhole.cfg

all.role server
xrd.port 1094

ofs.osslib /usr/lib64/xrootd/libXrdBlackhole-5.so
blackhole.defaultspath /data

all.export / rw
```

### With XAttr plugin and HTTP

Full configuration with both plugins and HTTP support:

```
# /etc/xrootd/xrootd-blackhole.cfg

all.role server
xrd.port 1094
xrd.protocol XrdHttp:1094 libXrdHttp.so

ofs.osslib  /usr/lib64/xrootd/libXrdBlackhole-5.so
ofs.xattrlib /usr/lib64/xrootd/libXrdBlackholeXattr-5.so

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
Close: /data/bench_10GiB, 9823451us, Write: 10737418240, WriteAIO: 0
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

### Verify the server is alive

```bash
xrdfs localhost ping
```

---

## Limitations

- **No persistence.** The in-memory filesystem is wiped when the server
  process exits. Pre-seeded test files are re-created at next startup if
  `blackhole.defaultspath` is configured.

- **No directory listing.** `Opendir` and `Readdir` return `-ENOTSUP`.
  There is no namespace to enumerate.

- **No extended attributes.** All `XrdSysXAttr` operations return `-ENOTSUP`.

- **No truncate or fsync.** `Ftruncate` and `Fsync` return `-ENOTSUP`.

- **No AIO reads.** `Read(XrdSfsAio*)` and `ReadV` return `-ENOTSUP`.

- **Write throttle granularity.** The `writespeedMiBps` throttle sleeps
  per write call. For small writes the integer delay may truncate to zero,
  giving higher than configured throughput. The throttle is most accurate
  for large sequential writes (≥ 1 MiB per call).

- **Concurrent file access.** The same file can be opened simultaneously by
  multiple clients, but the byte counters (`m_writeBytes`, `m_writeBytesAIO`)
  are per file-handle, not shared. Concurrent writes to the same path will
  each track their own count independently.

---

## License

XrdBlackhole is distributed under the GNU Lesser General Public License v3
or later. See [COPYING.LGPL](COPYING.LGPL) for the full text.

Original plugin skeleton by Sebastien Ponce, CERN/IT-DSS.
