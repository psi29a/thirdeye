
# config file
set(TESTS_DATA_DIR "${CMAKE_SOURCE_DIR}/files")
configure_file ("${CMAKE_CURRENT_SOURCE_DIR}/config.hpp.cmake" "${CMAKE_CURRENT_SOURCE_DIR}/config.hpp")

# local files
set(UNITTEST
    main.cpp
)

set(UNITTEST_HEADER

)
source_group(game FILES ${UNITTEST} ${UNITTEST_HEADER})

add_runtests_dir (graphics
    graphics font palette bitmap
    )

add_runtests_dir (resources
    res
    )

add_runtests_dir (vm
    vm opcodes objects events
    )

# Main executable
add_executable(runtests
    ${UNITTEST} ${UNITTEST_HEADER}
    ${UNITTEST_LIBS} ${UNITTEST_LIBS_HEADER}
    ${UNITTEST_FILES}
)

target_link_libraries(runtests
    ${SDL2_LIBRARY}
    ${SDL2MAIN_LIBRARY}
    ${WILDMIDI_LIBRARY}
    ${OPENAL_LIBRARY}
    GTest::gtest_main   # pulls in GTest::gtest transitively
    components
)

# Fix for not visible pthreads functions for linker with glibc 2.15
if (UNIX AND NOT APPLE)
target_link_libraries(runtests ${CMAKE_THREAD_LIBS_INIT})
endif()

# Register with CTest so `ctest --test-dir build` runs the suite. We use
# add_test (not gtest_discover_tests) so the binary is only executed at test
# time, not during the build -- this avoids needing the SDL2/OpenAL/WildMIDI
# runtime DLLs on PATH at build time (notably on Windows/vcpkg).
add_test(NAME runtests COMMAND runtests)

# daesop converter tests -- a separate executable: daesop is its own app (no
# SDL, C-style globals), so we compile its non-main sources here rather than
# entangle them with the thirdeye runtests link. Needs daesop's generated
# config.hpp, so gate on BUILD_DAESOP (which is what generates it).
if (BUILD_DAESOP)
    set(DAESOP_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../daesop)
    add_executable(daesoptests
        daesop_tests.cpp
        ${DAESOP_DIR}/convert.cpp
        ${DAESOP_DIR}/utils.cpp
        ${DAESOP_DIR}/dblocks.cpp
        ${DAESOP_DIR}/dict.cpp
        ${DAESOP_DIR}/rentry.cpp
        ${DAESOP_DIR}/dvar.cpp
        ${DAESOP_DIR}/damap.cpp
        ${DAESOP_DIR}/dasm.cpp
    )
    target_include_directories(daesoptests PRIVATE ${DAESOP_DIR})
    target_link_libraries(daesoptests GTest::gtest_main)
    add_test(NAME daesoptests COMMAND daesoptests)
endif()

if(DPKG_PROGRAM)
    INSTALL(TARGETS runtests RUNTIME DESTINATION games COMPONENT runtests)
endif(DPKG_PROGRAM)

if (BUILD_WITH_CODE_COVERAGE)
  add_definitions (--coverage)
  target_link_libraries(tests gcov)
endif()
