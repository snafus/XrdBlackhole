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

#ifndef __XRD_BLACKHOLE_XATTR_HH__
#define __XRD_BLACKHOLE_XATTR_HH__

#include <XrdSys/XrdSysXAttr.hh>

//------------------------------------------------------------------------------
//! XrdSysXAttr implementation for the blackhole storage backend.
//!
//! Extended attributes are not supported. All operations return -ENOTSUP.
//! Load via the ofs.xattrlib directive if an xattr plugin slot is required.
//------------------------------------------------------------------------------

class XrdBlackholeXAttr : public XrdSysXAttr {

public:

  XrdBlackholeXAttr();
  virtual ~XrdBlackholeXAttr();

  virtual int Del(const char *Aname, const char *Path, int fd=-1);
  virtual void Free(AList *aPL);
  virtual int Get(const char *Aname, void *Aval, int Avsz,
                  const char *Path,  int fd=-1);
  virtual int List(AList **aPL, const char *Path, int fd=-1, int getSz=0);
  virtual int Set(const char *Aname, const void *Aval, int Avsz,
                  const char *Path,  int fd=-1,  int isNew=0);

};

#endif /* __XRD_BLACKHOLE_XATTR_HH__ */
