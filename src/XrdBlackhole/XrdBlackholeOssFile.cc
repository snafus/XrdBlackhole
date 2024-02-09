#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#include "XrdOuc/XrdOucEnv.hh"
#include "XrdSys/XrdSysError.hh"
#include "XrdOuc/XrdOucTrace.hh"
#include "XrdSfs/XrdSfsAio.hh"

#include "XrdBlackhole/XrdBlackholeOssFile.hh"
#include "XrdBlackhole/XrdBlackholeOss.hh"

extern XrdSysError XrdBlackholeEroute;

XrdBlackholeOssFile::XrdBlackholeOssFile(XrdBlackholeOss *bhOss) : m_fd(-1), m_bhOss(bhOss) {}

int XrdBlackholeOssFile::Open(const char *path, int flags, mode_t mode, XrdOucEnv &env) {
  return XrdOssOK;
}

int XrdBlackholeOssFile::Close(long long *retsz) {
  return XrdOssOK;
}

ssize_t XrdBlackholeOssFile::Read(off_t offset, size_t blen) {
  return XrdOssOK;
}

ssize_t XrdBlackholeOssFile::Read(void *buff, off_t offset, size_t blen) {
  return -ENOTSUP;
}

//static void aioReadCallback(XrdSfsAio *aiop, size_t rc) {
//  aiop->Result = rc;
//  aiop->doneRead();
//}

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
  //return -ENOTSUP;
  //
  buf->st_dev = 1;
  buf->st_ino = 1;
  buf->st_mtime = 0;
  buf->st_ctime = 0;
  buf->st_mode = 666 | S_IFREG;
  return XrdOssOK;

}

ssize_t XrdBlackholeOssFile::Write(const void *buff, off_t offset, size_t blen) {
  std::string s = " AIOwrite: " + std::to_string(blen) + " " + std::to_string(offset);
  XrdBlackholeEroute.Say(__FUNCTION__, s.c_str());
  
  return blen;
}

//static void aioWriteCallback(XrdSfsAio *aiop, size_t rc) {
//  aiop->Result = rc;
//  aiop->doneWrite();
//}

int XrdBlackholeOssFile::Write(XrdSfsAio *aiop) {
  
  ssize_t rc = aiop->sfsAio.aio_nbytes;
  off64_t off = aiop->sfsAio.aio_offset;

  aiop->Result = rc; 
  aiop->doneWrite(); 

  std::string s = " AIOwrite: " + std::to_string(rc) + " " + std::to_string(off);
  XrdBlackholeEroute.Say(__FUNCTION__, s.c_str());
  return XrdOssOK;
}

int XrdBlackholeOssFile::Fsync() {
  return -ENOTSUP;
}

int XrdBlackholeOssFile::Ftruncate(unsigned long long len) {
  return -ENOTSUP;
}



