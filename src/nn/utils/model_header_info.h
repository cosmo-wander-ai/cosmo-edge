#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace cosmo::nn {

inline constexpr std::array<char, 4> kPlainNnHeaderMagic = {'C', 'E', 'N', 'N'};
inline constexpr uint16_t kPlainNnHeaderVersion          = 1;
inline constexpr size_t kPlainNnMaxModelCount            = 8;
inline constexpr size_t kPlainNnMagicOffset              = 0;
inline constexpr size_t kPlainNnVersionOffset            = kPlainNnMagicOffset + kPlainNnHeaderMagic.size();
inline constexpr size_t kPlainNnHeaderSizeOffset         = kPlainNnVersionOffset + sizeof(uint16_t);
inline constexpr size_t kPlainNnModelCountOffset         = kPlainNnHeaderSizeOffset + sizeof(uint16_t);
inline constexpr size_t kPlainNnReservedOffset           = kPlainNnModelCountOffset + sizeof(uint32_t);
inline constexpr size_t kPlainNnModelSizesOffset         = kPlainNnReservedOffset + sizeof(uint32_t);
inline constexpr size_t kPlainNnHeaderSize =
    kPlainNnModelSizesOffset + kPlainNnMaxModelCount * sizeof(uint64_t);
static_assert(kPlainNnHeaderSize <= std::numeric_limits<uint16_t>::max(),
              "plain nn header size must fit uint16_t");

struct PlainNnHeader {
    uint32_t model_count = 0;
    std::array<uint64_t, kPlainNnMaxModelCount> model_sizes{};
};

struct PlainNnHeaderBuildResult {
    bool ok = false;
    std::array<char, kPlainNnHeaderSize> data{};
    std::string error;

    explicit operator bool() const {
        return ok;
    }
};

struct PlainNnHeaderParseResult {
    bool ok = false;
    PlainNnHeader header;
    std::string error;

    explicit operator bool() const {
        return ok;
    }
};

struct PlainNnHeaderValidationResult {
    bool ok = false;
    std::string error;

    explicit operator bool() const {
        return ok;
    }
};

namespace detail {

    inline PlainNnHeaderBuildResult BuildError(const std::string& message) {
        PlainNnHeaderBuildResult result;
        result.error = message;
        return result;
    }

    inline PlainNnHeaderParseResult ParseError(const std::string& message) {
        PlainNnHeaderParseResult result;
        result.error = message;
        return result;
    }

    inline PlainNnHeaderValidationResult ValidationError(const std::string& message) {
        PlainNnHeaderValidationResult result;
        result.error = message;
        return result;
    }

    inline uint16_t ReadUint16Le(const char* data) {
        return static_cast<uint16_t>(static_cast<uint8_t>(data[0])) |
               static_cast<uint16_t>(static_cast<uint16_t>(static_cast<uint8_t>(data[1])) << 8u);
    }

    inline uint32_t ReadUint32Le(const char* data) {
        return static_cast<uint32_t>(static_cast<uint8_t>(data[0])) |
               (static_cast<uint32_t>(static_cast<uint8_t>(data[1])) << 8u) |
               (static_cast<uint32_t>(static_cast<uint8_t>(data[2])) << 16u) |
               (static_cast<uint32_t>(static_cast<uint8_t>(data[3])) << 24u);
    }

    inline uint64_t ReadUint64Le(const char* data) {
        uint64_t value = 0;
        for (size_t i = 0; i < sizeof(uint64_t); i++) {
            value |= static_cast<uint64_t>(static_cast<uint8_t>(data[i])) << (i * 8u);
        }
        return value;
    }

    inline void WriteUint16Le(uint16_t value, char* data) {
        data[0] = static_cast<char>(value & 0xffu);
        data[1] = static_cast<char>((value >> 8u) & 0xffu);
    }

    inline void WriteUint32Le(uint32_t value, char* data) {
        data[0] = static_cast<char>(value & 0xffu);
        data[1] = static_cast<char>((value >> 8u) & 0xffu);
        data[2] = static_cast<char>((value >> 16u) & 0xffu);
        data[3] = static_cast<char>((value >> 24u) & 0xffu);
    }

    inline void WriteUint64Le(uint64_t value, char* data) {
        for (size_t i = 0; i < sizeof(uint64_t); i++) {
            data[i] = static_cast<char>((value >> (i * 8u)) & 0xffu);
        }
    }

}  // namespace detail

