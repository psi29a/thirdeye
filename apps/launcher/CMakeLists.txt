# Qt6 launcher. Enable with -DBUILD_LAUNCHER=ON. find_package picks up
# Homebrew qt6, Ubuntu qt6-base-dev, and the official Qt SDK on Windows.

find_package(Qt6 6.5 COMPONENTS Widgets Network REQUIRED)

# miniz — reads the game data straight out of GOG's Linux .sh installer (a
# MojoSetup script with a plain ZIP appended; zip readers find the archive
# from the end of the file, so the shell prefix is invisible).
# Vendored in miniz/ (the amalgamated miniz.c/miniz.h from the 3.0.2 release
# + its MIT license) so configuring needs no network.
add_library(miniz STATIC ${CMAKE_CURRENT_SOURCE_DIR}/miniz/miniz.c)
target_include_directories(miniz SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/miniz)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# Git build info (configure-time; CI configures fresh every run so this is
# accurate where it matters). LAUNCHER_GIT_TAG is non-empty only when HEAD is
# exactly on a tag — that's what distinguishes a release build from a dev/CI
# build in the version label.
find_package(Git QUIET)
set(LAUNCHER_GIT_TAG "")
set(LAUNCHER_GIT_COMMIT "")
if (GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} describe --tags --exact-match
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE LAUNCHER_GIT_TAG
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE LAUNCHER_GIT_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
endif()

# config file
configure_file ("${CMAKE_CURRENT_SOURCE_DIR}/config.hpp.cmake" "${CMAKE_CURRENT_SOURCE_DIR}/config.hpp")

set(LAUNCHER
    main.cpp
    MainWindow.cpp
    unarj.cpp
    resources/launcher.qrc
)

set(LAUNCHER_HEADER
    MainWindow.hpp
    unarj.hpp
    config.hpp
)
source_group(launcher FILES ${LAUNCHER} ${LAUNCHER_HEADER})

include_directories(${CMAKE_CURRENT_BINARY_DIR})

# Windows: link as a GUI app so no console window pops up alongside the
# launcher. Other platforms default to the console/GUI-agnostic subsystem.
if(WIN32)
    set(LAUNCHER_GUI_TYPE WIN32)
endif()

add_executable(thirdeye-launcher
    ${LAUNCHER_GUI_TYPE}
    ${LAUNCHER} ${LAUNCHER_HEADER}
)

target_link_libraries(thirdeye-launcher PRIVATE
    Qt6::Widgets
    Qt6::Network
    components
    miniz
)

if(DPKG_PROGRAM)
    INSTALL(TARGETS thirdeye-launcher RUNTIME DESTINATION games COMPONENT thirdeye-launcher)
endif()

if (BUILD_WITH_CODE_COVERAGE)
    add_definitions (--coverage)
    target_link_libraries(thirdeye-launcher gcov)
endif()
