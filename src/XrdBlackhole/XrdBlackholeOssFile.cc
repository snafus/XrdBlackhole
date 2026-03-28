#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include <chrono>
#include <thread>

#include "XrdOuc/XrdOucEnv.hh"
#include "XrdSys/XrdSysError.hh"
#include "XrdSfs/XrdSfsAio.hh"

#include "XrdBlackhole/XrdBlackholeOssFile.hh"
#include "XrdBlackhole/XrdBlackholeOss.hh"

XrdBlackholeOssFile::XrdBlackholeOssFile(XrdBlackholeOss *bhOss) : m_bhOss(bhOss) {}

int XrdBlackholeOssFile::Open(const char *path, int flags, mode_t mode, XrdOucEnv &env) {
  m_path = path;

  int rc = g_blackholeFS.open(m_path, flags, mode);
  if (rc < 0) {
    XrdBlackholeEroute.Emsg("Open", -rc, "failed to open", path);
    return rc;
  }

  m_stub  = g_blackholeFS.getStub(m_path);
  m_start = std::chrono::high_resolution_clock::now();

  // Reset per-transfer counters and cache the stub size once so Read() never
  // needs to write m_size from a hot path (eliminates the data-race risk on
  // concurrent Read() calls).
  m_writeBytes    = 0;
  m_writeBytesAIO = 0;
  m_readBytes     = 0;
  m_writeOps      = 0;
  m_writeAioOps   = 0;
  m_readOps       = 0;
  m_errors        = 0;
  m_size               = m_stub ? m_stub->m_size : 0;
  m_isRandom           = m_stub && m_stub->m_readtype == "random";
  m_ino                = m_stub ? m_stub->m_stat.st_ino : 0;
  const double speed   = static_cast<double>(m_bhOss->writespeedMiBs());
  m_throttleBytesPerUs = speed > 0.0 ? speed * 1024.0 * 1024.0 / 1e6 : 0.0;

  // Initialise per-transfer stats.
  m_stats           = TransferStats{};
  m_stats.path      = path;
  m_stats.open_time = std::chrono::system_clock::now();
  m_stats.was_write = (flags & O_ACCMODE) != O_RDONLY;

  BHTRACE("Open path=" << path << " flags=" << std::hex << flags << std::dec
                       << " mode=" << mode);
  return XrdOssOK;
}

int XrdBlackholeOssFile::Close(long long *retsz) {
  if (!m_stub) {
    XrdBlackholeEroute.Emsg("Close", ENOENT, "closing unknown path", m_path.c_str());
    return -ENOENT;
  }

  m_end         = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(m_end - m_start);

  // Update the in-memory stub size from bytes written this session.
  ssize_t wb = m_writeBytes + m_writeBytesAIO;
  if (m_stub->m_isOpenWrite) {
    m_stub->m_size        = wb;
    m_stub->m_stat.st_size = wb;
  }

  // Finalise and submit transfer statistics.
  m_stats.duration_us   = duration.count();
  m_stats.bytes_written = wb;
  m_stats.bytes_read    = m_readBytes;
  m_stats.write_ops     = m_writeOps;
  m_stats.write_aio_ops = m_writeAioOps;
  m_stats.read_ops      = m_readOps;
  m_stats.errors        = m_errors;
  g_statsManager.recordTransfer(m_stats);

  g_blackholeFS.close(m_path);
  m_stub.reset();
  return XrdOssOK;
}

// Preposition-only read: no data transfer, just validates the stub exists.
ssize_t XrdBlackholeOssFile::Read(off_t offset, size_t blen) {
  if (!m_stub) {
    XrdBlackholeEroute.Emsg("Read", EINVAL, "no open stub for", m_path.c_str());
    m_errors++;
    return -EINVAL;
  }
  return XrdOssOK;
}

ssize_t XrdBlackholeOssFile::Read(void *buff, off_t offset, size_t blen) {
  if (!m_stub) {
    XrdBlackholeEroute.Emsg("Read", EINVAL, "no open stub for", m_path.c_str());
    m_errors++;
    return -EINVAL;
  }
  ssize_t n = ReadInner(buff, offset, blen);
  if (n > 0) {
    m_readBytes += n;
    m_readOps++;
  }
  BHTRACE("Read " << n << " bytes @ " << offset << " path=" << m_path);
  return n;
}

