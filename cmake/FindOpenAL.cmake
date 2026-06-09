# FindOpenAL.cmake - search for OpenAL, preferring Homebrew (openal-soft) on macOS
# Sets:
#  OPENAL_LIBRARY
#  OPENAL_INCLUDE_DIR
#  OPENAL_LIBRARIES

if(DEFINED ENV{HOMEBREW_PREFIX})
    set(_hb_prefix $ENV{HOMEBREW_PREFIX})
else()
    set(_hb_prefix "")
endif()

set(_hb_candidates "${_hb_prefix}" "/opt/homebrew" "/usr/local")

set(_search_inc_dirs "")
set(_search_lib_dirs "")
foreach(_p IN LISTS _hb_candidates)
    if(EXISTS "${_p}")
        list(APPEND _search_inc_dirs "${_p}/opt/openal-soft/include" "${_p}/include")
        list(APPEND _search_lib_dirs "${_p}/opt/openal-soft/lib" "${_p}/lib")
    endif()
endforeach()

# Try Homebrew-ish locations first
find_path(OPENAL_INCLUDE_DIR
    NAMES AL/al.h OpenAL/al.h
    PATHS ${_search_inc_dirs}
    NO_DEFAULT_PATH
)

find_library(OPENAL_LIBRARY
    NAMES openal openal-soft libopenal OpenAL
    PATHS ${_search_lib_dirs}
    NO_DEFAULT_PATH
)

# If not found, try macOS frameworks
if(NOT OPENAL_LIBRARY)
    find_library(OPENAL_LIBRARY
        NAMES OpenAL openal
        PATHS /Library/Frameworks /System/Library/Frameworks /Library/Developer/CommandLineTools/SDKs
        PATH_SUFFIXES Frameworks
    )
endif()

# Final fallback: search default paths (covers vcpkg, MSYS2, and system installs).
# vcpkg's openal-soft import library is named OpenAL32 on Windows.
if(NOT OPENAL_LIBRARY)
    find_library(OPENAL_LIBRARY NAMES OpenAL openal OpenAL32 al)
endif()

if(NOT OPENAL_INCLUDE_DIR)
    find_path(OPENAL_INCLUDE_DIR NAMES AL/al.h OpenAL/al.h)
endif()

if(OPENAL_LIBRARY)
    set(OPENAL_LIBRARIES ${OPENAL_LIBRARY})
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(OpenAL DEFAULT_MSG OPENAL_LIBRARY OPENAL_INCLUDE_DIR)

mark_as_advanced(OPENAL_LIBRARY OPENAL_INCLUDE_DIR OPENAL_LIBRARIES)
