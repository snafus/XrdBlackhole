// Minimal definitions of the XRootD globals that production code references
// via extern declarations. These stand in for the definitions normally found
// in XrdBlackholeOss.cc, without pulling in the plugin entry point or the
// XrdVersionINFO macro (which requires the generated XrdVersion.hh).
//
// In the real plugin, XrdBlackholeEroute starts with a null logger and
// receives a real one from XrdOssGetStorageSystem() before any I/O calls.
// In tests, I/O paths are exercised immediately, so we must provide a real
// XrdSysLogger up front.  Writing to /dev/null keeps test output clean.

#include <fcntl.h>

#include "XrdSys/XrdSysError.hh"
#include "XrdSys/XrdSysLogger.hh"
#include "XrdOuc/XrdOucTrace.hh"

// Open /dev/null once; all log output from production code is discarded.
static int        g_devnull = open("/dev/null", O_WRONLY);
static XrdSysLogger g_logger(g_devnull, 0);

XrdSysError XrdBlackholeEroute(&g_logger);
XrdOucTrace XrdBlackholeTrace(&XrdBlackholeEroute);
