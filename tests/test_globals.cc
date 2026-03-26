// Minimal definitions of the XRootD globals that production code references
// via extern declarations. These stand in for the definitions normally found
// in XrdBlackholeOss.cc, without pulling in the plugin entry point or the
// XrdVersionINFO macro (which requires the generated XrdVersion.hh).
//
// Rule: define globals here in the same order as their dependencies.
//   XrdBlackholeEroute must be constructed before XrdBlackholeTrace.

#include "XrdSys/XrdSysError.hh"
#include "XrdOuc/XrdOucTrace.hh"

// Logger handle 0 → messages are silently discarded during tests.
XrdSysError XrdBlackholeEroute(0);
XrdOucTrace XrdBlackholeTrace(&XrdBlackholeEroute);
