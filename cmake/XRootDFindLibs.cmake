#-------------------------------------------------------------------------------
# Find the required libraries
#-------------------------------------------------------------------------------

find_package( XRootD REQUIRED )

#-------------------------------------------------------------------------------
# The plug-in version is the major version of the XRootD we build against:
# XRootD 6 looks for libXrdBlackhole-6.so.  It is read from XrdVersion.hh:
# from XrdVNUMBER (60201 for 6.2.1), or from XrdVERSION ("v6.2.1-14-g...")
# for a build that is not a release, where the number is 1000000.
# Set -DPLUGIN_VERSION=<major> to override.
#-------------------------------------------------------------------------------
if( NOT DEFINED PLUGIN_VERSION OR "${PLUGIN_VERSION}" STREQUAL "" )
  set( PLUGIN_VERSION "" )
  find_file( XROOTD_VERSION_HEADER XrdVersion.hh
             HINTS ${XROOTD_INCLUDE_DIR} NO_DEFAULT_PATH )
  if( XROOTD_VERSION_HEADER )
    file( STRINGS ${XROOTD_VERSION_HEADER} _xrd_vnumber
          REGEX "^#define[ \t]+XrdVNUMBER[ \t]+[0-9]+" )
    file( STRINGS ${XROOTD_VERSION_HEADER} _xrd_vstring
          REGEX "^#define[ \t]+XrdVERSION[ \t]+\"v[0-9]+\\." )
    if( _xrd_vnumber MATCHES "XrdVNUMBER[ \t]+([0-9]+)" )
      set( _xrd_vnumber ${CMAKE_MATCH_1} )
      if( _xrd_vnumber GREATER 9999 AND _xrd_vnumber LESS 1000000 )
        math( EXPR PLUGIN_VERSION "${_xrd_vnumber} / 10000" )
      endif()
    endif()
    if( "${PLUGIN_VERSION}" STREQUAL "" AND _xrd_vstring MATCHES "\"v([0-9]+)\\." )
      set( PLUGIN_VERSION ${CMAKE_MATCH_1} )
    endif()
  endif()
  if( "${PLUGIN_VERSION}" STREQUAL "" )
    set( PLUGIN_VERSION 5 )
    message( WARNING "Cannot read the XRootD version from XrdVersion.hh; "
                     "using plug-in version ${PLUGIN_VERSION}. "
                     "Set -DPLUGIN_VERSION=<XRootD major version> if this is wrong." )
  else()
    message( STATUS "Plug-in version ${PLUGIN_VERSION}, from ${XROOTD_VERSION_HEADER}" )
  endif()
endif()

if( ENABLE_TESTS )
  if( CMAKE_VERSION VERSION_LESS "3.14" )
    message( FATAL_ERROR "ENABLE_TESTS=ON requires CMake >= 3.14 (FetchContent_MakeAvailable)" )
  endif()

  message( STATUS "GoogleTest: fetching v1.14.0 via FetchContent" )
  include( FetchContent )
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
    GIT_SHALLOW    TRUE )
  # Prevent GTest from overriding the parent project's compiler/linker settings.
  set( gtest_force_shared_crt ON CACHE BOOL "" FORCE )
  FetchContent_MakeAvailable( googletest )
  set( BUILD_TESTS TRUE )
endif()
