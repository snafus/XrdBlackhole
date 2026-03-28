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
message( STATUS "XrdBlackholeMetrics: searching for XrdHttp/XrdHttpExtHandler.hh" )
message( STATUS "  XROOTD_INCLUDE_DIR = ${XROOTD_INCLUDE_DIR}" )

find_path( XRDHTTP_INCLUDE_DIR
           NAMES XrdHttp/XrdHttpExtHandler.hh
           HINTS ${XROOTD_INCLUDE_DIR}
                 ${XROOTD_INCLUDE_DIR}/private
                 /usr/include/xrootd
                 /usr/local/include/xrootd )

message( STATUS "  XRDHTTP_INCLUDE_DIR = ${XRDHTTP_INCLUDE_DIR}" )

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

  # g_statsManager is defined in libXrdBlackhole-5.so.  Add an explicit
  # DT_NEEDED so the dynamic linker resolves it when the metrics plugin is
  # dlopen'd (XRootD loads plugins with RTLD_LOCAL, so symbols are not
  # shared between plugins without an explicit dependency).
  target_link_options(
    ${LIB_XRD_BLACKHOLE_METRICS}
    PRIVATE $<TARGET_FILE:${LIB_XRD_BLACKHOLE}> )

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
