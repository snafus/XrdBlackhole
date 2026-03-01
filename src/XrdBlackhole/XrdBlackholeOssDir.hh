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

#include "XrdOss/XrdOss.hh"
#include "XrdBlackhole/XrdBlackholeOss.hh"

//------------------------------------------------------------------------------
//! XrdOssDF directory handle for the blackhole storage backend.
//!
//! Directory listing is not supported: the blackhole has no persistent
//! namespace, so Opendir and Readdir always return -ENOTSUP.
//------------------------------------------------------------------------------

class XrdBlackholeOssDir : public XrdOssDF {

public:

  XrdBlackholeOssDir(XrdBlackholeOss *bhoss);
  virtual ~XrdBlackholeOssDir() {};
  virtual int Opendir(const char *, XrdOucEnv &);
  virtual int Readdir(char *buff, int blen);
  virtual int Close(long long *retsz=0);

private:

  DIR *m_dirp;
  XrdBlackholeOss *m_bhOss = nullptr;

};

#endif /* __XRD_BLACKHOLE_OSS_DIR_HH__ */
