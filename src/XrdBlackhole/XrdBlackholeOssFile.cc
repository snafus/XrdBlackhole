nclude <sys/types.h>
#include <unistd.h>

#include "XrdOuc/XrdOucEnv.hh"
#include "XrdSys/XrdSysError.hh"
#include "XrdOuc/XrdOucTrace.hh"
#include "XrdSfs/XrdSfsAio.hh"

#include "XrdCeph/XrdBlackholeOssFile.hh"
#include "XrdCeph/XrdBlackholeOss.hh"

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

static void aioReadCallback(XrdSfsAio *aiop, size_t rc) {
  aiop->Result = rc;
  aiop->doneRead();
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


int XrdBlackholeOssFile::Fstat(struct stat *buff) {
  return -ENOTSUP;
}

ssize_t XrdBlackholeOssFile::Write(const void *buff, off_t offset, size_t blen) {
  return -ENOTSUP;
}

static void aioWriteCallback(XrdSfsAio *aiop, size_t rc) {
  aiop->Result = rc;
  aiop->doneWrite();
}

int XrdBlackholeOssFile::Write(XrdSfsAio *aiop) {
  return -ENOTSUP;
}

int XrdBlackholeOssFile::Fsync() {
  return -ENOTSUP;
}

int XrdBlackholeOssFile::Ftruncate(unsigned long long len) {
  return -ENOTSUP;
}



