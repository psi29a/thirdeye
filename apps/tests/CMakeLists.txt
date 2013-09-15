
# config file
configure_file ("${CMAKE_CURRENT_SOURCE_DIR}/config.hpp.cmake" "${CMAKE_CURRENT_SOURCE_DIR}/config.hpp")

# local files
set(UNITTEST
    main.cpp
)

set(UNITTEST_HEADER

)
source_group(game FILES ${UNITTEST} ${UNITTEST_HEADER})


# Main executable
add_executable(runtests
    ${UNITTEST} ${UNITTEST_HEADER}
)

target_link_libraries(runtests
    ${Boost_LIBRARIES}
    ${SDL2_LIBRARY}
    ${SDL2MAIN_LIBRARY}
    ${WILDMIDI_LIBRARY}
    ${OPENAL_LIBRARY}
    ${GTEST_LIBRARIES} 
    ${GTEST_MAIN_LIBRARIES} 
    pthread
    components
)

# Fix for not visible pthreads functions for linker with glibc 2.15
if (UNIX AND NOT APPLE)
target_link_libraries(runtests ${CMAKE_THREAD_LIBS_INIT})
endif()

if(DPKG_PROGRAM)
    INSTALL(TARGETS runtests RUNTIME DESTINATION games COMPONENT runtests)
endif(DPKG_PROGRAM)

if (BUILD_WITH_CODE_COVERAGE)
  add_definitions (--coverage)
  target_link_libraries(tests gcov)
endif()
