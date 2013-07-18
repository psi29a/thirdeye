# - Try to find ALSA
# Once done, this will define
#
#  ALSA_FOUND - system has ALSA (GL and GLU)
#  ALSA_INCLUDE_DIRS - the ALSA include directories
#  ALSA_LIBRARIES - link these to use ALSA
#  ALSA_GL_LIBRARY - only GL
#  ALSA_GLU_LIBRARY - only GLU
#
# See documentation on how to write CMake scripts at
# http://www.cmake.org/Wiki/CMake:How_To_Find_Libraries

FIND_PATH(ALSA_INCLUDE_DIR
  NAMES alsa/version.h
  HINTS
  $ENV{ALSADIR}
  PATH_SUFFIXES include/ include
  PATHS ${ALSA_PKGCONF_INCLUDE_DIRS}
  ~/Library/Frameworks
  /Library/Frameworks
  /usr/local/include
  /usr/include
  /sw # Fink
  /opt/local # DarwinPorts
  /opt/csw # Blastwave
  /  
)

FIND_LIBRARY(ALSA_LIBRARY
  NAMES asound
  PATHS ${ALSA_PKGCONF_LIBRARY_DIRS} /usr/lib /usr/lib32  /usr/local/lib /usr/local/lib32
)

# Extract the version number
IF(ALSA_INCLUDE_DIR)
file(READ "${ALSA_INCLUDE_DIR}/alsa/version.h" _ALSA_VERSION_H_CONTENTS)
string(REGEX REPLACE ".*#define SND_LIB_VERSION_STR[ \t]*\"([^\n]*)\".*" "\\1" ALSA_VERSION "${_ALSA_VERSION_H_CONTENTS}")
ENDIF(ALSA_INCLUDE_DIR)

IF(ALSA_LIBRARY)
SET(ALSA_FOUND TRUE CACHE INTERNAL "alsa")
ELSE(ALSA_LIBRARY)
MESSAGE(STATUS "Could not find alsa library")
ENDIF(ALSA_LIBRARY)

set(ALSA_PROCESS_INCLUDES ALSA_INCLUDE_DIR)
set(ALSA_PROCESS_LIBS ALSA_LIBRARY)

FIND_PACKAGE_HANDLE_STANDARD_ARGS(ALSA
                                  REQUIRED_VARS ALSA_LIBRARY ALSA_INCLUDE_DIR)