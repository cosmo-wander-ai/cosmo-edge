// Network to local byte order conversion utility.
#include "util/NToL.h"

#include <arpa/inet.h>

#include <cstring>

namespace cosmo::util {

void LtonConvert(void* data, size_t length) {
    if (!data) {
        return;
    }
    auto* bytes = static_cast<uint8_t*>(data);
    for (size_t idx = 0; idx < length; ++idx) {
        uint32_t tmp;
        std::memcpy(&tmp, bytes + idx * sizeof(uint32_t), sizeof(uint32_t));
        tmp = htonl(tmp);
        std::memcpy(bytes + idx * sizeof(uint32_t), &tmp, sizeof(uint32_t));
    }
}

}  // namespace cosmo::util
