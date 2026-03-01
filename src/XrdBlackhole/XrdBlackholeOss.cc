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

#include <stdio.h>
#include <string>
#include <fcntl.h>
#include <limits.h>
#include "XrdOuc/XrdOucEnv.hh"
#include "XrdSys/XrdSysError.hh"
#include "XrdOuc/XrdOucTrace.hh"
#include "XrdOuc/XrdOucStream.hh"
#include "XrdVersion.hh"
#include "XrdBlackhole/XrdBlackholeOss.hh"
#include "XrdBlackhole/XrdBlackholeOssDir.hh"
#include "XrdBlackhole/XrdBlackholeOssFile.hh"

XrdVERSIONINFO(XrdOssGetStorageSystem, XrdBlackholeOss);

XrdSysError XrdBlackholeEroute(0);
XrdOucTrace XrdBlackholeTrace(&XrdBlackholeEroute);

std::mutex g_buflog_mutex;

BlackholeFS g_blackholeFS;

extern "C"
{
  XrdOss*
  XrdOssGetStorageSystem(XrdOss* native_oss,
                         XrdSysLogger* lp,
                         const char* config_fn,
                         const char* parms)
  {
    XrdBlackholeEroute.SetPrefix("blackhole_");
    XrdBlackholeEroute.logger(lp);
    XrdBlackholeEroute.Say("++++++ CERN/IT-DSS XrdBlackhole");
    return new XrdBlackholeOss(config_fn, XrdBlackholeEroute);
  }
}

XrdBlackholeOss::XrdBlackholeOss(const char *configfn, XrdSysError &Eroute) {
  Configure(configfn, Eroute);
}

XrdBlackholeOss::~XrdBlackholeOss() {
}

int XrdBlackholeOss::Configure(const char *configfn, XrdSysError &Eroute) {
   int NoGo = 0;
   XrdOucEnv myEnv;
   XrdOucStream Config(&Eroute, getenv("XRDINSTANCE"), &myEnv, "=====> ");
   // Disable POSC (Persist-On-Successful-Close): nothing is ever persisted.
   XrdOucEnv::Export("XRDXROOTD_NOPOSC", "1");
   if (configfn && *configfn) {
     int cfgFD;
     if ((cfgFD = open(configfn, O_RDONLY, 0)) < 0) {
       Eroute.Emsg("Config", errno, "open config file", configfn);
       return 1;
     }
     Config.Attach(cfgFD);
     char *var;
     while((var = Config.GetMyFirstWord())) {
        if (!strncmp(var, "blackhole.writespeedMiBps", 25)) {
         var = Config.GetWord();
         if (var) {
           unsigned long value = strtoul(var, 0, 10);
           if ((value > 0) && (value < INT_MAX)){
             m_writespeedMiBs = value;
           } else {
             Eroute.Emsg("Config", "Invalid value for blackhole.writespeedMiBps:", var);
           }
         } else {
           Eroute.Emsg("Config", "Missing value for blackhole.writespeedMiBps in config file");
           return 1;
         }
       }

        if (!strncmp(var, "blackhole.defaultspath", 22)) {
         var = Config.GetWord();
         Eroute.Emsg("Config", "create_defaults", var);
         if (var) {
           char parms[1040];
           if (!Config.GetRest(parms, sizeof(parms)) || parms[0]) {
             Eroute.Emsg("Config", "defaultspath parameters will be ignored");
           }
           m_defaultspath = var;
           g_blackholeFS.create_defaults(m_defaultspath);
         }
        }
     }
     int retc = Config.LastError();
     if (retc) {
       NoGo = Eroute.Emsg("Config", -retc, "read config file", configfn);
     }
     Config.Close();
   }
   return NoGo;
}

int XrdBlackholeOss::Chmod(const char *path, mode_t mode, XrdOucEnv *envP) {
  return -ENOTSUP;
}

int XrdBlackholeOss::Create(const char *tident, const char *path, mode_t access_mode,
                    XrdOucEnv &env, int Opts) {
  return -ENOTSUP;
}

int XrdBlackholeOss::Init(XrdSysLogger *logger, const char* configFn) { return 0; }

// Silently succeed for directory operations: POSIX-assuming clients (notably
// GFAL2) require these to not fail even though the blackhole has no real
// directory hierarchy.
int XrdBlackholeOss::Mkdir(const char *path, mode_t mode, int mkpath, XrdOucEnv *envP) {
  return 0;
}

int XrdBlackholeOss::Remdir(const char *path, int Opts, XrdOucEnv *eP) {
  return 0;
}

int XrdBlackholeOss::Rename(const char *from,
                    const char *to,
                    XrdOucEnv *eP1,
                    XrdOucEnv *eP2) {
  return -ENOTSUP;
}

int XrdBlackholeOss::Stat(const char* path,
                  struct stat* buff,
                  int opts,
                  XrdOucEnv* env) {
  XrdBlackholeEroute.Say(__FUNCTION__, " path = ", path);
  if (g_blackholeFS.exists(path)) {
    auto stub = g_blackholeFS.getStub(path);
    *buff = stub->m_stat;
    XrdBlackholeEroute.Say(__FUNCTION__, " Stat response ",
            std::to_string(stub->m_size).c_str());
  } else {
    return -ENOENT;
  }
  return 0;
}

int XrdBlackholeOss::StatFS(const char *path, char *buff, int &blen, XrdOucEnv *eP) {
  XrdOssVSInfo sP{};
  int percentUsedSpace = 0;
  blen = snprintf(buff, blen, "%d %lld %d %d %lld %d",
                  1, sP.Free, percentUsedSpace, 0, 0LL, 0);
  return XrdOssOK;
}

int XrdBlackholeOss::StatVS(XrdOssVSInfo *sP, const char *sname, int updt) {
  sP->Large = sP->Total;
  sP->LFree = sP->Free;
  sP->Usage = sP->Total - sP->Free;
  sP->Extents = 1;
  return XrdOssOK;
}

int formatStatLSResponse(char *buff, int &blen, const char* cgroup, long long totalSpace,
  long long usedSpace, long long freeSpace, long long quota, long long maxFreeChunk)
{
  return snprintf(buff, blen, "oss.cgroup=%s&oss.space=%lld&oss.free=%lld&oss.maxf=%lld&oss.used=%lld&oss.quota=%lld",
                                     cgroup,       totalSpace,    freeSpace,    maxFreeChunk, usedSpace,    quota);
}

int XrdBlackholeOss::StatLS(XrdOucEnv &env, const char *path, char *buff, int &blen)
{
  long long usedSpace = 0;
  long long totalSpace = 0;
  long long freeSpace = totalSpace - usedSpace;
  blen = formatStatLSResponse(buff, blen,
    path,
    totalSpace,
    usedSpace,
    freeSpace,
    totalSpace,
    freeSpace);
  return XrdOssOK;
}

int XrdBlackholeOss::Truncate(const char* path,
                          unsigned long long size,
                          XrdOucEnv* env) {
  return -ENOTSUP;
}

int XrdBlackholeOss::Unlink(const char *path, int Opts, XrdOucEnv *env) {
  return g_blackholeFS.unlink(path);
}

XrdOssDF* XrdBlackholeOss::newDir(const char *tident) {
  return new XrdBlackholeOssDir(this);
}

XrdOssDF* XrdBlackholeOss::newFile(const char *tident) {
  XrdBlackholeEroute.Say("newFile: ", tident);
  return new XrdBlackholeOssFile(this);
}
