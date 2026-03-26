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

#ifndef __BLACKHOLE_OSS_HH__
#define __BLACKHOLE_OSS_HH__

#include <string>
#include <sstream>

#include <XrdOss/XrdOss.hh>
#include "XrdSys/XrdSysError.hh"
#include "XrdOuc/XrdOucTrace.hh"

class XrdOucStream;  // used in cfg_* handler signatures; full header in .cc

#include <sys/stat.h>
#include <sys/errno.h>
#include <fcntl.h>

#include "XrdBlackhole/BlackholeFS.hh"
#include "XrdBlackhole/XrdBlackholeStats.hh"

extern BlackholeFS       g_blackholeFS;
extern XrdSysError       XrdBlackholeEroute;
extern XrdOucTrace       XrdBlackholeTrace;

//------------------------------------------------------------------------------
// Trace bits for XrdBlackholeTrace.What
//   Enable with: xrootd.trace all  (or set the bit programmatically)
//------------------------------------------------------------------------------
#define BHTRACE_IO  0x0001  ///< Per-operation I/O tracing (open/read/write/close)

/// Gate a trace message on the BHTRACE_IO bit.  Usage: BHTRACE("key=" << val)
#define BHTRACE(x) \
  if (XrdBlackholeTrace.What & BHTRACE_IO) { \
    std::ostringstream _bh_oss; _bh_oss << x; \
    XrdBlackholeEroute.Say(_bh_oss.str().c_str()); }


//------------------------------------------------------------------------------
//! XrdOss plugin that provides a blackhole storage backend.
//!
//! Writes are accepted and silently discarded — no data is ever persisted to
//! disk. An optional write-speed throttle can be configured to simulate a
//! target storage throughput. The plugin should be loaded via the
//! ofs.osslib directive:
//!
//!   ofs.osslib /path/to/libXrdBlackhole.so
//!
//! Supported configuration directives:
//!   blackhole.writespeedMiBps <N>   Throttle writes to N MiB/s.
//!                                   Set to 0 (the default) for unlimited.
//!   blackhole.defaultspath    <path> Pre-seed the in-memory filesystem with
//!                                   test files of fixed sizes at <path>.
//!   blackhole.readtype        <type> Read pattern for pre-seeded files.
//!                                   Currently only "zeros" is implemented.
//!   blackhole.seedfile <path> <size>[K|M|G|T] [count=N] [type=zeros|random]
//!                                   Create one (or N) pre-seeded file(s).
//!                                   If count>1, path must contain a printf
//!                                   integer format specifier (e.g. %04d).
//------------------------------------------------------------------------------

class XrdBlackholeOss : public XrdOss {
public:
  XrdBlackholeOss(const char *, XrdSysError &);
  virtual ~XrdBlackholeOss();

  int Configure(const char *, XrdSysError &);

  virtual int     Chmod(const char *, mode_t mode, XrdOucEnv *eP=0) override;
  virtual int     Create(const char *, const char *, mode_t, XrdOucEnv &, int opts=0) override;
  virtual int     Init(XrdSysLogger *, const char*) override;
  virtual int     Mkdir(const char *, mode_t mode, int mkpath=0, XrdOucEnv *eP=0) override;
  virtual int     Remdir(const char *, int Opts=0, XrdOucEnv *eP=0) override;
  virtual int     Rename(const char *, const char *, XrdOucEnv *eP1=0, XrdOucEnv *eP2=0) override;
  virtual int     Stat(const char *, struct stat *, int opts=0, XrdOucEnv *eP=0) override;
  virtual int     StatFS(const char *path, char *buff, int &blen, XrdOucEnv *eP=0) override;
  virtual int     StatLS(XrdOucEnv &env, const char *path, char *buff, int &blen) override;
  virtual int     StatVS(XrdOssVSInfo *sP, const char *sname=0, int updt=0) override;
  virtual int     Truncate(const char *, unsigned long long, XrdOucEnv *eP=0) override;
  virtual int     Unlink(const char *path, int Opts=0, XrdOucEnv *eP=0) override;
  virtual XrdOssDF *newDir(const char *tident) override;
  virtual XrdOssDF *newFile(const char *tident) override;

  inline unsigned long writespeedMiBs() const { return m_writespeedMiBs; }

private:
  // ---------------------------------------------------------------------------
  // Configuration state — one member per supported directive.
  // ---------------------------------------------------------------------------
  unsigned long m_writespeedMiBs{0};  ///< 0 = unlimited
  std::string   m_defaultspath{};     ///< Empty = no pre-seeded files
  std::string   m_readtype{"zeros"};  ///< Read pattern for pre-seeded files

  // ---------------------------------------------------------------------------
  // Per-directive handlers — called by Configure() via the dispatch table.
  //
  // Each method reads its value(s) from `cfg`, stores the result in the
  // corresponding member, and returns true on success or false on a fatal
  // parse error (which causes Configure() to return non-zero).
  //
  // To add a new directive:
  //   1. Add a member above for the parsed value.
  //   2. Declare a handler here: bool cfg_<name>(XrdOucStream&, XrdSysError&);
  //   3. Implement it in XrdBlackholeOss.cc following the existing examples.
  //   4. Add one row to the k_directives table in Configure().
  //   5. Add the directive to the logConfig() summary.
  // ---------------------------------------------------------------------------
  bool cfg_writespeedMiBps(XrdOucStream &, XrdSysError &);
  bool cfg_defaultspath   (XrdOucStream &, XrdSysError &);
  bool cfg_readtype       (XrdOucStream &, XrdSysError &);
  bool cfg_seedfile       (XrdOucStream &, XrdSysError &);

  /// Log the effective configuration after parsing completes.
  void logConfig(XrdSysError &) const;
};

#endif /* __BLACKHOLE_OSS_HH__ */
