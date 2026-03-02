include_directories( ${XROOTD_INCLUDE_DIR} )
include_directories( ${CMAKE_SOURCE_DIR}/src )

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
  XrdBlackhole/XrdBlackholeStats.cc     XrdBlackhole/XrdBlackholeStats.hh
  XrdBlackhole/BlackholeFS.cc           XrdBlackhole/BlackholeFS.hh
  )

target_link_libraries(
  ${LIB_XRD_BLACKHOLE}
  ${XROOTD_LIBRARIES}
)

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
  ${XROOTD_LIBRARIES} )

set_target_properties(
  ${LIB_XRD_BLACKHOLE_XATTR}
  PROPERTIES
  INTERFACE_LINK_LIBRARIES ""
  LINK_INTERFACE_LIBRARIES "" )

#-------------------------------------------------------------------------------
# The XrdBlackholeMetrics module (optional — requires XrdHttp headers)
#-------------------------------------------------------------------------------
find_path( XRDHTTP_INCLUDE_DIR XrdHttp/XrdHttpExtHandler.hh
           HINTS ${XROOTD_INCLUDE_DIR} )

if( XRDHTTP_INCLUDE_DIR )
  message( STATUS "XrdHttp headers found — building XrdBlackholeMetrics" )

  set( LIB_XRD_BLACKHOLE_METRICS XrdBlackholeMetrics-${PLUGIN_VERSION} )

  add_library(
    ${LIB_XRD_BLACKHOLE_METRICS}
    MODULE
    XrdBlackhole/XrdBlackholeMetrics.cc   XrdBlackhole/XrdBlackholeMetrics.hh )

  target_include_directories(
    ${LIB_XRD_BLACKHOLE_METRICS}
    PRIVATE ${XRDHTTP_INCLUDE_DIR} )

  target_link_libraries(
    ${LIB_XRD_BLACKHOLE_METRICS}
    ${XROOTD_LIBRARIES} )

  set_target_properties(
    ${LIB_XRD_BLACKHOLE_METRICS}
    PROPERTIES
    INTERFACE_LINK_LIBRARIES ""
    LINK_INTERFACE_LIBRARIES "" )

  install(
    TARGETS ${LIB_XRD_BLACKHOLE_METRICS}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} )

else()
  message( STATUS "XrdHttp headers not found — skipping XrdBlackholeMetrics" )
endif()

#-------------------------------------------------------------------------------
# Install
#-------------------------------------------------------------------------------
install(
  TARGETS ${LIB_XRD_BLACKHOLE} ${LIB_XRD_BLACKHOLE_XATTR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} )