// Fill [buff, buff+blen) from the virtual file at [offset, offset+blen).
// Uses cached m_isRandom / m_ino set at Open() — no string comparison on the
// hot path.  The random branch unrolls the LCG 8 bytes per iteration to
// reduce loop overhead while preserving byte-identical output.
ssize_t XrdBlackholeOssFile::ReadInner(void *buff, off_t offset, size_t blen) const {
  if (offset < 0 || static_cast<size_t>(offset) >= m_size) return 0;
  size_t n = std::min(blen, m_size - static_cast<size_t>(offset));
  if (m_isRandom) {
    static constexpr uint64_t K = 6364136223846793005ULL;
    static constexpr uint64_t C = 1442695040888963407ULL;
    uint64_t state = static_cast<uint64_t>(offset) ^ m_ino;
    auto *out = static_cast<uint8_t*>(buff);
    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
      state = state * K + C; const uint8_t b0 = static_cast<uint8_t>(state >> 56);
      state = state * K + C; const uint8_t b1 = static_cast<uint8_t>(state >> 56);
      state = state * K + C; const uint8_t b2 = static_cast<uint8_t>(state >> 56);
      state = state * K + C; const uint8_t b3 = static_cast<uint8_t>(state >> 56);
      state = state * K + C; const uint8_t b4 = static_cast<uint8_t>(state >> 56);
      state = state * K + C; const uint8_t b5 = static_cast<uint8_t>(state >> 56);
      state = state * K + C; const uint8_t b6 = static_cast<uint8_t>(state >> 56);
      state = state * K + C; const uint8_t b7 = static_cast<uint8_t>(state >> 56);
      out[i]   = b0; out[i+1] = b1; out[i+2] = b2; out[i+3] = b3;
      out[i+4] = b4; out[i+5] = b5; out[i+6] = b6; out[i+7] = b7;
    }
    for (; i < n; i++) {
      state = state * K + C;
      out[i] = static_cast<uint8_t>(state >> 56);
    }
  } else {
    memset(buff, 0, n);
  }
  return static_cast<ssize_t>(n);
}

int XrdBlackholeOssFile::Read(XrdSfsAio *aiop) {
  size_t blen   = aiop->sfsAio.aio_nbytes;
  off_t  offset = aiop->sfsAio.aio_offset;
  void  *buff   = const_cast<void*>(
                    static_cast<volatile void*>(aiop->sfsAio.aio_buf));
  aiop->Result  = Read(buff, offset, blen);
  aiop->doneRead();
  BHTRACE("ReadAIO " << blen << " bytes @ " << offset << " path=" << m_path);
  return XrdOssOK;
}

ssize_t XrdBlackholeOssFile::ReadRaw(void *buff, off_t offset, size_t blen) {
  return Read(buff, offset, blen);
}

ssize_t XrdBlackholeOssFile::ReadV(XrdOucIOVec *readV, int n) {
  if (n <= 0) return 0;
  if (!m_stub) {
    XrdBlackholeEroute.Emsg("ReadV", EINVAL, "no open stub for", m_path.c_str());
    m_errors++;
    return -EINVAL;
  }
  // Accumulate totals locally and update atomics once after the loop.
  ssize_t  total = 0;
  uint32_t ops   = 0;
  for (int i = 0; i < n; i++) {
    if (readV[i].size <= 0) continue;
    ssize_t rc = ReadInner(readV[i].data,
                           static_cast<off_t>(readV[i].offset),
                           static_cast<size_t>(readV[i].size));
    if (rc < 0) return rc;
    total += rc;
    ops++;
  }
  m_readBytes += total;
  m_readOps   += ops;
  BHTRACE("ReadV " << n << " segs total=" << total << " path=" << m_path);
  return total;
}

int XrdBlackholeOssFile::Fstat(struct stat *buf) {
  if (!m_stub) {
    XrdBlackholeEroute.Emsg("Fstat", ENOENT, "no open stub for", m_path.c_str());
    return -ENOENT;
  }
  *buf = m_stub->m_stat;
  buf->st_mode = 0666 | S_IFREG;
  return XrdOssOK;
}

ssize_t XrdBlackholeOssFile::Write(const void *buff, off_t offset, size_t blen) {
  if (m_throttleBytesPerUs > 0.0) {
    auto delay_us = static_cast<long long>(
      static_cast<double>(blen) / m_throttleBytesPerUs);
    std::this_thread::sleep_for(std::chrono::microseconds(delay_us));
  }
  m_writeBytes += blen;
  m_writeOps++;
  BHTRACE("Write " << blen << " bytes @ " << offset << " path=" << m_path);
  return blen;
}

int XrdBlackholeOssFile::Write(XrdSfsAio *aiop) {
  // aio_nbytes is size_t (unsigned); keep it unsigned to avoid ssize_t truncation
  // for large (>SSIZE_MAX) write requests.
  const size_t nbytes = aiop->sfsAio.aio_nbytes;
  if (m_throttleBytesPerUs > 0.0) {
    auto delay_us = static_cast<long long>(
      static_cast<double>(nbytes) / m_throttleBytesPerUs);
    std::this_thread::sleep_for(std::chrono::microseconds(delay_us));
  }
  aiop->Result = static_cast<ssize_t>(nbytes);
  aiop->doneWrite();
  m_writeBytesAIO += static_cast<ssize_t>(nbytes);
  m_writeAioOps++;
  BHTRACE("WriteAIO " << nbytes << " bytes @ " << aiop->sfsAio.aio_offset
                      << " path=" << m_path);
  return XrdOssOK;
}

int XrdBlackholeOssFile::Fsync() {
  return -ENOTSUP;
}

int XrdBlackholeOssFile::Ftruncate(unsigned long long len) {
  return -ENOTSUP;
}
