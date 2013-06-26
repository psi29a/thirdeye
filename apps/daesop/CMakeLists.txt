# config file
configure_file ("${CMAKE_CURRENT_SOURCE_DIR}/config.hpp.cmake" "${CMAKE_CURRENT_SOURCE_DIR}/config.hpp")

set(DAESOP
    convert.cpp
    daesop.cpp
    damap.cpp
    dasm.cpp
    dblocks.cpp
    dict.cpp
    dvar.cpp    
    rentry.cpp
    utils.cpp
)

set(DAESOP_HEADER
    resstr.hpp
    config.hpp
)

source_group(game FILES ${DAESOP} ${DAESOP_HEADER})

include_directories(${CMAKE_CURRENT_BINARY_DIR})

# Main executable
add_executable(daesop
    ${DAESOP} ${DAESOP_HEADER}
)

target_link_libraries(daesop
    ${Boost_LIBRARIES}
#    components
)

if(DPKG_PROGRAM)
    INSTALL(TARGETS daesop RUNTIME DESTINATION games COMPONENT daesop)
endif()

if (BUILD_WITH_CODE_COVERAGE)
  add_definitions (--coverage)
  target_link_libraries(daesop gcov)
endif()
