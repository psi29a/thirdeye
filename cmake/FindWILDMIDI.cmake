
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

FIND_LIBRARY(WILDMIDI_LIB NAME WildMidi PATHS /usr/lib /usr/lib32  /usr/local/lib /usr/local/lib32)

IF(NOT WILDMIDI_LIB MATCHES "^.*-NOTFOUND")
SET(WILDMIDI_FOUND TRUE CACHE INTERNAL "wildmidi")
MESSAGE(STATUS "Found WildMidi: ${WILDMIDI_LIB}")
ELSE(NOT WILDMIDI_LIB MATCHES "^.*-NOTFOUND")
MESSAGE(STATUS "Could not find WildMidi library")
ENDIF(NOT WILDMIDI_LIB MATCHES "^.*-NOTFOUND")
