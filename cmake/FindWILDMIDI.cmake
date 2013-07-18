
FIND_PATH(WILDMIDI_INCLUDE_DIR wildmidi_lib.h
  HINTS
  $ENV{WILDMIDIDIR}
  PATH_SUFFIXES include/ include
  PATHS
  ~/Library/Frameworks
  /Library/Frameworks
  /usr/local/include
  /usr/include
  /sw # Fink
  /opt/local # DarwinPorts
  /opt/csw # Blastwave
  /
)

FIND_LIBRARY(WILDMIDI_LIBRARY NAME WildMidi PATHS /usr/lib /usr/lib32  /usr/local/lib /usr/local/lib32)

IF(NOT WILDMIDI_LIBRARY MATCHES "^.*-NOTFOUND")
SET(WILDMIDI_FOUND TRUE CACHE INTERNAL "wildmidi")
ELSE(NOT WILDMIDI_LIBRARY MATCHES "^.*-NOTFOUND")
MESSAGE(STATUS "Could not find WildMidi library")
ENDIF(NOT WILDMIDI_LIBRARY MATCHES "^.*-NOTFOUND")

FIND_PACKAGE_HANDLE_STANDARD_ARGS(WILDMIDI
                                  REQUIRED_VARS WILDMIDI_LIBRARY WILDMIDI_INCLUDE_DIR)