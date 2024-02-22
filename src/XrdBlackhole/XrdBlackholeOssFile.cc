#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#include <chrono>
#include <iostream>
#include <thread>


#include "XrdOuc/XrdOucEnv.hh"
#include "XrdSys/XrdSysError.hh"
#include "XrdOuc/XrdOucTrace.hh"
#include "XrdSfs/XrdSfsAio.hh"

#include "XrdBlackhole/XrdBlackholeOssFile.hh"
#include "XrdBlackhole/XrdBlackholeOss.hh"

extern XrdSysError XrdBlackholeEroute;

using namespace std::chrono_literals;

XrdBlackholeOssFile::XrdBlackholeOssFile(XrdBlackholeOss *bhOss) : m_fd(-1), m_bhOss(bhOss) {}

int XrdBlackholeOssFile::Open(const char *path, int flags, mode_t mode, XrdOucEnv &env) {
  XrdBlackholeEroute.Say(__FILE__,__FUNCTION__);
  XrdBlackholeEroute.Say("Open: ", path, (", " + std::to_string(mode)).c_str() );

  m_path = path;
  g_blackholeFS.open(m_path, flags, mode);
  m_stub = g_blackholeFS.getStub(m_path);
  m_start = std::chrono::high_resolution_clock::now();

  return XrdOssOK;
}

int XrdBlackholeOssFile::Close(long long *retsz) {
  XrdBlackholeEroute.Say(__FILE__,__FUNCTION__);
  if (!g_blackholeFS.exists(m_path)) {
    BUFLOG("Closing file that didn't exist: " << m_path);
    return -ENOENT;
  }

  m_end = std::chrono::high_resolution_clock::now();

  std::chrono::duration<long long, std::micro> duration = 
        std::chrono::duration_cast<std::chrono::microseconds>(m_end - m_start);

  BUFLOG("Close: " << m_path << ", " << duration.count() << ", "
        << "Write: " << m_writeBytes << ", WriteAIO:" << m_writeBytesAIO
  );
  auto stub = g_blackholeFS.getStub(m_path);
  stub->m_size = m_writeBytes + m_writeBytesAIO;
  stub->m_stat.st_size = stub->m_size;

  g_blackholeFS.close(m_path);
  m_stub = nullptr;
  return XrdOssOK;
}

ssize_t XrdBlackholeOssFile::Read(off_t offset, size_t blen) {
  XrdBlackholeEroute.Say(__FILE__,__FUNCTION__);
  if (!m_stub) { 
      XrdBlackholeEroute.Say("No stub file");
    return -EINVAL; 
  }

  return XrdOssOK;
}

ssize_t XrdBlackholeOssFile::Read(void *buff, off_t offset, size_t blen) {
  if (!m_stub) { 
      XrdBlackholeEroute.Say(__FILE__,__FUNCTION__, "bob");
      XrdBlackholeEroute.Say("No stub file");
    return -EINVAL; 
  }
  if (m_size == 0) {
    auto stub = g_blackholeFS.getStub(m_path);
    m_size = stub->m_size;
  }

  size_t bytesremaining = min(blen, m_size - offset);
  //#FIXME; optimise
  // for (size_t i =0; i<bytesremaining; ++i) {
  //   ((char*)buff)[i] = 0;
  // }
  return bytesremaining;

}

//static void aioReadCallback(XrdSfsAio *aiop, size_t rc) {
//  aiop->Result = rc;
//  aiop->doneRead();
//}

int XrdBlackholeOssFile::Read(XrdSfsAio *aiop) {
      XrdBlackholeEroute.Say(__FILE__,__FUNCTION__, "aio");
  if (!m_stub) { 
      XrdBlackholeEroute.Say("No stub file");
    return -EINVAL; 
  }
  return -ENOTSUP;
}

ssize_t XrdBlackholeOssFile::ReadRaw(void *buff, off_t offset, size_t blen) {
  return Read(buff, offset, blen);
}

ssize_t XrdBlackholeOssFile::ReadV(XrdOucIOVec *readV, int n) {
  return -ENOTSUP;
}


int XrdBlackholeOssFile::Fstat(struct stat *buf) {
  XrdBlackholeEroute.Say(__FILE__,__FUNCTION__);
  if (!g_blackholeFS.exists(m_path)) {
      XrdBlackholeEroute.Say("Path not found");
      return -ENOENT;
  }

  //return -ENOTSUP;
    std::string pointerString = std::to_string(reinterpret_cast<uintptr_t>(g_blackholeFS.getStub(m_path)));
  *buf = g_blackholeFS.getStub(m_path)->m_stat;

  buf->st_mode = 0666 | S_IFREG;
  return XrdOssOK;

}

ssize_t XrdBlackholeOssFile::Write(const void *buff, off_t offset, size_t blen) {
  // std::string s = " AIOwrite: " + std::to_string(blen) + " " + std::to_string(offset);
  // XrdBlackholeEroute.Say(__FILE__,__FUNCTION__, s.c_str());
  if (m_bhOss->writespeedMiBs() > 0) {
    int delay = blen / (m_bhOss->writespeedMiBs() *1024*1024) * 1000;
    std::this_thread::sleep_for(delay*1ms);
  }
  m_writeBytes += blen;
  return blen;
}

//static void aioWriteCallback(XrdSfsAio *aiop, size_t rc) {
//  aiop->Result = rc;
//  aiop->doneWrite();
//}

int XrdBlackholeOssFile::Write(XrdSfsAio *aiop) {
  
  ssize_t rc = aiop->sfsAio.aio_nbytes;
  off64_t off = aiop->sfsAio.aio_offset;
  if (m_bhOss->writespeedMiBs() > 0) {
    int delay = rc / (m_bhOss->writespeedMiBs() *1024*1024) * 1000;
    std::this_thread::sleep_for(delay*1ms);
  }

  aiop->Result = rc; 
  aiop->doneWrite(); 

  m_writeBytesAIO += rc;

  std::string s = " AIOwrite: " + std::to_string(rc) + " " + std::to_string(off);
  XrdBlackholeEroute.Say(__FUNCTION__, s.c_str());
  return XrdOssOK;
}

int XrdBlackholeOssFile::Fsync() {
    XrdBlackholeEroute.Say(__FILE__,__FUNCTION__);
  return -ENOTSUP;
}

int XrdBlackholeOssFile::Ftruncate(unsigned long long len) {
    XrdBlackholeEroute.Say(__FILE__,__FUNCTION__);
  return -ENOTSUP;
}



