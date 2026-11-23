#include <cassert>
#include <string>

#include "flow/detect/NativeRequiredConfiguration.h"

// The real path validator is linked by the engine. This focused test supplies
// only the narrow numeric-code predicate, so it can run without the engine.
namespace cosmo::path {
bool IsSafePathComponent(const std::string& s, size_t max_length) {
    return !s.empty() && s.size() <= max_length && s.find('/') == std::string::npos;
}
}  // namespace cosmo::path

int main(int argc, char** argv) {
    assert(argc == 3);
    assert(cosmo::native_face::ProcessRequires("1001003") == (std::string(argv[1]) == "1"));
    assert(cosmo::native_face::ProcessRequires("9275710") == (std::string(argv[2]) == "1"));
}
