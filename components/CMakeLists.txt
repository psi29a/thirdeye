project (Components)

# source files
#add_component_dir (settings settings )

#add_component_dir (to_utf8 to_utf8 )

# Fix for not visible pthreads functions for linker with glibc 2.15
#if (UNIX AND NOT APPLE)
#target_link_libraries(components ${CMAKE_THREAD_LIBS_INIT})
#endif()

# Make the variable accessible for other subdirectories
set(COMPONENT_FILES ${COMPONENT_FILES} PARENT_SCOPE)
