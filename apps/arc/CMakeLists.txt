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
    system.hpp
)

source_group(arc FILES ${ARC} ${ARC_HEADER})

include_directories(${CMAKE_CURRENT_BINARY_DIR})

# Main executable
add_executable(arc
    ${ARC} ${ARC_HEADER}
)

target_link_libraries(arc)

# arc is a near-verbatim port of 1992 C source (per project CLAUDE.md, "don't
# 'fix' unless it matters"). Modern C++ compilers emit hundreds of warnings on
# its old idioms — string-literal -> char*, unbraced if/else chains, sprintf
# deprecations on macOS — none of which we want to refactor away. Silence them
# on the arc target only; substantive correctness warnings are left enabled.
if (MSVC)
    target_compile_options(arc PRIVATE
        # The 1992 source assigns string literals to BYTE*/char* hundreds of
        # times. Under MSVC's default C++17/20 conformance that's a HARD ERROR
        # (C2440), not a warning. /Zc:strictStrings- opts back into the old
        # implicit-const-strip behavior so the port compiles without rewriting
        # half the codebase.
        /Zc:strictStrings-
        /wd4101  # unreferenced local variable
        /wd4244  # narrowing conversion
        /wd4267  # size_t -> smaller
        /wd4996  # deprecated POSIX/CRT
    )
    target_compile_definitions(arc PRIVATE
        _CRT_SECURE_NO_WARNINGS
        _CRT_NONSTDC_NO_DEPRECATE
    )
else()
    target_compile_options(arc PRIVATE
        -Wno-write-strings
        -Wno-writable-strings
        -Wno-dangling-else
        -Wno-deprecated-declarations
    )
endif()

if(DPKG_PROGRAM)
    INSTALL(TARGETS arc RUNTIME DESTINATION games COMPONENT arc)
endif()

if (BUILD_WITH_CODE_COVERAGE)
  add_definitions (--coverage)
  target_link_libraries(arc gcov)
endif()
