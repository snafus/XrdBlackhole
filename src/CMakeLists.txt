
#-------------------------------------------------------------------------------
# Include the subcomponents
#-------------------------------------------------------------------------------
include_directories( ${CMAKE_CURRENT_SOURCE_DIR} )
if( XRDBLACKHOLE_SUBMODULE )
  add_compile_definitions( XRDBLACKHOLE_SUBMODULE )
endif()
include( XrdBlackhole )

