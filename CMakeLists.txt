cmake_minimum_required(VERSION 3.15)

project(Thirdeye)

if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

if(POLICY CMP0177)
  cmake_policy(SET CMP0177 NEW)
endif()

# Require modern C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Macros
set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake/)
include (ThirdeyeMacros)

# Set a default build type if none was specified
IF(NOT DEFINED CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "")
  MESSAGE(STATUS "Setting build type to 'Debug' as none was specified.")
  SET(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose the type of build." FORCE)
  # Set the possible values of build type for cmake-gui
  SET_PROPERTY(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
ENDIF()
SET(CMAKE_CONFIGURATION_TYPES "${CMAKE_BUILD_TYPE}")
MESSAGE(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")

# Platform specific defines
IF(UNIX)
    # allow 'large' files in 32 bit builds
    ADD_DEFINITIONS(
        -D_LARGEFILE_SOURCE
        -D_FILE_OFFSET_BITS=64
        -D_LARGE_FILES
        )
ENDIF()

# Version
set (THIRDEYE_VERSION_MAJOR 0)
set (THIRDEYE_VERSION_MINOR 88)
set (THIRDEYE_VERSION_RELEASE 0)

set (THIRDEYE_VERSION "${THIRDEYE_VERSION_MAJOR}.${THIRDEYE_VERSION_MINOR}.${THIRDEYE_VERSION_RELEASE}")

# Doxygen main page
configure_file ("${Thirdeye_SOURCE_DIR}/docs/mainpage.hpp.cmake" "${Thirdeye_SOURCE_DIR}/docs/mainpage.hpp")

# Are we building a debug build?
option(DEBUG "Use debug build options" OFF)

# Are we running unit tests?
option(UNIT_TESTS "Run unit tests" ON)


# Apps and tools
option(BUILD_DAESOP "Build Daesop" ON)
option(BUILD_LAUNCHER "Build Thirdeye Launcher" ON)
option(BUILD_ARC "Build ARC" ON)

# Source directory: libs
set(LIBDIR ${CMAKE_SOURCE_DIR}/libs)

# Platform specific variables
## Location of game data files
if(DPKG_PROGRAM)
    set(GAME_DATA_FILES "/usr/share/games/thirdeye/data/" CACHE PATH "Location of Thirdeye data files")
else()
    if (APPLE)
        set(GAME_DATA_FILES "./data" CACHE PATH "Location of Thirdeye data files")
    else()
        set(GAME_DATA_FILES "data" CACHE PATH "Location of Thirdeye data files")
    endif(APPLE)
endif(DPKG_PROGRAM)

if (WIN32)
    option(USE_DEBUG_CONSOLE "whether debug goes to console or to Visual Studio output" ON)
endif()

IF(WIN32)
    ADD_DEFINITIONS(
        -DWIN32_LEAN_AND_MEAN
        -D_CRT_SECURE_NO_WARNINGS
        )
ENDIF()

if (WIN32)
    set(PLATFORM_INCLUDE_DIR "platform")
else (WIN32)
    set(PLATFORM_INCLUDE_DIR "")
    if (UNIX AND NOT APPLE)
        find_path (UUID_INCLUDE_DIR uuid/uuid.h)
    else()
        unset(UUID_INCLUDE_DIR CACHE)
    endif()
endif (WIN32)

if (MSVC10)
    set(PLATFORM_INCLUDE_DIR "")
endif()

if (UUID_INCLUDE_DIR)
    include_directories(SYSTEM ${UUID_INCLUDE_DIR})
endif()

## Fix for not visible pthreads functions for linker with glibc 2.15
if (UNIX AND NOT APPLE)
    find_package (Threads)
endif()

include_directories("."
    ${PLATFORM_INCLUDE_DIR}
    ${OPENAL_INCLUDE_DIR}
    ${LIBDIR}
)

# Treat a dependency's headers as SYSTEM headers so its warnings don't pollute
# our build (e.g. CLI11's MSVC C4866/C4868). Header-only INTERFACE libraries
# can't use target_compile_options -- the warnings are emitted when OUR code
# compiles their headers -- so downgrading to system includes is the right
# lever. Works for GCC/Clang (-isystem) and MSVC (/external:I, paired with the
# /external:W0 added in the compiler settings below).
function(thirdeye_mark_system_includes target)
    if (TARGET ${target})
        get_target_property(_inc ${target} INTERFACE_INCLUDE_DIRECTORIES)
        if (_inc)
            set_target_properties(${target} PROPERTIES
                INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_inc}")
        endif()
    endif()
endfunction()

if (UNIT_TESTS)
    # Fetch and build GoogleTest automatically (no system install required).
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.15.2   # last release supporting CMake 3.15 / C++14+
    )
    # Don't override the parent project's compiler/linker settings on Windows.
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    # We only need the test framework, not GoogleMock or its install rules.
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    # GoogleTest headers are third-party: treat them as system headers so their
    # warnings don't show up when our test code includes <gtest/gtest.h>.
    foreach(gtest_tgt gtest gtest_main)
        thirdeye_mark_system_includes(${gtest_tgt})
    endforeach()

    # GoogleTest's own headers trip -Wcharacter-conversion (char8_t->char32_t)
    # under C++20 on recent Clang. It's third-party code we don't control, so
    # silence that single warning on the fetched targets only (not our code).
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        foreach(gtest_tgt gtest gtest_main)
            if (TARGET ${gtest_tgt})
                target_compile_options(${gtest_tgt} PRIVATE -Wno-character-conversion)
            endif()
        endforeach()
    endif()
endif()

# CLI argument parsing: CLI11 (header-only), fetched automatically.
include(FetchContent)
FetchContent_Declare(
    cli11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG        v2.6.2  # uses cmake_minimum_required(VERSION 3.14...4.2);
                           # v2.4.2 hit the "compat with CMake <3.10 going away" warning
)
FetchContent_MakeAvailable(cli11)
# CLI11 is header-only; mark its headers SYSTEM so its warnings (MSVC C4866/
# C4868, etc.) don't surface when we compile main.cpp.
thirdeye_mark_system_includes(CLI11)

# SDL3 ships a first-class CMake package config that exports the SDL3::SDL3
# imported target (headers + link line + Cocoa/Threads on the relevant
# platforms). No FindSDL3.cmake needed.
find_package(SDL3 CONFIG REQUIRED)

# Find OpenAL: use our FindOpenAL.cmake module to prefer Homebrew openal-soft and fall back to system OpenAL.
find_package(OpenAL REQUIRED)

if(OPENAL_INCLUDE_DIR)
    include_directories(${OPENAL_INCLUDE_DIR})
    set(OPENAL_INCLUDE_DIR ${OPENAL_INCLUDE_DIR} CACHE PATH "OpenAL include dir" FORCE)
    message(STATUS "Using OpenAL include: ${OPENAL_INCLUDE_DIR}")
endif()

if(OPENAL_LIBRARY)
    set(OPENAL_LIBRARY ${OPENAL_LIBRARY} CACHE PATH "OpenAL library path" FORCE)
    message(STATUS "Using OpenAL library: ${OPENAL_LIBRARY}")
endif()

# WildMIDI — vendored via FetchContent. Pinned to a master commit ahead of
# the 0.5.0 tag for two features we depend on:
#   * SoundFont2 (SF2) rendering via TinySoundFont — WildMidi_Init now takes
#     either a .sf2 file or a cfg; the launcher's OPL-3 setup just downloads
#     the .sf2 and points at it, no unsf conversion step.
#   * Quoted paths in cfg files (our previous local carry, now upstream).
# Bump to GIT_TAG wildmidi-0.5.0 once that's tagged and drop this note.
FetchContent_Declare(
    wildmidi
    GIT_REPOSITORY https://github.com/Mindwerks/wildmidi.git
    GIT_TAG        46b10bf9c18644c20de9a925a252da38198e2df5 # master 2026-07-08
    UPDATE_DISCONNECTED TRUE   # don't re-fetch on every configure
)
set(WANT_PLAYER  OFF CACHE BOOL "" FORCE)  # skip WildMIDI's CLI player
set(WANT_STATIC  OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(wildmidi)
# Preserve the WILDMIDI_LIBRARY / WILDMIDI_INCLUDE_DIR names our subdirs use.
set(WILDMIDI_LIBRARY libwildmidi)
set(WILDMIDI_INCLUDE_DIR "${wildmidi_SOURCE_DIR}/include")
include_directories(${WILDMIDI_INCLUDE_DIR})

if (APPLE)
    if (NOT DEFINED APP_BUNDLE_DIR)
        set(APP_BUNDLE_DIR "${Thirdeye_BINARY_DIR}/thirdeye.app")
    endif()
    if (EXISTS "${Thirdeye_SOURCE_DIR}/files/mac/Info.plist" AND EXISTS "${Thirdeye_SOURCE_DIR}/files/mac/THIRDEYE.icns")
        configure_file(${Thirdeye_SOURCE_DIR}/files/mac/Info.plist
            "${APP_BUNDLE_DIR}/Contents/Info.plist")

        configure_file(${Thirdeye_SOURCE_DIR}/files/mac/THIRDEYE.icns
            "${APP_BUNDLE_DIR}/Contents/Resources/THIRDEYE.icns" COPYONLY)
        set(APPLE_BUNDLE_ENABLED TRUE)
    else()
        message(STATUS "Apple bundle resources not found; building as a normal executable.")
    endif()
endif (APPLE)

# Set up DEBUG define
set_directory_properties(PROPERTIES COMPILE_DEFINITIONS_DEBUG DEBUG=1)

# Specify build paths
if (APPLE AND APPLE_BUNDLE_ENABLED)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${APP_BUNDLE_DIR}/Contents/MacOS")
else()
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${Thirdeye_BINARY_DIR}")
endif()


if (NOT WIN32 AND NOT APPLE)
    configure_file(${Thirdeye_SOURCE_DIR}/files/thirdeye.desktop
        "${Thirdeye_BINARY_DIR}/thirdeye.desktop")
endif()

# Compiler settings
if (CMAKE_COMPILER_IS_GNUCCXX)
    add_definitions (-Wall -Wextra -Wno-unused-parameter -Wno-reorder -std=c++11 -pedantic -Wno-long-long)

    # Set our debugging standards
    if(DEBUG)
        add_definitions (-ggdb3 -O0)
    endif(DEBUG)

    execute_process(COMMAND ${CMAKE_C_COMPILER} -dumpversion
                OUTPUT_VARIABLE GCC_VERSION)
    if ("${GCC_VERSION}" VERSION_GREATER 4.6 OR "${GCC_VERSION}" VERSION_EQUAL 4.6)
        add_definitions (-Wno-unused-but-set-parameter)
    endif("${GCC_VERSION}" VERSION_GREATER 4.6 OR "${GCC_VERSION}" VERSION_EQUAL 4.6)
endif (CMAKE_COMPILER_IS_GNUCCXX)

if (MSVC)
    # Set warning level 0 for SYSTEM (/external:I) headers -- this is what makes
    # thirdeye_mark_system_includes() actually silence third-party header
    # warnings (CLI11, GoogleTest, ...) while keeping warnings for our own code.
    add_compile_options(/external:W0)
endif()

if(DPKG_PROGRAM)
    SET(CMAKE_INSTALL_PREFIX "/usr")

    if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/.git")
        exec_program("git" ${CMAKE_CURRENT_SOURCE_DIR} ARGS "describe" OUTPUT_VARIABLE GIT_VERSION )
        STRING(REGEX REPLACE "THIRDEYE-" "" VERSION_STRING "${GIT_VERSION}")
        exec_program("git" ARGS "config --get user.name" OUTPUT_VARIABLE GIT_NAME )
        exec_program("git" ARGS "config --get user.email" OUTPUT_VARIABLE GIT_EMAIL)
        set(PACKAGE_MAINTAINER "${GIT_NAME} <${GIT_EMAIL}>")
    else()
        set(VERSION_STRING "${THIRDEYE_VERSION}")
        set(PACKAGE_MAINTAINER "unknown")
    endif()

    #Install icon and desktop file
    INSTALL(FILES "${Thirdeye_BINARY_DIR}/thirdeye.desktop" DESTINATION "share/applications/" PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ COMPONENT "thirdeye")
    INSTALL(FILES "${Thirdeye_SOURCE_DIR}/apps/launcher/resources/images/thirdeye.png" DESTINATION "share/pixmaps/" PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ COMPONENT "thirdeye")

    #Install resources
    INSTALL(DIRECTORY "${Thirdeye_BINARY_DIR}/resources" DESTINATION "share/games/thirdeye/" FILE_PERMISSIONS OWNER_READ GROUP_READ WORLD_READ COMPONENT "Resources")
    INSTALL(DIRECTORY DESTINATION "share/games/thirdeye/data/" COMPONENT "Resources")

    SET(CPACK_GENERATOR "DEB")
    SET(CPACK_PACKAGE_NAME "Thirdeye")
    SET(CPACK_DEBIAN_PACKAGE_HOMEPAGE "http://www.mindwerks.net")
    SET(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    SET(CPACK_DEBIAN_PACKAGE_MAINTAINER "${PACKAGE_MAINTAINER}")
    SET(CPACK_DEBIAN_PACKAGE_DESCRIPTION "Thirdeye is an attempt at recreating the engine for the popular role-playing games produced by Strategic Simulations, Inc. You need to own and install the original games for Thirdeye to work.")
    SET(CPACK_DEBIAN_PACKAGE_NAME "Thirdeye")
    SET(CPACK_DEBIAN_PACKAGE_VERSION "${VERSION_STRING}")
    SET(CPACK_PACKAGE_EXECUTABLES "thirdeye;Thirdeye thirdeye-launcher;ThirdeyeLauncher daesop;Daesop;runtests;RunTests")
    SET(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6 (>= 2.11.2), libstdc++6 (>= 4.4.5), libuuid1 (>= 2.17.2)")
    SET(CPACK_DEBIAN_PACKAGE_SECTION "Games")
    string(TOLOWER "${CPACK_PACKAGE_NAME}" CPACK_PACKAGE_NAME_LOWERCASE)
    execute_process(
        COMMAND ${DPKG_PROGRAM} --print-architecture
        OUTPUT_VARIABLE CPACK_DEBIAN_PACKAGE_ARCHITECTURE
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME_LOWERCASE}_${CPACK_DEBIAN_PACKAGE_VERSION}_${CPACK_DEBIAN_PACKAGE_ARCHITECTURE}")


    include(CPack)
endif(DPKG_PROGRAM)

if(WIN32)
    # Ninja + single-config MSVC drop binaries directly into ${BINARY_DIR},
    # not into a Release/ subdir. Glob DLLs at install-time (not configure)
    # so vcpkg runtime DLLs (SDL3, OpenAL32, libWildMidi) + windeployqt's
    # Qt6 DLLs staged into build/ just before `cpack` runs both get picked
    # up. windeployqt also drops Qt plugins into subdirs (platforms/,
    # styles/, imageformats/, iconengines/, networkinformation/, tls/) —
    # sweep any that exist.
    INSTALL(CODE "
        file(GLOB _thirdeye_dlls \"${Thirdeye_BINARY_DIR}/*.dll\")
        file(INSTALL \${_thirdeye_dlls} DESTINATION \"\${CMAKE_INSTALL_PREFIX}\")
        foreach(_qt_plugin_subdir platforms styles imageformats iconengines networkinformation tls generic)
            if(IS_DIRECTORY \"${Thirdeye_BINARY_DIR}/\${_qt_plugin_subdir}\")
                file(INSTALL \"${Thirdeye_BINARY_DIR}/\${_qt_plugin_subdir}\" DESTINATION \"\${CMAKE_INSTALL_PREFIX}\")
            endif()
        endforeach()
    ")
    INSTALL(FILES
        "${Thirdeye_SOURCE_DIR}/README.md"
        "${Thirdeye_SOURCE_DIR}/LICENSE.txt"
        "${Thirdeye_BINARY_DIR}/thirdeye.exe"
        DESTINATION ".")
    if(BUILD_DAESOP)
        INSTALL(FILES "${Thirdeye_BINARY_DIR}/daesop.exe" DESTINATION ".")
    endif()
    if(BUILD_LAUNCHER)
        INSTALL(FILES "${Thirdeye_BINARY_DIR}/thirdeye-launcher.exe" DESTINATION ".")
    endif()
    # ponytail: no ${BINARY_DIR}/resources exists — add_thirdeye_dir() is a
    # source-file group macro, nothing populates a runtime tree. The Linux
    # DPKG + macOS APPLE blocks reference it too and silently install an
    # empty dir; not worth fixing there for a Windows-only PR.

    SET(CPACK_GENERATOR "NSIS")
    SET(CPACK_PACKAGE_NAME "Thirdeye")
    SET(CPACK_PACKAGE_FILE_NAME "Thirdeye-${THIRDEYE_VERSION}-win64")
    SET(CPACK_PACKAGE_INSTALL_DIRECTORY "Thirdeye")
    SET(CPACK_PACKAGE_VENDOR "Mindwerks.net")
    SET(CPACK_PACKAGE_VERSION ${THIRDEYE_VERSION})
    SET(CPACK_PACKAGE_VERSION_MAJOR ${THIRDEYE_VERSION_MAJOR})
    SET(CPACK_PACKAGE_VERSION_MINOR ${THIRDEYE_VERSION_MINOR})
    SET(CPACK_PACKAGE_VERSION_PATCH ${THIRDEYE_VERSION_RELEASE})
    if(BUILD_LAUNCHER)
        SET(CPACK_PACKAGE_EXECUTABLES "thirdeye-launcher;Thirdeye Launcher;thirdeye;Thirdeye")
        SET(CPACK_NSIS_INSTALLED_ICON_NAME "thirdeye-launcher.exe")
    else()
        SET(CPACK_PACKAGE_EXECUTABLES "thirdeye;Thirdeye")
        SET(CPACK_NSIS_INSTALLED_ICON_NAME "thirdeye.exe")
    endif()
    SET(CPACK_NSIS_CREATE_ICONS_EXTRA "CreateShortCut '\$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\Readme.lnk' '\$INSTDIR\\\\README.md'")
    SET(CPACK_NSIS_DELETE_ICONS_EXTRA "
        !insertmacro MUI_STARTMENU_GETFOLDER Application $MUI_TEMP
        Delete \\\"$SMPROGRAMS\\\\$MUI_TEMP\\\\Readme.lnk\\\"
        ")
    SET(CPACK_RESOURCE_FILE_README "${Thirdeye_SOURCE_DIR}/README.md")
    SET(CPACK_RESOURCE_FILE_LICENSE "${Thirdeye_SOURCE_DIR}/LICENSE.txt")
    SET(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")
    SET(CPACK_NSIS_DISPLAY_NAME "Thirdeye ${THIRDEYE_VERSION}")
    SET(CPACK_NSIS_PACKAGE_NAME "Thirdeye ${THIRDEYE_VERSION}")
    SET(CPACK_NSIS_HELP_LINK "http://www.mindwerks.net")
    SET(CPACK_NSIS_URL_INFO_ABOUT "http://www.mindwerks.net")
    SET(CPACK_NSIS_MUI_ICON "${Thirdeye_SOURCE_DIR}/apps/launcher/resources/images/thirdeye.ico")
    SET(CPACK_NSIS_MUI_UNIICON "${Thirdeye_SOURCE_DIR}/apps/launcher/resources/images/thirdeye.ico")
    SET(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    SET(CPACK_NSIS_MODIFY_PATH OFF)
    SET(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")

    include(CPack)

    # Parallel to `dmg` on macOS: `cmake --build build --target nsis`
    # produces build/Thirdeye-<version>-win64.exe via cpack.
    add_custom_target(nsis
        COMMAND ${CMAKE_CPACK_COMMAND} -G NSIS --config "${Thirdeye_BINARY_DIR}/CPackConfig.cmake"
        WORKING_DIRECTORY "${Thirdeye_BINARY_DIR}"
        COMMENT "Producing NSIS installer via cpack"
        VERBATIM
    )
endif(WIN32)

# Components
add_subdirectory (components)

# Apps and tools
add_subdirectory( apps/thirdeye )
if (BUILD_LAUNCHER)
   add_subdirectory( apps/launcher )
endif()
if (BUILD_DAESOP)
   add_subdirectory( apps/daesop )
endif()
if (BUILD_ARC)
   add_subdirectory( apps/arc )
endif()
if (UNIT_TESTS)
   enable_testing()
   add_subdirectory( apps/tests )
endif()

if (WIN32)
  if (MSVC)
    if (USE_DEBUG_CONSOLE)
      set_target_properties(thirdeye PROPERTIES LINK_FLAGS_DEBUG "/SUBSYSTEM:CONSOLE")
      set_target_properties(thirdeye PROPERTIES LINK_FLAGS_RELWITHDEBINFO "/SUBSYSTEM:CONSOLE")
      set_target_properties(thirdeye PROPERTIES COMPILE_DEFINITIONS_DEBUG "_CONSOLE")
    else()
      # Turn off debug console, debug output will be written to visual studio output instead
      set_target_properties(thirdeye PROPERTIES LINK_FLAGS_DEBUG "/SUBSYSTEM:WINDOWS")
      set_target_properties(thirdeye PROPERTIES LINK_FLAGS_RELWITHDEBINFO "/SUBSYSTEM:WINDOWS")
    endif()

    # Release builds use the debug console
    set_target_properties(thirdeye PROPERTIES LINK_FLAGS_RELEASE "/SUBSYSTEM:CONSOLE")
    set_target_properties(thirdeye PROPERTIES COMPILE_DEFINITIONS_RELEASE "_CONSOLE")
    set_target_properties(thirdeye PROPERTIES LINK_FLAGS_MINSIZEREL "/SUBSYSTEM:CONSOLE")

    # Play a bit with the warning levels

    set(WARNINGS "/Wall") # Since windows can only disable specific warnings, not enable them

    set(WARNINGS_DISABLE
        # Warnings that aren't enabled normally and don't need to be enabled
        # They're unneeded and sometimes completely retarded warnings that /Wall enables
        # Not going to bother commenting them as they tend to warn on every standard library files
        4061 4263 4264 4266 4350 4371 4514 4548 4571 4610 4619 4623 4625 4626 4628 4640 4668 4710 4711 4820 4826 4917 4946

        # Warnings that are thrown on standard libraries and not thirdeye
        4347 # Non-template function with same name and parameter count as template function
        4365 # Variable signed/unsigned mismatch
        4510 4512 # Unable to generate copy constructor/assignment operator as it's not public in the base
        4706 # Assignment in conditional expression
        4738 # Storing 32-bit float result in memory, possible loss of performance
        4986 # Undocumented warning that occurs in the crtdbg.h file
        4996 # Function was declared deprecated

		# cause by ogre extensivly
		4193 # #pragma warning(pop) : no matching '#pragma warning(push)'
		4251 # class 'XXXX' needs to have dll-interface to be used by clients of class 'YYYY'
		4275 # non dll-interface struct 'XXXX' used as base for dll-interface class 'YYYY'

        # thirdeye specific warnings
        4099 # Type mismatch, declared class or struct is defined with other type
        4100 # Unreferenced formal parameter (-Wunused-parameter)
        4127 # Conditional expression is constant
        4242 # Storing value in a variable of a smaller type, possible loss of data
        4244 # Storing value of one type in variable of another (size_t in int, for example)
        4305 # Truncating value (double to float, for example)
        4309 # Variable overflow, trying to store 128 in a signed char for example
        4355 # Using 'this' in member initialization list
        4701 # Potentially uninitialized local variable used
		4800 # Boolean optimization warning, e.g. myBool = (myInt != 0) instead of myBool = myInt

        # Noise from /Wall on system / third-party headers, or intentional patterns
        4464 # Relative include path contains '..' (cosmetic)
        4774 # Format string is not a string literal (intentional: runtime format args)
        5039 # Throwing function passed to extern "C" (Windows SDK headers)
        5045 # Spectre mitigation insertion note (informational, requires /Qspectre)
        5262 # Implicit fall-through (occurs inside SDL headers)
        )

    foreach(d ${WARNINGS_DISABLE})
        set(WARNINGS "${WARNINGS} /wd${d}")
    endforeach(d)

    set_target_properties(components PROPERTIES COMPILE_FLAGS ${WARNINGS})
    if (BUILD_LAUNCHER)
		set_target_properties(thirdeye-launcher PROPERTIES COMPILE_FLAGS ${WARNINGS})
	endif (BUILD_LAUNCHER)
    if (BUILD_DAESOP)
        set_target_properties(daesop PROPERTIES COMPILE_FLAGS ${WARNINGS})
    endif (BUILD_DAESOP)
    if (UNIT_TESTS)
        set_target_properties(runtests PROPERTIES COMPILE_FLAGS ${WARNINGS})
    endif(UNIT_TESTS)
    set_target_properties(thirdeye PROPERTIES COMPILE_FLAGS ${WARNINGS})
  endif(MSVC)

  # Same for MinGW
  if (MINGW)
    if (USE_DEBUG_CONSOLE)
      set_target_properties(thirdeye PROPERTIES LINK_FLAGS_DEBUG "-Wl,-subsystem,console")
      set_target_properties(thirdeye PROPERTIES LINK_FLAGS_RELWITHDEBINFO "-Wl,-subsystem,console")
      set_target_properties(thirdeye PROPERTIES COMPILE_DEFINITIONS_DEBUG "_CONSOLE")
    else(USE_DEBUG_CONSOLE)
      set_target_properties(thirdeye PROPERTIES LINK_FLAGS_DEBUG "-Wl,-subsystem,windows")
      set_target_properties(thirdeye PROPERTIES LINK_FLAGS_RELWITHDEBINFO "-Wl,-subsystem,windows")
    endif(USE_DEBUG_CONSOLE)

    set_target_properties(thirdeye PROPERTIES LINK_FLAGS_RELEASE "-Wl,-subsystem,console")
    set_target_properties(thirdeye PROPERTIES LINK_FLAGS_MINSIZEREL "-Wl,-subsystem,console")
    set_target_properties(thirdeye PROPERTIES COMPILE_DEFINITIONS_RELEASE "_CONSOLE")
  endif(MINGW)

  # TODO: At some point release builds should not use the console but rather write to a log file
  #set_target_properties(thirdeye PROPERTIES LINK_FLAGS_RELEASE "/SUBSYSTEM:WINDOWS")
  #set_target_properties(thirdeye PROPERTIES LINK_FLAGS_MINSIZEREL "/SUBSYSTEM:WINDOWS")
endif()

# Apple bundling
if (APPLE)
    set(INSTALL_SUBDIR THIRDEYE)

    install(DIRECTORY "${APP_BUNDLE_DIR}" USE_SOURCE_PERMISSIONS DESTINATION "${INSTALL_SUBDIR}" COMPONENT Runtime)
    install(DIRECTORY "${Thirdeye_BINARY_DIR}/resources" DESTINATION "${INSTALL_SUBDIR}" COMPONENT Runtime)

    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_PACKAGE_VERSION ${THIRDEYE_VERSION})
    set(CPACK_PACKAGE_VERSION_MAJOR ${THIRDEYE_VERSION_MAJOR})
    set(CPACK_PACKAGE_VERSION_MINOR ${THIRDEYE_VERSION_MINOR})
    set(CPACK_PACKAGE_VERSION_PATCH ${THIRDEYE_VERSION_RELEASE})
    set(APPS "\${CMAKE_INSTALL_PREFIX}/${INSTALL_SUBDIR}/${APP_BUNDLE_NAME}")
    set(PLUGINS "")
    set(ABSOLUTE_PLUGINS "")

    foreach (PLUGIN ${USED_OGRE_PLUGINS})
        get_filename_component(PLUGIN_ABS ${PLUGIN} REALPATH)
        set(ABSOLUTE_PLUGINS ${PLUGIN_ABS} ${ABSOLUTE_PLUGINS})
    endforeach ()

    set(PLUGIN_INSTALL_BASE "\${CMAKE_INSTALL_PREFIX}/${INSTALL_SUBDIR}/${APP_BUNDLE_NAME}/Contents/Plugins")
    install(FILES ${ABSOLUTE_PLUGINS} DESTINATION "${INSTALL_SUBDIR}/${APP_BUNDLE_NAME}/Contents/Plugins" COMPONENT Runtime)
    foreach (PLUGIN ${ABSOLUTE_PLUGINS})
        get_filename_component(PLUGIN_RELATIVE ${PLUGIN} NAME)
        set(PLUGINS ${PLUGINS} "${PLUGIN_INSTALL_BASE}/${PLUGIN_RELATIVE}")
    endforeach ()

    #For now, search unresolved dependencies only in default system paths, so if you put unresolveable (i.e. with @executable_path in id name) lib or framework somewhere else, it would fail
    set(DIRS "")

    # Overriding item resolving during installation, it needed if
    # some library already has been "fixed up", i.e. its id name contains @executable_path,
    # but library is not embedded in bundle. For example, it's Ogre.framework from Ogre SDK.
    # Current implementation of GetPrerequsities/BundleUtilities doesn't handle that case.
    #
    # Current limitations:
    #   1. Handles only frameworks, not simple libs
    INSTALL(CODE "
        set(CMAKE_FIND_LIBRARY_PREFIXES ${CMAKE_FIND_LIBRARY_PREFIXES})
        set(CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
        set(CMAKE_SYSTEM_FRAMEWORK_PATH ${CMAKE_SYSTEM_FRAMEWORK_PATH})

        set(THIRDEYE_RESOLVED_ITEMS \"\")

        function(gp_resolve_item_override context item exepath dirs resolved_item_var resolved_var)
            if(item MATCHES \"@executable_path\" AND NOT \${\${resolved_var}})
                if (item MATCHES \"Frameworks\") # if it is a framework
                    # get last segment of path
                    get_filename_component(fname \"\${item}\" NAME_WE)
                    find_library(ri NAMES \${fname} PATHS \${exepath} \${dirs} \${CMAKE_SYSTEM_FRAMEWORK_PATH})
                    if (ri)
                        string(REGEX REPLACE \"^.*/Frameworks/.*\\\\.framework\" \"\" item_part \${item})
                        set(ri \"\${ri}\${item_part}\")
                        set(\${resolved_item_var} \${ri} PARENT_SCOPE)
                        set(\${resolved_var} 1 PARENT_SCOPE)
                    endif()
                else()
                    # code path for standard (non-framework) libs (ogre & qt pugins)
                    get_filename_component(fname \"\${item}\" NAME_WE)
                    string(REGEX REPLACE \"^lib\" \"\" fname \${fname})
                    find_library(ri NAMES \${fname} PATHS \${exepath} \${dirs} /usr/lib /usr/local/lib)
                    if (ri)
                        set(\${resolved_item_var} \${ri} PARENT_SCOPE)
                        set(\${resolved_var} 1 PARENT_SCOPE)
                    endif ()
                endif()
            endif()
        endfunction(gp_resolve_item_override)

        cmake_policy(SET CMP0009 OLD)
        set(BU_CHMOD_BUNDLE_ITEMS ON)
        include(BundleUtilities)
        fixup_bundle(\"${APPS}\" \"${PLUGINS}\" \"${DIRS}\")
        " COMPONENT Runtime)
        include(CPack)

    # `dmg` target: build a distributable .dmg. Needs BUILD_LAUNCHER=ON so the
    # bundle's CFBundleExecutable (thirdeye-launcher) actually exists.
    #
    # We stage a copy of the .app into build/dmg-staging/, strip dev-only tools
    # (arc, daesop, runtests, daesoptests, bytecode.def), let macdeployqt bundle
    # Qt frameworks + platform plugins into the staged copy, then hdiutil-wrap
    # it. Staging keeps the build tree usable after packaging — daesop still
    # has its bytecode.def next to it in build/thirdeye.app/.
    if (BUILD_LAUNCHER)
        # Qt's imported targets (Qt6::qmake) are created inside apps/launcher's
        # scope by find_package(Qt6), so they're not guaranteed visible here.
        # Qt6_DIR is a CACHE variable (globally visible) set to the dir holding
        # Qt6Config.cmake, i.e. <qt>/lib/cmake/Qt6 -- three levels up is <qt>,
        # and macdeployqt ships in <qt>/bin.
        find_program(MACDEPLOYQT_EXE macdeployqt
                     HINTS "${Qt6_DIR}/../../../bin"
                     REQUIRED)

        set(_dmg_stage    "${Thirdeye_BINARY_DIR}/dmg-staging")
        set(_dmg_stage_app "${_dmg_stage}/thirdeye.app")
        set(_dmg_path     "${Thirdeye_BINARY_DIR}/Thirdeye-${THIRDEYE_VERSION}.dmg")

        add_custom_target(dmg
            COMMAND ${CMAKE_COMMAND} -E rm -f  "${_dmg_path}"
            COMMAND ${CMAKE_COMMAND} -E rm -rf "${_dmg_stage}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${_dmg_stage}"
            # cp -RP preserves the Versions/Current symlinks inside .framework
            # bundles; cmake -E copy_directory materializes them as directories,
            # which trips codesign with "bundle format is ambiguous".
            COMMAND cp -RP "${APP_BUNDLE_DIR}" "${_dmg_stage_app}"
            # Strip dev-only binaries + bytecode.def from the staged copy.
            COMMAND ${CMAKE_COMMAND} -E rm -f
                    "${_dmg_stage_app}/Contents/MacOS/arc"
                    "${_dmg_stage_app}/Contents/MacOS/daesop"
                    "${_dmg_stage_app}/Contents/MacOS/daesoptests"
                    "${_dmg_stage_app}/Contents/MacOS/runtests"
                    "${_dmg_stage_app}/Contents/MacOS/bytecode.def"
            # Wipe any bundled Qt state that may have been left in the build
            # tree by earlier deploy runs. Otherwise macdeployqt sees "file
            # exists" and skips, leaving stale frameworks whose plugins
            # collide with fresh Homebrew Qt at dev-runtime.
            COMMAND ${CMAKE_COMMAND} -E rm -rf
                    "${_dmg_stage_app}/Contents/Frameworks"
                    "${_dmg_stage_app}/Contents/PlugIns"
                    "${_dmg_stage_app}/Contents/Resources/qt.conf"
            # macdeployqt's default target is the bundle's main executable
            # (thirdeye-launcher, which links Qt). Pass -executable= for the
            # engine so its non-Qt deps (SDL3, WildMIDI, OpenAL) also get
            # copied into Contents/Frameworks/ and their install_names
            # rewritten to @executable_path/../Frameworks/... — otherwise the
            # engine can't load on a machine without Homebrew.
            COMMAND ${MACDEPLOYQT_EXE} "${_dmg_stage_app}"
                    -executable=${_dmg_stage_app}/Contents/MacOS/thirdeye
                    -verbose=1
            # macdeployqt scans Homebrew qtbase's plugin dir and copies
            # everything it finds into the bundle, then walks each plugin's
            # deps. QtPdf / QtSvg / QtVirtualKeyboard live in *separate*
            # Homebrew formulae we don't install, so the walk fails on those
            # plugins and prints "ERROR: Cannot resolve rpath ..." — but the
            # plugins are still copied (broken). Strip them post-hoc so we
            # don't ship dylibs that will fail to load at runtime. Our
            # launcher uses none of these plugins. `rm -f` no-ops on absent
            # files, so this stays safe if macdeployqt gets smarter later.
            COMMAND ${CMAKE_COMMAND} -E rm -f
                    "${_dmg_stage_app}/Contents/PlugIns/imageformats/libqpdf.dylib"
                    "${_dmg_stage_app}/Contents/PlugIns/imageformats/libqsvg.dylib"
                    "${_dmg_stage_app}/Contents/PlugIns/iconengines/libqsvgicon.dylib"
                    "${_dmg_stage_app}/Contents/PlugIns/platforminputcontexts/libqtvirtualkeyboardplugin.dylib"
            COMMAND hdiutil create
                    -volname Thirdeye
                    -srcfolder "${_dmg_stage_app}"
                    -ov -format UDZO
                    "${_dmg_path}"
            DEPENDS thirdeye thirdeye-launcher
            WORKING_DIRECTORY "${Thirdeye_BINARY_DIR}"
            COMMENT "Bundling Qt + producing ${_dmg_path}"
            VERBATIM
        )
    endif()
endif (APPLE)

if (NOT WIN32 AND NOT DPKG_PROGRAM AND NOT APPLE)
    ## Non Debian based Linux building
    # paths
    set(BINDIR "${CMAKE_INSTALL_PREFIX}/usr/bin" CACHE PATH "Where to install binaries")
    set(DATAROOTDIR "${CMAKE_INSTALL_PREFIX}/share" CACHE PATH "Sets the root of data directories to a non-default location")
    set(DATADIR "${DATAROOTDIR}/thirdeye" CACHE PATH "Sets the thirdeye data directories to a non-default location")
    set(DOCDIR "${DATAROOTDIR}/doc/thirdeye" CACHE PATH "Sets the doc directory to a non-default location.")
    set(MANDIR "${DATAROOTDIR}/man" CACHE PATH "Where to install manpages")
    set(SYSCONFDIR "${CMAKE_INSTALL_PREFIX}/etc/thirdeye" CACHE PATH "Set config dir")
    set(ICONDIR "${DATAROOTDIR}/pixmaps" CACHE PATH "Set icon dir")

    # Install binaries
    INSTALL(PROGRAMS "${Thirdeye_BINARY_DIR}/thirdeye" DESTINATION "${BINDIR}" )
    IF(BUILD_LAUNCHER)
        INSTALL(PROGRAMS "${Thirdeye_BINARY_DIR}/thirdeye-launcher" DESTINATION "${BINDIR}" )
    ENDIF(BUILD_LAUNCHER)
    IF(BUILD_DAESOP)
        INSTALL(PROGRAMS "${Thirdeye_BINARY_DIR}/daesop" DESTINATION "${BINDIR}" )
    ENDIF(BUILD_DAESOP)  
    if(UNIT_TESTS)
        INSTALL(PROGRAMS "${Thirdeye_BINARY_DIR}/runtests" DESTINATION "${BINDIR}" )
    ENDIF(UNIT_TESTS)  
    IF(BUILD_ARC)
        INSTALL(PROGRAMS "${Thirdeye_BINARY_DIR}/arc" DESTINATION "${BINDIR}" )
    ENDIF(BUILD_ARC) 

    # Install icon and .desktop
    INSTALL(FILES "${Thirdeye_SOURCE_DIR}/apps/launcher/resources/images/thirdeye.png" DESTINATION "${ICONDIR}")
    INSTALL(FILES "${Thirdeye_BINARY_DIR}/thirdeye.desktop" DESTINATION "${DATAROOTDIR}/applications")

    # Install resources
    INSTALL(DIRECTORY "${Thirdeye_BINARY_DIR}/resources" DESTINATION "${DATADIR}" )
    IF(BUILD_LAUNCHER)
        INSTALL(FILES "${Thirdeye_BINARY_DIR}/launcher.qss" DESTINATION "${DATADIR}/resources" )
    ENDIF(BUILD_LAUNCHER)
endif(NOT WIN32 AND NOT DPKG_PROGRAM AND NOT APPLE)
