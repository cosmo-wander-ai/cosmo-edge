#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cassert>
#include <string>

#include "flow/detect/AxeraTrustedDirectories.h"

int main(int argc, char** argv) {
    assert(argc == 2);
    const std::string parent = argv[1];
    using namespace cosmo::axera_trusted_directories;
    Fd root{OpenTrustedAbsoluteDirectory(parent)};
    assert(root.value >= 0);
    assert(TrustedDirectory(root.value, ::geteuid()));
    assert(OpenTrustedAbsoluteDirectory(parent + "/../x") < 0);
    assert(::mkdirat(root.value, "world-writable", 0777) == 0);
    assert(::fchmodat(root.value, "world-writable", 0777, 0) == 0);
    assert(OpenTrustedAbsoluteDirectory(parent + "/world-writable") < 0);
    assert(::symlinkat("world-writable", root.value, "alias") == 0);
    assert(OpenTrustedAbsoluteDirectory(parent + "/alias") < 0);
    assert(::unlinkat(root.value, "alias", 0) == 0);
    assert(::unlinkat(root.value, "world-writable", AT_REMOVEDIR) == 0);
}
