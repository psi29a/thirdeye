set(LAUNCHER
    main.cpp
)

include_directories(${CMAKE_CURRENT_BINARY_DIR})

# Main executable
add_executable(openaddlauncher
    ${GUI_TYPE}
    ${LAUNCHER}
)

target_link_libraries(openaddlauncher
    ${Boost_LIBRARIES}
#    components
)

if(DPKG_PROGRAM)
    INSTALL(TARGETS openaddlauncher RUNTIME DESTINATION games COMPONENT openaddlauncher)
endif()

if (BUILD_WITH_CODE_COVERAGE)
  add_definitions (--coverage)
  target_link_libraries(openaddlauncher gcov)
endif()
