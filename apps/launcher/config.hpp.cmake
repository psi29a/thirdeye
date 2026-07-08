#ifndef CONFIG_H
#define CONFIG_H

#define LAUNCHER_VERSION_MAJOR @THIRDEYE_VERSION_MAJOR@
#define LAUNCHER_VERSION_MINOR @THIRDEYE_VERSION_MINOR@
#define LAUNCHER_VERSION_RELEASE @THIRDEYE_VERSION_RELEASE@
#define LAUNCHER_VERSION "@THIRDEYE_VERSION@"

// Git build info, baked at configure time.
// LAUNCHER_GIT_TAG:    non-empty only when HEAD sits exactly on a tag
//                      (a release build); e.g. "thirdeye-0.87.0".
// LAUNCHER_GIT_COMMIT: short hash; empty when built from a tarball.
#define LAUNCHER_GIT_TAG "@LAUNCHER_GIT_TAG@"
#define LAUNCHER_GIT_COMMIT "@LAUNCHER_GIT_COMMIT@"

#endif
