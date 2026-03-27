# Configuration Reference

All XrdBlackhole directives are placed in the standard XRootD configuration
file and are prefixed with `blackhole.`.

---

## Loading the plugin

```
ofs.osslib  /usr/lib64/libXrdBlackhole.so
```

XRootD automatically appends the `-5` ABI version suffix when searching for
the library. Do **not** include it in the directive — specifying
`libXrdBlackhole-5.so` causes a double-versioned lookup failure.

Optional xattr stub (all operations return `-ENOTSUP`; harmless to omit):

```
ofs.xattrlib /usr/lib64/libXrdBlackholeXattr.so
```

Optional Prometheus metrics endpoint (requires XrdHttp to be enabled):

```
xrd.protocol XrdHttp:1094 libXrdHttp.so
http.exthandler bhmetrics /usr/lib64/libXrdBlackholeMetrics-5.so
```

!!! note
    `http.exthandler` is loaded via `dlopen` directly (not through XRootD's
    versioned lookup), so the full filename including `-5` **is** required here.

---

## Directives

### `blackhole.writespeedMiBps <N>`

Throttle the apparent write throughput to `N` MiB/s. Each `Write()` call
sleeps for `blen / (N × 1024²)` seconds before returning.

- `N` must be a positive integer.
- Omit (or set to 0) for unlimited write speed (the default).
- Most accurate for large sequential writes (≥ 1 MiB per call); integer
  truncation means very small writes may not be throttled accurately.

```
blackhole.writespeedMiBps 1024
```

---

### `blackhole.defaultspath <path>`

Pre-populate the in-memory filesystem at startup with three fixed zero-filled
test files rooted at `<path>`:

| File | Size |
|---|---|
| `<path>/testfile_zeros_1MiB` | 1 MiB |
| `<path>/testfile_zeros_1GiB` | 1 GiB |
| `<path>/testfile_zeros_10GiB` | 10 GiB |

Files are readable immediately without any prior write step. They persist for
the lifetime of the server process.

```
blackhole.defaultspath /test
```

---

### `blackhole.seedfile <path> <size>[K|M|G|T] [count=N] [type=zeros|random]`

Create one or more pre-seeded stubs at startup with full control over path,
size, count, and read-data pattern.

**Parameters:**

| Parameter | Description |
|---|---|
| `path` | Absolute path of the file to create. With `count=N`, must contain a `printf` integer format specifier (e.g. `%04d`). |
| `size` | File size in bytes. Accepts `K`, `M`, `G`, `T` suffixes (powers of 1024, case-insensitive). |
| `count=N` | Create N files numbered 0 through N-1 by expanding the format specifier in `path`. Default: 1. |
| `type=zeros` | (default) Reads return a zero-filled buffer. |
| `type=random` | Reads return a deterministic pseudo-random byte stream, seeded by `offset ^ st_ino`. The same offset always yields the same bytes, enabling client-side checksum verification. |

Multiple `blackhole.seedfile` directives may appear in the config.

```
# Single 4 GiB zero file
blackhole.seedfile /data/4GiB.root  4G

# 100 files of 1 GiB each: /data/f_0000.root … /data/f_0099.root
blackhole.seedfile /data/f_%04d.root  1G  count=100

# 1 GiB file with deterministic pseudo-random content
blackhole.seedfile /data/rand.root  1G  type=random
```

---

### `blackhole.readtype <type>`

Default read-data pattern for files that do not have a per-stub type set.
Currently only `zeros` is accepted (the default). Reserved for future
extension.

```
blackhole.readtype zeros
```

---

## Example configurations

### Minimal — unlimited write speed

```
all.role server
xrd.port 1094

ofs.osslib /usr/lib64/libXrdBlackhole.so

all.export / rw
```

### Write speed simulation — 1 GiB/s

```
all.role server
xrd.port 1094

ofs.osslib /usr/lib64/libXrdBlackhole.so
blackhole.writespeedMiBps 1024

all.export / rw
```

### Read benchmarking — pre-seeded files under `/data`

```
all.role server
xrd.port 1094

ofs.osslib /usr/lib64/libXrdBlackhole.so
blackhole.defaultspath /data

all.export / rw
```

### Full — HTTP, Prometheus metrics, throttle, and custom seed files

```
all.role server
xrd.port 1094
xrd.protocol XrdHttp:1094 libXrdHttp.so

ofs.osslib   /usr/lib64/libXrdBlackhole.so
ofs.xattrlib /usr/lib64/libXrdBlackholeXattr.so

http.exthandler bhmetrics /usr/lib64/libXrdBlackholeMetrics-5.so

blackhole.writespeedMiBps 500
blackhole.defaultspath /test
blackhole.seedfile /data/4GiB.root    4G
blackhole.seedfile /data/f_%04d.root  1G  count=100  type=random

all.export / rw
```

---

## Prometheus metrics

When `http.exthandler bhmetrics` is loaded, a Prometheus-compatible text
endpoint is available at `GET http://<host>:1094/metrics`.

| Metric | Type | Description |
|---|---|---|
| `blackhole_transfers_total{op="write\|read"}` | counter | Completed transfers |
| `blackhole_bytes_written_total` | counter | Total bytes accepted for writing |
| `blackhole_bytes_read_total` | counter | Total bytes returned for reading |
| `blackhole_errors_total` | counter | Transfer errors |
| `blackhole_write_throughput_MiBs_avg` | gauge | Lifetime average write throughput |
| `blackhole_read_throughput_MiBs_avg` | gauge | Lifetime average read throughput |

```bash
curl http://localhost:1094/metrics
```

---

## Build Instructions

### Prerequisites

- CMake 3.1+
- C++14 compiler (gcc or clang)
- XRootD development headers: `xrootd-devel`, `xrootd-private-devel`, `xrootd-server`

```bash
curl -L https://xrootd.web.cern.ch/xrootd.repo \
     -o /etc/yum.repos.d/xrootd.repo
dnf install xrootd-devel xrootd-private-devel xrootd-server cmake gcc-c++
```

### Build

```bash
mkdir build && cd build
cmake /path/to/XrdBlackhole -DCMAKE_BUILD_TYPE=RelWithDebInfo \
                             -DCMAKE_INSTALL_PREFIX=/usr
make -j$(nproc)
make install
```

### Output libraries

| Library | Loaded via | Purpose |
|---|---|---|
| `libXrdBlackhole-5.so` | `ofs.osslib` | Main OSS plugin |
| `libXrdBlackholeXattr-5.so` | `ofs.xattrlib` | XAttr stub (all no-ops) |
| `libXrdBlackholeMetrics-5.so` | `http.exthandler` | Prometheus `/metrics` endpoint |

### Docker

```bash
docker build -t xrootd-blackhole .
docker run --rm -p 1094:1094 xrootd-blackhole \
    /usr/bin/xrootd -f -c /etc/xrootd/xrootd-blackhole.cfg -l /dev/stderr
```
