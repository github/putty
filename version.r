/*
 * PuTTY version resource
 * (separated because it gets rebuilt for each architecture)
 */

#include "version.h"
#include "Types.r"

#ifndef BUILD_ARCH
#define BUILD_ARCH "unknown"
#endif

resource 'vers' (1, purgeable) {
    PUTTY_VERS_MAJOR_BCD,
    PUTTY_VERS_MINOR_BCD,
    PUTTY_VERS_STATE,
    PUTTY_VERS_PRERELEASE,
    2,			/* Region code (2 = UK) */
    PUTTY_VERS_SHORT_STR,

    PUTTY_VERS_LONG_STR " (" BUILD_ARCH ")\n"
    "Copyright \$a9 Simon Tatham 1999",
};

