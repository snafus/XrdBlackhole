# XrdBlackhole

An XRootD OSS plugin providing a **blackhole storage backend** for benchmarking
and integration testing.

- **Writes** are accepted and silently discarded — no disk I/O, no storage required.
- **Reads** return zero-filled or deterministic pseudo-random data at wire speed.
- **Pre-seeded files** of any size are immediately available for read benchmarks.
- **Prometheus metrics** expose transfer throughput via an optional HTTP endpoint.

---

## Quick Start

### Docker

```bash
docker run --rm -p 1094:1094 ghcr.io/snafus/xrdblackhole \
    /usr/bin/xrootd -f -c /etc/xrootd/xrootd-blackhole.cfg -l /dev/stderr
```

### Minimal server config

```
all.role server
xrd.port 1094

ofs.osslib /usr/lib64/libXrdBlackhole.so

all.export / rw
```

### Write benchmark

```bash
# Write a 10 GiB stream from /dev/zero; data is discarded on arrival
xrdcp /dev/zero root://localhost//data/10GiB --streams 4 --cksum none
```

### Read benchmark

Add `blackhole.defaultspath /test` to the server config, then:

```bash
# Read the pre-seeded 10 GiB file at full network speed
xrdcp root://localhost//test/testfile_zeros_10GiB /dev/null --streams 4
```

---

## Install

RPMs for Rocky Linux 8 and 9 are attached to each
[GitHub release](https://github.com/snafus/XrdBlackhole/releases).

```bash
dnf install xrootd-blackhole-<version>.el9.x86_64.rpm
```

Or build from source — see [Build Instructions](configuration.md#build-instructions).

---

## Use Cases

| Scenario | How XrdBlackhole helps |
|---|---|
| **Write throughput benchmarking** | Removes disk I/O as a bottleneck; clients push at wire speed. |
| **Write speed simulation** | `blackhole.writespeedMiBps` throttles writes to mimic real storage. |
| **Read throughput benchmarking** | Pre-seeded files of any size; no prior write needed. |
| **Client pipeline testing** | Server always responds correctly without needing working storage. |
| **Protocol correctness testing** | Validates open/read/write/close, AIO callbacks, stat checks. |
| **Observability testing** | Scrape `/metrics` to test monitoring pipelines against live transfers. |

See [Configuration](configuration.md) for all available directives.
