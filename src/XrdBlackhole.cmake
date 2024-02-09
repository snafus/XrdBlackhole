include_directories( ${XROOTD_INCLUDE_DIR} )
include_directories( ${CMAKE_SOURCE_DIR}/src )


#-------------------------------------------------------------------------------
# XrdBlackholePosix library version
#-------------------------------------------------------------------------------
set( XRD_BLACKHOLE_POSIX_VERSION   0.0.1 )
set( XRD_BLACKHOLE_POSIX_SOVERSION 0 )

#-------------------------------------------------------------------------------
# The XrdBlackhole module
#-------------------------------------------------------------------------------
set( LIB_XRD_BLACKHOLE XrdBlackhole-${PLUGIN_VERSION} )

add_library(
  ${LIB_XRD_BLACKHOLE}
  MODULE
  XrdBlackhole/XrdBlackholeOss.cc       XrdBlackhole/XrdBlackholeOss.hh
  XrdBlackhole/XrdBlackholeOssFile.cc   XrdBlackhole/XrdBlackholeOssFile.hh
  XrdBlackhole/XrdBlackholeOssDir.cc    XrdBlackhole/XrdBlackholeOssDir.hh
  )

target_link_libraries(
  ${LIB_XRD_BLACKHOLE}
  ${XROOTD_LIBRARIES}  
  XrdBlackholePosix )

set_target_properties(
  ${LIB_XRD_BLACKHOLE}
  PROPERTIES
  INTERFACE_LINK_LIBRARIES ""
  LINK_INTERFACE_LIBRARIES "" )

#-------------------------------------------------------------------------------
# The XrdBlackholeXattr module
#-------------------------------------------------------------------------------
set( LIB_XRD_BLACKHOLE_XATTR XrdBlackholeXattr-${PLUGIN_VERSION} )

add_library(
  ${LIB_XRD_BLACKHOLE_XATTR}
  MODULE
  XrdBlackhole/XrdBlackholeXAttr.cc   XrdBlackhole/XrdBlackholeXAttr.hh )

target_link_libraries(
  ${LIB_XRD_BLACKHOLE_XATTR}
  ${XROOTD_LIBRARIES}  
  XrdBlackholePosix )

set_target_properties(
  ${LIB_XRD_BLACKHOLE_XATTR}
  PROPERTIES
  INTERFACE_LINK_LIBRARIES ""
  LINK_INTERFACE_LIBRARIES "" )

#-------------------------------------------------------------------------------
# Install
#-------------------------------------------------------------------------------
install(
  TARGETS ${LIB_XRD_BLACKHOLE} ${LIB_XRD_BLACKHOLE_XATTR} XrdBlackholePosix
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} )
