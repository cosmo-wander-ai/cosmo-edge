// UuidUtil — Uuid Util implementation.

#include "util/UuidUtil.h"

#include "uuid.h"

namespace cosmo::util {

constexpr std::size_t kUuidBufferSize = 37;  // 36 chars + null terminator

std::string GenerateUUID() {
    uuid_t uu;
    char buf[kUuidBufferSize] = {};
    uuid_generate(uu);
    uuid_unparse(uu, buf);
    return buf;
}

}  // namespace cosmo::util
