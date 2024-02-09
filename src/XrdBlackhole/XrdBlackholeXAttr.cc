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

#include "XrdVersion.hh"
#include "XrdSys/XrdSysError.hh"
#include "XrdOuc/XrdOucTrace.hh"
#include "XrdBlackhole/XrdBlackholeXAttr.hh"

XrdSysError XrdBlackholeXattrEroute(0);
XrdOucTrace XrdBlackholeXattrTrace(&XrdBlackholeXattrEroute);

extern "C"
{
  XrdSysXAttr*
  XrdSysGetXAttrObject(XrdSysError  *errP,
                       const char   *config_fn,
                       const char   *parms)
  {
    // Do the herald thing
    XrdBlackholeXattrEroute.SetPrefix("cephxattr_");
    XrdBlackholeXattrEroute.logger(errP->logger());
    XrdBlackholeXattrEroute.Say("++++++ CERN/IT-DSS XrdBlackholeXattr");
    return new XrdBlackholeXAttr();
  }
}

XrdBlackholeXAttr::XrdBlackholeXAttr() {}

XrdBlackholeXAttr::~XrdBlackholeXAttr() {}

int XrdBlackholeXAttr::Del(const char *Aname, const char *Path, int fd) {
  return -ENOTSUP;
}

void XrdBlackholeXAttr::Free(AList *aPL) {
  //ceph_posix_freexattrlist(aPL);
}

int XrdBlackholeXAttr::Get(const char *Aname, void *Aval, int Avsz,
                   const char *Path,  int fd) {
  return -ENOTSUP;
}

int XrdBlackholeXAttr::List(AList **aPL, const char *Path, int fd, int getSz) {
  return -ENOTSUP;
}

int XrdBlackholeXAttr::Set(const char *Aname, const void *Aval, int Avsz,
                      const char *Path,  int fd,  int isNew) {
  return -ENOTSUP;
}

XrdVERSIONINFO(XrdSysGetXAttrObject, XrdBlackholeXAttr);

