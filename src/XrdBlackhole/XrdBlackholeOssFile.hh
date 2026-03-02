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
#include "XrdBlackhole/XrdBlackholeStats.hh"

#include <atomic>
#include <chrono>
#include <memory>

//------------------------------------------------------------------------------
//! XrdOssDF implementation for the blackhole storage backend.
//!
//! Writes are discarded after optionally sleeping to simulate a configured
//! write speed. The total bytes written are tracked so that the file appears
//! with the correct size after close.
//!
//! Reads return a zero-filled buffer up to the file's registered size.
//! AIO reads and vectored reads are not supported.
//!
//! Per-transfer statistics are accumulated during the Open→Close lifecycle
//! and submitted to XrdBlackholeStatsManager on close.
//------------------------------------------------------------------------------

class XrdBlackholeOssFile : public XrdOssDF {

public:

  XrdBlackholeOssFile(XrdBlackholeOss *bhoss);
  virtual ~XrdBlackholeOssFile() = default;
  virtual int     Open(const char *path, int flags, mode_t mode, XrdOucEnv &env) override;
  virtual int     Close(long long *retsz=0) override;
  virtual ssize_t Read(off_t offset, size_t blen) override;
  virtual ssize_t Read(void *buff, off_t offset, size_t blen) override;
  virtual int     Read(XrdSfsAio *aoip) override;
  virtual ssize_t ReadV(XrdOucIOVec *readV, int n) override;
  virtual ssize_t ReadRaw(void *, off_t, size_t) override;
  virtual int     Fstat(struct stat *buff) override;
  virtual ssize_t Write(const void *buff, off_t offset, size_t blen) override;
  virtual int     Write(XrdSfsAio *aiop) override;
  virtual int     Fsync(void) override;
  virtual int     Ftruncate(unsigned long long) override;

private:

  // NOTE: XRootD serialises all I/O operations on a single file handle —
  // Close() is always called after all reads and writes have completed.
  // Per-handle members therefore do not require additional locking beyond
  // what the atomic counters below already provide.
  std::shared_ptr<Stub> m_stub;
  std::string  m_path;
  size_t       m_size {0};   ///< Cached stub size; set once in Open(), read-only thereafter
  XrdBlackholeOss *m_bhOss;

  // Timing — high-resolution clock for throttle / duration calculations.
  std::chrono::high_resolution_clock::time_point m_start;
  std::chrono::high_resolution_clock::time_point m_end;

  // Atomic byte/op counters — updated on every I/O call; may be called from
  // concurrent threads on the same handle.  Read into TransferStats at Close().
  std::atomic<ssize_t>   m_writeBytes{0};
  std::atomic<ssize_t>   m_writeBytesAIO{0};
  std::atomic<ssize_t>   m_readBytes{0};
  std::atomic<uint32_t>  m_writeOps{0};
  std::atomic<uint32_t>  m_writeAioOps{0};
  std::atomic<uint32_t>  m_readOps{0};
  std::atomic<uint32_t>  m_errors{0};

  // Per-transfer statistics submitted to g_statsManager on close.
  TransferStats m_stats;
};

#endif /* __XRD_BLACKHOLE_OSS_FILE_HH__ */
