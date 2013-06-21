set(LAUNCHER
    main.cpp
)

include_directories(${CMAKE_CURRENT_BINARY_DIR})

# Main executable
add_executable(thirdeyelauncher
    ${GUI_TYPE}
    ${LAUNCHER}
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
