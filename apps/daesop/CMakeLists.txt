# config file
configure_file ("${CMAKE_CURRENT_SOURCE_DIR}/config.hpp.cmake" "${CMAKE_CURRENT_SOURCE_DIR}/config.hpp")

set(DAESOP
    convert.cpp
    daesop.cpp
    damap.cpp
    dasm.cpp
    dblocks.cpp
    dict.cpp
    dvar.cpp    
    rentry.cpp
    utils.cpp
)

set(DAESOP_HEADER
    resstr.hpp
    config.hpp
)

source_group(game FILES ${DAESOP} ${DAESOP_HEADER})

include_directories(${CMAKE_CURRENT_BINARY_DIR})

# Main executable
add_executable(daesop
    ${DAESOP} ${DAESOP_HEADER}
)

target_link_libraries(daesop
#    components
)

# daesop's disassembler reads files/bytecode.def; copy it next to the executable
# so it can be found relative to the binary (not the current directory).
add_custom_command(TARGET daesop POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/files/bytecode.def"
        "$<TARGET_FILE_DIR:daesop>/bytecode.def"
    COMMENT "Copying bytecode.def next to daesop"
)

if(DPKG_PROGRAM)
    INSTALL(TARGETS daesop RUNTIME DESTINATION games COMPONENT daesop)
endif()

if (BUILD_WITH_CODE_COVERAGE)
  add_definitions (--coverage)
  target_link_libraries(daesop gcov)
endif()
