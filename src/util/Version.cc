// Version information for the application.

#include "util/Version.h"

#include <cstdio>

namespace cosmo::util {

const char* GetVersion() {
    static char buf[128];
    static bool once = (snprintf(buf, sizeof(buf), "Version %d.%d.%d.%d %s", kVersionMajor, kVersionMinor,
                                 kVersionPatch, kVersionBuild, __DATE__),
                        true);
    (void)once;
    return buf;
}

const char* GetAbbrVersion() {
    static char buf[64];
    static bool once = (snprintf(buf, sizeof(buf), "V%d.%d.%d.%d", kVersionMajor, kVersionMinor,
                                 kVersionPatch, kVersionBuild),
                        true);
    (void)once;
    return buf;
}

}  // namespace cosmo::util
