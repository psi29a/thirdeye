/*
 * Cross-platform shims for arc's POSIX-flavoured C source.
 * The original 1992 sources target DOS+glibc; this header maps the bits
 * that don't exist on MSVC/Windows to their underscore-prefixed CRT
 * equivalents so the same .cpp files compile everywhere.
 */
#ifndef ARC_COMPAT_HPP_INCLUDED
#define ARC_COMPAT_HPP_INCLUDED

#ifdef _WIN32
  #include <io.h>
  #include <process.h>
  #include <string.h>
  // MSVC's CRT spells these with a leading underscore; the unprefixed
  // POSIX names are available as deprecated aliases, but routing through
  // macros avoids the deprecation noise on every call site.
  #define strcasecmp  _stricmp
  #define strncasecmp _strnicmp
  #define unlink      _unlink
#else
  #include <unistd.h>
  #include <strings.h>
#endif

#endif
