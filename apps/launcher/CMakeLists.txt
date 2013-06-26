# config file
configure_file ("${CMAKE_CURRENT_SOURCE_DIR}/config.hpp.cmake" "${CMAKE_CURRENT_SOURCE_DIR}/config.hpp")


set(LAUNCHER
    main.cpp
)

set(LAUNCHER_HEADER
    config.hpp
)
source_group(game FILES ${LAUNCHER} ${LAUNCHER_HEADER})

include_directories(${CMAKE_CURRENT_BINARY_DIR})

# Main executable
add_executable(thirdeyelauncher
    ${GUI_TYPE}
    ${LAUNCHER} ${LAUNCHER_HEADER}
)

target_link_libraries(thirdeyelauncher
    ${Boost_LIBRARIES}
#    components
)

if(DPKG_PROGRAM)
    INSTALL(TARGETS thirdeyelauncher RUNTIME DESTINATION games COMPONENT thirdeyelauncher)
endif()

if (BUILD_WITH_CODE_COVERAGE)
  add_definitions (--coverage)
  target_link_libraries(thirdeyelauncher gcov)
endif()
