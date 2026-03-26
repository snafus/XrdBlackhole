# ---------------------------------------------------------------------------
# tests/CMakeLists.txt — GoogleTest unit tests for XrdBlackhole
#
# Test executables compile the core source files directly rather than linking
# the plugin MODULE library (which cannot be linked into other targets).
# The test_globals.cc file provides minimal definitions of the XRootD globals
# that the production code references, standing in for XrdBlackholeOss.cc.
# ---------------------------------------------------------------------------

include( GoogleTest )

set( SRC ${CMAKE_SOURCE_DIR}/src/XrdBlackhole )

# Common include paths shared by all test targets.
set( TEST_INCLUDES
  ${CMAKE_SOURCE_DIR}/src
  ${XROOTD_INCLUDE_DIR} )

# ---------------------------------------------------------------------------
# test_blackholefs — BlackholeFS lifecycle, open/close/unlink, create_defaults
# ---------------------------------------------------------------------------
add_executable( test_blackholefs
  ${SRC}/BlackholeFS.cc
  test_globals.cc
  test_BlackholeFS.cc )

target_include_directories( test_blackholefs PRIVATE ${TEST_INCLUDES} )
target_link_libraries( test_blackholefs GTest::gtest_main ${XROOTD_LIBRARIES} )
gtest_discover_tests( test_blackholefs )

# ---------------------------------------------------------------------------
# test_stats — XrdBlackholeStatsManager aggregation and snapshot
# ---------------------------------------------------------------------------
add_executable( test_stats
  ${SRC}/XrdBlackholeStats.cc
  test_globals.cc
  test_Stats.cc )

target_include_directories( test_stats PRIVATE ${TEST_INCLUDES} )
target_link_libraries( test_stats GTest::gtest_main ${XROOTD_LIBRARIES} )
gtest_discover_tests( test_stats )
