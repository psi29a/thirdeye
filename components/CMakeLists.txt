project (Components)

# source files
add_component_dir (files
    linuxpath windowspath macospath fixedpath multidircollection collections fileops configurationmanager
    filelibrary lowlevelfile
    )
    
# Fix for not visible pthreads functions for linker with glibc 2.15
#if (UNIX AND NOT APPLE)
#target_link_libraries(components ${CMAKE_THREAD_LIBS_INIT})
#endif()

# Make the variable accessible for other subdirectories
set(COMPONENT_FILES ${COMPONENT_FILES} PARENT_SCOPE)
