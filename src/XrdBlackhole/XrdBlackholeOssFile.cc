#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#include <chrono>
#include <thread>

#include "XrdOuc/XrdOucEnv.hh"
#include "XrdSys/XrdSysError.hh"
#include "XrdSfs/XrdSfsAio.hh"

#include "XrdBlackhole/XrdBlackholeOssFile.hh"
#include "XrdBlackhole/XrdBlackholeOss.hh"

using namespace std::chrono_literals;

XrdBlackholeOssFile::XrdBlackholeOssFile(XrdBlackholeOss *bhOss) : m_fd(-1), m_bhOss(bhOss) {}

int XrdBlackholeOssFile::Open(const char *path, int flags, mode_t mode, XrdOucEnv &env) {
  m_path = path;

  int rc = g_blackholeFS.open(m_path, flags, mode);
  if (rc < 0) {
    XrdBlackholeEroute.Emsg("Open", -rc, "failed to open", path);
    return rc;
  }

  m_stub  = g_blackholeFS.getStub(m_path);
  m_start = std::chrono::high_resolution_clock::now();

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
  if (!g_blackholeFS.exists(m_path)) {
    XrdBlackholeEroute.Emsg("Close", ENOENT, "closing unknown path", m_path.c_str());
    m_stats.errors++;
    return -ENOENT;
  }

  m_end         = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(m_end - m_start);

  // Update the in-memory stub size from bytes written this session.
  auto stub = g_blackholeFS.getStub(m_path);
  if (stub->m_isOpenWrite) {
    stub->m_size        = m_writeBytes + m_writeBytesAIO;
    stub->m_stat.st_size = stub->m_size;
  }

  // Finalise and submit transfer statistics.
  m_stats.duration_us   = duration.count();
  m_stats.bytes_written = m_writeBytes + m_writeBytesAIO;
  m_stats.bytes_read    = m_readBytes;
  g_statsManager.recordTransfer(m_stats);

  g_blackholeFS.close(m_path);
  m_stub = nullptr;
  return XrdOssOK;
}

// Preposition-only read: no data transfer, just validates the stub exists.
ssize_t XrdBlackholeOssFile::Read(off_t offset, size_t blen) {
  if (!m_stub) {
    XrdBlackholeEroute.Emsg("Read", EINVAL, "no open stub for", m_path.c_str());
    m_stats.errors++;
    return -EINVAL;
  }
  return XrdOssOK;
}

ssize_t XrdBlackholeOssFile::Read(void *buff, off_t offset, size_t blen) {
  if (!m_stub) {
    XrdBlackholeEroute.Emsg("Read", EINVAL, "no open stub for", m_path.c_str());
    m_stats.errors++;
    return -EINVAL;
  }
  if (m_size == 0) {
    m_size = g_blackholeFS.getStub(m_path)->m_size;
  }
  size_t n = std::min(blen, m_size - static_cast<size_t>(offset));
  memset(buff, 0, n);

  m_readBytes += n;
  m_stats.read_ops++;
  BHTRACE("Read " << n << " bytes @ " << offset << " path=" << m_path);
  return n;
}

int XrdBlackholeOssFile::Read(XrdSfsAio *aiop) {
  return -ENOTSUP;
}

ssize_t XrdBlackholeOssFile::ReadRaw(void *buff, off_t offset, size_t blen) {
  return Read(buff, offset, blen);
}

ssize_t XrdBlackholeOssFile::ReadV(XrdOucIOVec *readV, int n) {
  return -ENOTSUP;
}

int XrdBlackholeOssFile::Fstat(struct stat *buf) {
  if (!g_blackholeFS.exists(m_path)) {
    XrdBlackholeEroute.Emsg("Fstat", ENOENT, "path not found", m_path.c_str());
    return -ENOENT;
  }
  *buf = g_blackholeFS.getStub(m_path)->m_stat;
  buf->st_mode = 0666 | S_IFREG;
  return XrdOssOK;
}

ssize_t XrdBlackholeOssFile::Write(const void *buff, off_t offset, size_t blen) {
  if (m_bhOss->writespeedMiBs() > 0) {
    int delay = blen / (m_bhOss->writespeedMiBs() * 1024 * 1024) * 1000;
    std::this_thread::sleep_for(delay * 1ms);
  }
  m_writeBytes += blen;
  m_stats.write_ops++;
  BHTRACE("Write " << blen << " bytes @ " << offset << " path=" << m_path);
  return blen;
}

int XrdBlackholeOssFile::Write(XrdSfsAio *aiop) {
  ssize_t rc  = aiop->sfsAio.aio_nbytes;
  if (m_bhOss->writespeedMiBs() > 0) {
    int delay = rc / (m_bhOss->writespeedMiBs() * 1024 * 1024) * 1000;
    std::this_thread::sleep_for(delay * 1ms);
  }
  aiop->Result = rc;
  aiop->doneWrite();
  m_writeBytesAIO += rc;
  m_stats.write_aio_ops++;
  BHTRACE("WriteAIO " << rc << " bytes @ " << aiop->sfsAio.aio_offset
                      << " path=" << m_path);
  return XrdOssOK;
}

int XrdBlackholeOssFile::Fsync() {
  return -ENOTSUP;
}

int XrdBlackholeOssFile::Ftruncate(unsigned long long len) {
  return -ENOTSUP;
}
