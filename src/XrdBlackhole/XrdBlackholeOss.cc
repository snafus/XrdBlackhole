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
#include <sstream>
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
  g_statsManager.logSummary();
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
    while ((var = Config.GetMyFirstWord())) {

      if (!strcmp(var, "blackhole.writespeedMiBps")) {
        var = Config.GetWord();
        if (!var) {
          Eroute.Emsg("Config", "Missing value for blackhole.writespeedMiBps");
          return 1;
        }
        char *endp = nullptr;
        unsigned long value = strtoul(var, &endp, 10);
        if (endp == var || *endp != '\0') {
          Eroute.Emsg("Config", "Non-numeric value for blackhole.writespeedMiBps:", var);
        } else if (value >= static_cast<unsigned long>(INT_MAX)) {
          Eroute.Emsg("Config", "Value too large for blackhole.writespeedMiBps:", var);
        } else {
          m_writespeedMiBs = value;  // 0 means unlimited
        }

      } else if (!strcmp(var, "blackhole.defaultspath")) {
        var = Config.GetWord();
        if (!var) {
          Eroute.Emsg("Config", "Missing value for blackhole.defaultspath");
          return 1;
        }
        char rest[1040];
        if (!Config.GetRest(rest, sizeof(rest)) || rest[0]) {
          Eroute.Emsg("Config", "blackhole.defaultspath: extra tokens will be ignored");
        }
        m_defaultspath = var;
        g_blackholeFS.create_defaults(m_defaultspath);

      } else if (!strcmp(var, "blackhole.readtype")) {
        var = Config.GetWord();
        if (!var) {
          Eroute.Emsg("Config", "Missing value for blackhole.readtype");
          return 1;
        }
        if (strcmp(var, "zeros") != 0) {
          Eroute.Emsg("Config", "Unknown readtype (only 'zeros' is supported):", var);
        } else {
          m_readtype = var;
        }

      } else if (!strncmp(var, "blackhole.", 10)) {
        // Catch-all for unrecognised blackhole.* directives.
        Eroute.Emsg("Config", "Unknown directive (ignored):", var);
      }
    }

    int retc = Config.LastError();
    if (retc) {
      NoGo = Eroute.Emsg("Config", -retc, "read config file", configfn);
    }
    Config.Close();
  }

  // Log effective configuration so operators can verify what was parsed.
  std::ostringstream summary;
  summary << "Configured:"
          << " writespeedMiBps=" << (m_writespeedMiBs == 0 ? "unlimited"
                                                            : std::to_string(m_writespeedMiBs))
          << " defaultspath="    << (m_defaultspath.empty() ? "(none)" : m_defaultspath)
          << " readtype="        << m_readtype;
  Eroute.Say(summary.str().c_str());

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
  BHTRACE("Stat path=" << path);
  if (!g_blackholeFS.exists(path)) return -ENOENT;
  auto stub = g_blackholeFS.getStub(path);
  *buff = stub->m_stat;
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
    path, totalSpace, usedSpace, freeSpace, totalSpace, freeSpace);
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
  BHTRACE("newFile tident=" << tident);
  return new XrdBlackholeOssFile(this);
}
