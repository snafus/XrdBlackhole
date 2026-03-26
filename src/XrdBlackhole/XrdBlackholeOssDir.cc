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

#include <string.h>

#include "XrdBlackhole/XrdBlackholeOssDir.hh"
#include "XrdSys/XrdSysError.hh"
#include "XrdOuc/XrdOucTrace.hh"

extern XrdSysError XrdBlackholeEroute;

XrdBlackholeOssDir::XrdBlackholeOssDir(XrdBlackholeOss *bhOss) : m_bhOss(bhOss) {}

int XrdBlackholeOssDir::Opendir(const char *path, XrdOucEnv &env) {
  m_prefix = path;
  m_entries.clear();
  m_pos = 0;
  g_blackholeFS.readdir(m_prefix, m_entries);
  BHTRACE("Opendir path=" << path << " entries=" << m_entries.size());
  return XrdOssOK;
}

int XrdBlackholeOssDir::Close(long long *retsz) {
  m_entries.clear();
  m_pos = 0;
  return XrdOssOK;
}

int XrdBlackholeOssDir::Readdir(char *buff, int blen) {
  if (m_pos >= m_entries.size()) {
    // Signal end-of-directory with an empty string.
    buff[0] = '\0';
    return XrdOssOK;
  }
  const std::string& name = m_entries[m_pos++];
  if (static_cast<int>(name.size()) >= blen) {
    XrdBlackholeEroute.Emsg("Readdir", ENAMETOOLONG, "entry too long for buffer",
                            name.c_str());
    return -ENAMETOOLONG;
  }
  memcpy(buff, name.c_str(), name.size() + 1);
  return XrdOssOK;
}
