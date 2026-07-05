# Qt6 launcher. Enable with -DBUILD_LAUNCHER=ON. find_package picks up
# Homebrew qt6, Ubuntu qt6-base-dev, and the official Qt SDK on Windows.

find_package(Qt6 6.5 COMPONENTS Widgets REQUIRED)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# config file
configure_file ("${CMAKE_CURRENT_SOURCE_DIR}/config.hpp.cmake" "${CMAKE_CURRENT_SOURCE_DIR}/config.hpp")

set(LAUNCHER
    main.cpp
    MainWindow.cpp
    resources/launcher.qrc
)

set(LAUNCHER_HEADER
    MainWindow.hpp
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
)

if(DPKG_PROGRAM)
    INSTALL(TARGETS thirdeye-launcher RUNTIME DESTINATION games COMPONENT thirdeye-launcher)
endif()

if (BUILD_WITH_CODE_COVERAGE)
    add_definitions (--coverage)
    target_link_libraries(thirdeye-launcher gcov)
endif()
