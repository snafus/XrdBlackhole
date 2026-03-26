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
#include <cerrno>
#include <cstring>
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
  // -------------------------------------------------------------------------
  // Directive dispatch table.
  //
  // Each row maps a config keyword to the handler method that parses it.
  // Defined inside the member function so that pointers to private methods
  // are accessible. To add a new directive, append one row here and implement
  // the corresponding cfg_<name>() method.
  // -------------------------------------------------------------------------
  struct Directive {
    const char *name;
    bool (XrdBlackholeOss::*parse)(XrdOucStream &, XrdSysError &);
  };
  static const Directive k_directives[] = {
    { "blackhole.writespeedMiBps", &XrdBlackholeOss::cfg_writespeedMiBps },
    { "blackhole.defaultspath",    &XrdBlackholeOss::cfg_defaultspath    },
    { "blackhole.readtype",        &XrdBlackholeOss::cfg_readtype        },
    { "blackhole.seedfile",        &XrdBlackholeOss::cfg_seedfile        },
  };
  static const int k_nDirectives = sizeof(k_directives) / sizeof(k_directives[0]);
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

    const char *var;
    while ((var = Config.GetMyFirstWord())) {
      // Walk the dispatch table looking for a matching directive name.
      bool matched = false;
      for (int i = 0; i < k_nDirectives; ++i) {
        if (!strcmp(var, k_directives[i].name)) {
          if (!(this->*k_directives[i].parse)(Config, Eroute)) NoGo = 1;
          matched = true;
          break;
        }
      }
      // Warn on unrecognised blackhole.* tokens; ignore all other namespaces.
      if (!matched && !strncmp(var, "blackhole.", 10))
        Eroute.Emsg("Config", "Unknown directive (ignored):", var);
    }

    int retc = Config.LastError();
    if (retc) NoGo = Eroute.Emsg("Config", -retc, "read config file", configfn);
    Config.Close();
  }

  logConfig(Eroute);
  return NoGo;
}

// ---------------------------------------------------------------------------
// Directive handlers
// ---------------------------------------------------------------------------

bool XrdBlackholeOss::cfg_writespeedMiBps(XrdOucStream &cfg, XrdSysError &Eroute) {
  const char *val = cfg.GetWord();
  if (!val) {
    Eroute.Emsg("Config", "blackhole.writespeedMiBps: missing value");
    return false;
  }
  char *endp = nullptr;
  errno = 0;
  unsigned long value = strtoul(val, &endp, 10);
  if (endp == val || *endp != '\0') {
    Eroute.Emsg("Config", "blackhole.writespeedMiBps: non-numeric value:", val);
    return false;
  }
  if (errno == ERANGE || value >= static_cast<unsigned long>(INT_MAX)) {
    Eroute.Emsg("Config", "blackhole.writespeedMiBps: value too large:", val);
    return false;
  }
  m_writespeedMiBs = value;  // 0 = unlimited
  return true;
}

bool XrdBlackholeOss::cfg_defaultspath(XrdOucStream &cfg, XrdSysError &Eroute) {
  const char *val = cfg.GetWord();
  if (!val) {
    Eroute.Emsg("Config", "blackhole.defaultspath: missing value");
    return false;
  }
  char rest[1040];
  if (!cfg.GetRest(rest, sizeof(rest)) || rest[0])
    Eroute.Emsg("Config", "blackhole.defaultspath: extra tokens ignored");
  m_defaultspath = val;
  g_blackholeFS.create_defaults(m_defaultspath);
  return true;
}

bool XrdBlackholeOss::cfg_readtype(XrdOucStream &cfg, XrdSysError &Eroute) {
  const char *val = cfg.GetWord();
  if (!val) {
    Eroute.Emsg("Config", "blackhole.readtype: missing value");
    return false;
  }
  if (strcmp(val, "zeros") != 0) {
    Eroute.Emsg("Config", "blackhole.readtype: unknown type (only 'zeros' supported):", val);
    return false;
  }
  m_readtype = val;
  return true;
}

