# config file
configure_file ("${CMAKE_CURRENT_SOURCE_DIR}/config.hpp.cmake" "${CMAKE_CURRENT_SOURCE_DIR}/config.hpp")

set(ARC
    system.cpp
    sopcomp.cpp
    rscomp.cpp
    resource.cpp
    resfile.cpp
    preproc.cpp
    palcomp.cpp
    mapcomp.cpp
    lexan.cpp
    dict.cpp
    arc.cpp
)

set(ARC_HEADER
    config.hpp
)

source_group(arc FILES ${ARC} ${ARC_HEADER})

include_directories(${CMAKE_CURRENT_BINARY_DIR})

# Main executable
add_executable(arc
    ${ARC} ${ARC_HEADER}
)

target_link_libraries(arc)

if(DPKG_PROGRAM)
    INSTALL(TARGETS arc RUNTIME DESTINATION games COMPONENT arc)
endif()

if (BUILD_WITH_CODE_COVERAGE)
  add_definitions (--coverage)
  target_link_libraries(arc gcov)
endif()