inline PlainNnHeaderBuildResult BuildPlainNnHeader(const std::vector<uint64_t>& model_sizes) {
    if (model_sizes.empty() || model_sizes.size() > kPlainNnMaxModelCount) {
        return detail::BuildError("model_count must be in range 1..8");
    }

    PlainNnHeaderBuildResult result;
    result.data.fill(0);
    for (size_t i = 0; i < kPlainNnHeaderMagic.size(); i++) {
        result.data[kPlainNnMagicOffset + i] = kPlainNnHeaderMagic[i];
    }
    detail::WriteUint16Le(kPlainNnHeaderVersion, result.data.data() + kPlainNnVersionOffset);
    detail::WriteUint16Le(static_cast<uint16_t>(kPlainNnHeaderSize),
                          result.data.data() + kPlainNnHeaderSizeOffset);
    detail::WriteUint32Le(static_cast<uint32_t>(model_sizes.size()),
                          result.data.data() + kPlainNnModelCountOffset);
    detail::WriteUint32Le(0, result.data.data() + kPlainNnReservedOffset);

    for (size_t i = 0; i < model_sizes.size(); i++) {
        if (model_sizes[i] == 0) {
            return detail::BuildError("model segment size must be positive");
        }
        detail::WriteUint64Le(model_sizes[i],
                              result.data.data() + kPlainNnModelSizesOffset + i * sizeof(uint64_t));
    }

    result.ok = true;
    return result;
}

inline PlainNnHeaderParseResult ParsePlainNnHeader(const std::array<char, kPlainNnHeaderSize>& input) {
    for (size_t i = 0; i < kPlainNnHeaderMagic.size(); i++) {
        if (input[kPlainNnMagicOffset + i] != kPlainNnHeaderMagic[i]) {
            return detail::ParseError("magic mismatch");
        }
    }

    uint16_t version     = detail::ReadUint16Le(input.data() + kPlainNnVersionOffset);
    uint16_t header_size = detail::ReadUint16Le(input.data() + kPlainNnHeaderSizeOffset);
    uint32_t model_count = detail::ReadUint32Le(input.data() + kPlainNnModelCountOffset);
    uint32_t reserved    = detail::ReadUint32Le(input.data() + kPlainNnReservedOffset);

    if (version != kPlainNnHeaderVersion) {
        return detail::ParseError("unsupported header version");
    }
    if (header_size != kPlainNnHeaderSize) {
        return detail::ParseError("header size mismatch");
    }
    if (model_count == 0 || model_count > kPlainNnMaxModelCount) {
        return detail::ParseError("model_count must be in range 1..8");
    }
    if (reserved != 0) {
        return detail::ParseError("reserved field must be zero");
    }

    PlainNnHeader parsed;
    parsed.model_count = model_count;
    for (size_t i = 0; i < kPlainNnMaxModelCount; i++) {
        uint64_t size = detail::ReadUint64Le(input.data() + kPlainNnModelSizesOffset + i * sizeof(uint64_t));
        if (i < model_count) {
            if (size == 0) {
                return detail::ParseError("model segment size must be positive");
            }
            parsed.model_sizes[i] = size;
        } else if (size != 0) {
            return detail::ParseError("unused model size slot must be zero");
        }
    }

    PlainNnHeaderParseResult result;
    result.ok     = true;
    result.header = parsed;
    return result;
}

inline PlainNnHeaderValidationResult ValidatePlainNnFileSize(const PlainNnHeader& header,
                                                             uint64_t file_size) {
    if (header.model_count == 0 || header.model_count > kPlainNnMaxModelCount) {
        return detail::ValidationError("model_count must be in range 1..8");
    }

    uint64_t expected_size = kPlainNnHeaderSize;
    for (size_t i = 0; i < kPlainNnMaxModelCount; i++) {
        uint64_t segment_size = header.model_sizes[i];
        if (i >= header.model_count) {
            if (segment_size != 0) {
                return detail::ValidationError("unused model size slot must be zero");
            }
            continue;
        }
        if (segment_size == 0) {
            return detail::ValidationError("model segment size must be positive");
        }
        if (segment_size > std::numeric_limits<uint64_t>::max() - expected_size) {
            return detail::ValidationError("model file size overflow");
        }
        expected_size += segment_size;
    }

    if (expected_size != file_size) {
        return detail::ValidationError("model file size mismatch");
    }

    PlainNnHeaderValidationResult result;
    result.ok = true;
    return result;
}

}  // namespace cosmo::nn