bool XrdBlackholeOss::cfg_seedfile(XrdOucStream &cfg, XrdSysError &Eroute) {
  // blackhole.seedfile <path> <size>[K|M|G|T] [count=N] [type=zeros|random]
  const char *pathval = cfg.GetWord();
  if (!pathval) {
    Eroute.Emsg("Config", "blackhole.seedfile: missing path");
    return false;
  }
  std::string path = pathval;

  const char *sizeval = cfg.GetWord();
  if (!sizeval) {
    Eroute.Emsg("Config", "blackhole.seedfile: missing size");
    return false;
  }

  // Parse size with optional K/M/G/T suffix (powers of 1024).
  char *endp = nullptr;
  errno = 0;
  unsigned long long size = strtoull(sizeval, &endp, 10);
  if (endp == sizeval || errno == ERANGE) {
    Eroute.Emsg("Config", "blackhole.seedfile: invalid size:", sizeval);
    return false;
  }
  if (*endp != '\0') {
    switch (*endp | 0x20) {  // tolower
      case 'k': size *= 1024ULL;                   break;
      case 'm': size *= 1024ULL * 1024;             break;
      case 'g': size *= 1024ULL * 1024 * 1024;      break;
      case 't': size *= 1024ULL * 1024 * 1024 * 1024; break;
      default:
        Eroute.Emsg("Config", "blackhole.seedfile: unknown size suffix:", endp);
        return false;
    }
  }

  // Parse optional keyword arguments.
  unsigned long long count = 1;
  std::string readtype = "zeros";
  const char *tok;
  while ((tok = cfg.GetWord()) != nullptr) {
    if (!strncmp(tok, "count=", 6)) {
      char *ep = nullptr;
      errno = 0;
      count = strtoull(tok + 6, &ep, 10);
      if (ep == tok + 6 || *ep != '\0' || errno == ERANGE || count == 0) {
        Eroute.Emsg("Config", "blackhole.seedfile: invalid count=", tok + 6);
        return false;
      }
    } else if (!strncmp(tok, "type=", 5)) {
      readtype = tok + 5;
      if (readtype != "zeros" && readtype != "random") {
        Eroute.Emsg("Config", "blackhole.seedfile: unknown type (zeros|random):",
                    readtype.c_str());
        return false;
      }
    } else {
      Eroute.Emsg("Config", "blackhole.seedfile: unknown option (ignored):", tok);
    }
  }

  // Validate: count>1 requires a printf integer format specifier in the path.
  if (count > 1 && path.find('%') == std::string::npos) {
    Eroute.Emsg("Config",
      "blackhole.seedfile: count>1 requires a printf format specifier in path",
      path.c_str());
    return false;
  }

  // Create the stubs.
  if (count == 1) {
    g_blackholeFS.seed(path, size, readtype);
  } else {
    char buf[4096];
    for (unsigned long long i = 0; i < count; i++) {
      if (snprintf(buf, sizeof(buf), path.c_str(),
                   static_cast<unsigned long long>(i)) >= static_cast<int>(sizeof(buf))) {
        Eroute.Emsg("Config", "blackhole.seedfile: generated path too long");
        return false;
      }
      g_blackholeFS.seed(buf, size, readtype);
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// Configuration summary
// ---------------------------------------------------------------------------

void XrdBlackholeOss::logConfig(XrdSysError &Eroute) const {
  std::ostringstream oss;
  oss << "Configured:"
      << " writespeedMiBps=" << (m_writespeedMiBs == 0 ? "unlimited"
                                                        : std::to_string(m_writespeedMiBs))
      << " defaultspath="    << (m_defaultspath.empty() ? "(none)" : m_defaultspath)
      << " readtype="        << m_readtype;
  Eroute.Say(oss.str().c_str());
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
  int rc = g_blackholeFS.rename(from, to);
  if (rc) XrdBlackholeEroute.Emsg("Rename", -rc, "rename", from);
  BHTRACE("Rename from=" << from << " to=" << to << " rc=" << rc);
  return rc;
}

int XrdBlackholeOss::Stat(const char* path,
                  struct stat* buff,
                  int opts,
                  XrdOucEnv* env) {
  BHTRACE("Stat path=" << path);
  auto stub = g_blackholeFS.getStub(path);
  if (!stub) return -ENOENT;
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
  // Report effectively unlimited free space so that space-token-aware clients
  // (FTS3, GFAL2) do not reject the endpoint as full.  1 PiB is large enough
  // for any realistic benchmark while fitting safely in a long long.
  static const long long kReportedSpace = 1LL * 1024 * 1024 * 1024 * 1024 * 1024; // 1 PiB
  long long usedSpace = 0;
  long long freeSpace = kReportedSpace;
  blen = formatStatLSResponse(buff, blen,
    path, kReportedSpace, usedSpace, freeSpace, kReportedSpace, freeSpace);
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
