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
#include <chrono>
#include "XrdOuc/XrdOucEnv.hh"
#include "XrdSys/XrdSysError.hh"
#include "XrdOuc/XrdOucTrace.hh"
#include "XrdOuc/XrdOucStream.hh"
#include "XrdOuc/XrdOucName2Name.hh"
#include "XrdVersion.hh"
#include "XrdBlackhole/XrdBlackholeOss.hh"
#include "XrdBlackhole/XrdBlackholeOssDir.hh"
#include "XrdBlackhole/XrdBlackholeOssFile.hh"

XrdVERSIONINFO(XrdOssGetStorageSystem, XrdBlackholeOss);

XrdSysError XrdBlackholeEroute(0);
XrdOucTrace XrdBlackholeTrace(&XrdBlackholeEroute);

/// timestamp output for logging messages
// static std::string ts() {
//     std::time_t t = std::time(nullptr);
//     char mbstr[50];
//     std::strftime(mbstr, sizeof(mbstr), "%y%m%d %H:%M:%S ", std::localtime(&t));
//     return std::string(mbstr);
// }

BlackholeFS g_blackholeFS;

// log wrapping function to be used by ceph_posix interface
char g_logstring[1024];
//static void logwrapper(char *format, va_list argp) {
//  vsnprintf(g_logstring, 1024, format, argp);
//  XrdBlackholeEroute.Say(ts().c_str(), g_logstring);
//}

//static void logwrapper(char* format, ...) {
//  if (0 == g_logfunc) return;
//  va_list arg;
//  va_start(arg, format);
//  (*g_logfunc)(format, arg);
//  va_end(arg);
//}



/// converts a logical filename to physical one if needed
void m_translateFileName(std::string &physName, std::string logName){
  physName = logName;
}

/**
 * Get an integer numeric value from an extended attribute attached to an object
 *
 * @brief Retrieve an integer-value extended attribute.
 * @param path the object ID containing the attribute
 * @param attrName the name of the attribute to retrieve
 * @param maxAttrLen the largest number of characters to handle
 * @return value of the attibute, -EINVAL if not valid integer, or -ENOMEM
 *
 * Implementation:
 * Ian Johnson, ian.johnson@stfc.ac.uk, 2022
 *
 */

ssize_t getNumericAttr(const char* const path, const char* attrName, const int maxAttrLen)
{

  ssize_t retval;
  char *attrValue = (char*)malloc(maxAttrLen+1);
  if (NULL == attrValue) {
    return -ENOMEM;
  }

  ssize_t attrLen = 0; // ceph_posix_getxattr((XrdOucEnv*)NULL, path, attrName, attrValue, maxAttrLen);

  if (attrLen <= 0) {
    retval = -EINVAL;
  } else {
    attrValue[attrLen] = (char)NULL;
    char *endPointer = (char *)NULL;
    retval = strtoll(attrValue, &endPointer, 10);
  }

  if (NULL != attrValue) {
    free(attrValue);
  }
  
  return retval;

}

extern "C"
{
  XrdOss*
  XrdOssGetStorageSystem(XrdOss* native_oss,
                         XrdSysLogger* lp,
                         const char* config_fn,
                         const char* parms)
  {
    // Do the herald thing
    XrdBlackholeEroute.SetPrefix("ceph_");
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
   //disable posc  
   XrdOucEnv::Export("XRDXROOTD_NOPOSC", "1");
   // If there is no config file, nothing to be done
   if (configfn && *configfn) {
     // Try to open the configuration file.
     int cfgFD;
     if ((cfgFD = open(configfn, O_RDONLY, 0)) < 0) {
       Eroute.Emsg("Config", errno, "open config file", configfn);
       return 1;
     }
     Config.Attach(cfgFD);
     // Now start reading records until eof.
     char *var;
     while((var = Config.GetMyFirstWord())) {
      // start loop over vars 
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


     } // end config read loop
     // Now check if any errors occured during file i/o
     int retc = Config.LastError();
     if (retc) {
       NoGo = Eroute.Emsg("Config", -retc, "read config file",
                          configfn);
     }
     Config.Close();
   } // if config
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

//SCS - lie to posix-assuming clients about directories [fixes brittleness in GFAL2]
int XrdBlackholeOss::Mkdir(const char *path, mode_t mode, int mkpath, XrdOucEnv *envP) {
  return 0;
}

//SCS - lie to posix-assuming clients about directories [fixes brittleness in GFAL2]
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
    // XRootD assumes an 'offline' file if st_dev and st_ino 
    // are zero. Set to non-zero (meaningful) values to avoid this 
    auto stub = g_blackholeFS.getStub(path);
    *buff = stub->m_stat;

      XrdBlackholeEroute.Say(__FUNCTION__, " Stat response ", 
              std::to_string(stub->m_size).c_str());

  } else {
    return -ENOENT; // #TBD
  }

  return 0;


}



int XrdBlackholeOss::StatFS(const char *path, char *buff, int &blen, XrdOucEnv *eP) {

  XrdOssVSInfo sP;
  //int rc = StatVS(&sP, 0, 0);
  //if (rc) {
  //  return rc;
  //}
  int percentUsedSpace = 0; //(sP.Usage*100)/sP.Total;
  blen = snprintf(buff, blen, "%d %lld %d %d %lld %d",
                  1, sP.Free, percentUsedSpace, 0, 0LL, 0);
  return XrdOssOK;
}

int XrdBlackholeOss::StatVS(XrdOssVSInfo *sP, const char *sname, int updt) {

  int rc = 0; //ceph_posix_statfs(&(sP->Total), &(sP->Free));
  if (rc) {
    return rc;
  }
  sP->Large = sP->Total;
  sP->LFree = sP->Free;
  sP->Usage = sP->Total-sP->Free;
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
  long long freeSpace = 0;


  freeSpace = totalSpace - usedSpace;
  blen = formatStatLSResponse(buff, blen, 
    path,       /* "oss.cgroup" */ 
    totalSpace, /* "oss.space"  */
    usedSpace,  /* "oss.used"   */
    freeSpace,  /* "oss.free"   */
    totalSpace, /* "oss.quota"  */
    freeSpace   /* "oss.maxf"   */);
  return XrdOssOK;
}
 
int XrdBlackholeOss::Truncate (const char* path,
                          unsigned long long size,
                          XrdOucEnv* env) {
  return -ENOTSUP; 
}

int XrdBlackholeOss::Unlink(const char *path, int Opts, XrdOucEnv *env) {
  int rc = g_blackholeFS.unlink(path);
  return rc; 
}

XrdOssDF* XrdBlackholeOss::newDir(const char *tident) {
  return new XrdBlackholeOssDir(this);
}

XrdOssDF* XrdBlackholeOss::newFile(const char *tident) {
  std::string name = std::to_string(*tident);
  // logwrapper((char*)"%s", name.c_str());
  XrdBlackholeEroute.Say("newFile: ", name.c_str());
  return new XrdBlackholeOssFile(this);
}

