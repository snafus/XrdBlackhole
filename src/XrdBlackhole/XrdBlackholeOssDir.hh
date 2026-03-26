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

#ifndef __XRD_BLACKHOLE_OSS_DIR_HH__
#define __XRD_BLACKHOLE_OSS_DIR_HH__

#include <string>
#include <vector>

#include "XrdOss/XrdOss.hh"
#include "XrdBlackhole/XrdBlackholeOss.hh"

//------------------------------------------------------------------------------
//! XrdOssDF directory handle for the blackhole storage backend.
//!
//! Opendir() snapshots the direct children of the requested path from
//! BlackholeFS into m_entries.  Readdir() iterates that snapshot, copying
//! one entry name per call and signalling end-of-directory with an empty
//! buffer.
//------------------------------------------------------------------------------

class XrdBlackholeOssDir : public XrdOssDF {

public:

  XrdBlackholeOssDir(XrdBlackholeOss *bhoss);
  virtual ~XrdBlackholeOssDir() = default;
  virtual int Opendir(const char *, XrdOucEnv &) override;
  virtual int Readdir(char *buff, int blen) override;
  virtual int Close(long long *retsz=0) override;

private:

  XrdBlackholeOss          *m_bhOss   = nullptr;
  std::string               m_prefix;
  std::vector<std::string>  m_entries;
  size_t                    m_pos{0};

};

#endif /* __XRD_BLACKHOLE_OSS_DIR_HH__ */
