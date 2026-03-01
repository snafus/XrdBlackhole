//------------------------------------------------------------------------------
// Copyright (c) 2014-2015 by European Organization for Nuclear Research (CERN)
// Author: Sebastien Ponce <sebastien.ponce@cern.ch>
//------------------------------------------------------------------------------
// This file is part of the XRootD software suite.
//
// XRootD is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// XRootD is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with XRootD.  If not, see <http://www.gnu.org/licenses/>.
//
// In applying this licence, CERN does not waive the privileges and immunities
// granted to it by virtue of its status as an Intergovernmental Organization
// or submit itself to any jurisdiction.
//------------------------------------------------------------------------------

#ifndef __XRD_BLACKHOLE_OSS_FILE_HH__
#define __XRD_BLACKHOLE_OSS_FILE_HH__

#include "XrdOss/XrdOss.hh"
#include "XrdBlackhole/XrdBlackholeOss.hh"

#include <chrono>


//------------------------------------------------------------------------------
//! XrdOssDF implementation for the blackhole storage backend.
//!
//! Writes are discarded after optionally sleeping to simulate a configured
//! write speed. The total bytes written are tracked so that the file appears
//! with the correct size after close.
//!
//! Reads return a zero-filled buffer up to the file's registered size.
//! AIO reads and vectored reads are not supported.
//------------------------------------------------------------------------------

class XrdBlackholeOssFile : public XrdOssDF {

public:

  XrdBlackholeOssFile(XrdBlackholeOss *cephoss);
  virtual ~XrdBlackholeOssFile() {};
  virtual int Open(const char *path, int flags, mode_t mode, XrdOucEnv &env);
  virtual int Close(long long *retsz=0);
  virtual ssize_t Read(off_t offset, size_t blen);
  virtual ssize_t Read(void *buff, off_t offset, size_t blen);
  virtual int     Read(XrdSfsAio *aoip);
  virtual ssize_t ReadV(XrdOucIOVec *readV, int n);
  virtual ssize_t ReadRaw(void *, off_t, size_t);
  virtual int Fstat(struct stat *buff);
  virtual ssize_t Write(const void *buff, off_t offset, size_t blen);
  virtual int Write(XrdSfsAio *aiop);
  virtual int Fsync(void);
  virtual int Ftruncate(unsigned long long);

private:

  int m_fd;
  Stub * m_stub {nullptr};
  std::string m_path;
  size_t m_size {0};
  XrdBlackholeOss *m_bhOss;

  std::chrono::high_resolution_clock::time_point m_start;
  std::chrono::high_resolution_clock::time_point m_end;

  ssize_t m_writeBytesAIO{0};
  ssize_t m_writeBytes{0};

};

#endif /* __XRD_BLACKHOLE_OSS_FILE_HH__ */
