
# config file
configure_file ("${CMAKE_CURRENT_SOURCE_DIR}/config.hpp.cmake" "${CMAKE_CURRENT_SOURCE_DIR}/config.hpp")

# local files
set(GAME
    main.cpp
    engine.cpp
)

set(GAME_HEADER
    engine.hpp
    config.hpp
)
source_group(game FILES ${GAME} ${GAME_HEADER})

# Main executable
add_executable(openadd
    ${OPENADD_LIBS} ${OPENADD_LIBS_HEADER}
    ${OPENADD_FILES}
    ${GAME} ${GAME_HEADER}
    ${APPLE_BUNDLE_RESOURCES}
)

# Sound stuff - here so CMake doesn't stupidly recompile EVERYTHING
# when we change the backend.

target_link_libraries(openadd
    ${Boost_LIBRARIES}
    ${SDL2_LIBRARY}
    components
)

# Fix for not visible pthreads functions for linker with glibc 2.15
if (UNIX AND NOT APPLE)
target_link_libraries(openadd ${CMAKE_THREAD_LIBS_INIT})
endif()

if(APPLE)
    find_library(CARBON_FRAMEWORK Carbon)
    find_library(COCOA_FRAMEWORK Cocoa)
    find_library(IOKIT_FRAMEWORK IOKit)
    target_link_libraries(openadd ${CARBON_FRAMEWORK} ${COCOA_FRAMEWORK} ${IOKIT_FRAMEWORK})

    if (FFMPEG_FOUND)
        find_library(COREVIDEO_FRAMEWORK CoreVideo)
        find_library(VDA_FRAMEWORK VideoDecodeAcceleration)
        target_link_libraries(openadd ${COREVIDEO_FRAMEWORK} ${VDA_FRAMEWORK})
    endif()
endif(APPLE)

if(DPKG_PROGRAM)
    INSTALL(TARGETS openadd RUNTIME DESTINATION games COMPONENT openadd)
endif(DPKG_PROGRAM)

if (BUILD_WITH_CODE_COVERAGE)
  add_definitions (--coverage)
  target_link_libraries(openadd gcov)
endif()
