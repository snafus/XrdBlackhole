#-------------------------------------------------------------------------------
# Find the required libraries
#-------------------------------------------------------------------------------

find_package( XRootD REQUIRED )

# find_package( ceph REQUIRED )

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
