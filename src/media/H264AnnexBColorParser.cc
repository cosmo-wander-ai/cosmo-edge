#include "media/H264AnnexBColorParser.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace cosmo::media {
namespace {

    class Bits {
    public:
        explicit Bits(const std::vector<uint8_t>& data) : data_(data) {}

        bool Read(unsigned count, uint32_t& value) {
            value = 0;
            if (count > 32 || count > data_.size() * 8 - pos_)
                return false;
            for (unsigned i = 0; i < count; ++i, ++pos_)
                value = (value << 1) | ((data_[pos_ / 8] >> (7 - pos_ % 8)) & 1U);
            return true;
        }
        bool Flag(bool& value) {
            uint32_t bit = 0;
            if (!Read(1, bit))
                return false;
            value = bit != 0;
            return true;
        }
        bool Ue(uint32_t& value) {
            unsigned zeros = 0;
            uint32_t bit   = 0;
            while (true) {
                if (!Read(1, bit))
                    return false;
                if (bit)
                    break;
                if (++zeros > 31)
                    return false;
            }
            uint32_t suffix = 0;
            if (!Read(zeros, suffix))
                return false;
            const uint64_t decoded = ((uint64_t{1} << zeros) - 1) + suffix;
            if (decoded > std::numeric_limits<uint32_t>::max())
                return false;
            value = static_cast<uint32_t>(decoded);
            return true;
        }
        bool Se(int32_t& value) {
            uint32_t code = 0;
            if (!Ue(code) || code == std::numeric_limits<uint32_t>::max())
                return false;
            const int64_t magnitude = (static_cast<uint64_t>(code) + 1) / 2;
            const int64_t decoded   = (code & 1U) ? magnitude : -magnitude;
            if (decoded < std::numeric_limits<int32_t>::min() ||
                decoded > std::numeric_limits<int32_t>::max())
                return false;
            value = static_cast<int32_t>(decoded);
            return true;
        }
        bool MoreRbspData() const {
            if (pos_ >= data_.size() * 8)
                return false;
            if (((data_[pos_ / 8] >> (7 - pos_ % 8)) & 1U) == 0)
                return true;
            for (size_t i = pos_ + 1; i < data_.size() * 8; ++i)
                if ((data_[i / 8] >> (7 - i % 8)) & 1U)
                    return true;
            return false;
        }
        bool Trailing() {
            uint32_t bit = 0;
            if (!Read(1, bit) || bit != 1)
                return false;
            while (pos_ < data_.size() * 8)
                if (!Read(1, bit) || bit != 0)
                    return false;
            return true;
        }

    private:
        const std::vector<uint8_t>& data_;
        size_t pos_{0};
    };

    bool ScalingList(Bits& bits, unsigned count) {
        int last = 8;
        int next = 8;
        for (unsigned i = 0; i < count; ++i) {
            if (next != 0) {
                int32_t delta = 0;
                if (!bits.Se(delta) || delta < -128 || delta > 127)
                    return false;
                next = (last + delta + 256) % 256;
            }
            last = next == 0 ? last : next;
        }
        return true;
    }

    bool Hrd(Bits& bits) {
        uint32_t count = 0, ignored = 0;
        bool flag = false;
        if (!bits.Ue(count) || count > 31 || !bits.Read(4, ignored) || !bits.Read(4, ignored))
            return false;
        for (uint32_t i = 0; i <= count; ++i)
            if (!bits.Ue(ignored) || !bits.Ue(ignored) || !bits.Flag(flag))
                return false;
        return bits.Read(5, ignored) && bits.Read(5, ignored) && bits.Read(5, ignored) &&
               bits.Read(5, ignored);
    }

    bool Vui(Bits& bits, NativeVideoColorSpace& space, NativeVideoColorRange& range) {
        bool flag      = false;
        uint32_t value = 0;
        if (!bits.Flag(flag))
            return false;
        if (flag) {
            if (!bits.Read(8, value))
                return false;
            if (value == 255 && (!bits.Read(16, value) || !bits.Read(16, value)))
                return false;
        }
        if (!bits.Flag(flag))
            return false;
        if (flag && !bits.Flag(flag))
            return false;
        bool video_signal = false;
        if (!bits.Flag(video_signal))
            return false;
        // H.264 infers limited range when video_full_range_flag is absent.
        // Matrix value 2 (or an absent colour description) remains unspecified.
        space = NativeVideoColorSpace::Unspecified;
        range = NativeVideoColorRange::Limited;
        if (video_signal) {
            bool full = false, color_description = false;
            if (!bits.Read(3, value) || !bits.Flag(full) || !bits.Flag(color_description))
                return false;
            range = full ? NativeVideoColorRange::Full : NativeVideoColorRange::Limited;
            if (color_description) {
                if (!bits.Read(8, value) || !bits.Read(8, value) || !bits.Read(8, value))
                    return false;
                if (value == 1)
                    space = NativeVideoColorSpace::Bt709;
                else if (value == 5 || value == 6)
                    space = NativeVideoColorSpace::Bt601;
                else if (value != 2)
                    return false;
            }
        }
        if (!bits.Flag(flag))
            return false;
        if (flag && (!bits.Ue(value) || value > 5 || !bits.Ue(value) || value > 5))
            return false;
        if (!bits.Flag(flag))
            return false;
        if (flag &&
            (!bits.Read(32, value) || value == 0 || !bits.Read(32, value) || value == 0 || !bits.Flag(flag)))
            return false;
        bool nal_hrd = false, vcl_hrd = false;
        if (!bits.Flag(nal_hrd) || (nal_hrd && !Hrd(bits)) || !bits.Flag(vcl_hrd) || (vcl_hrd && !Hrd(bits)))
            return false;
        if ((nal_hrd || vcl_hrd) && !bits.Flag(flag))
            return false;
        if (!bits.Flag(flag) || !bits.Flag(flag))
            return false;
        if (flag) {
            if (!bits.Flag(flag))
                return false;
            for (int i = 0; i < 6; ++i)
                if (!bits.Ue(value))
                    return false;
        }
        return true;
    }

    bool Unescape(const uint8_t* data, size_t size, std::vector<uint8_t>& out) {
        out.clear();
        out.reserve(size);
        unsigned zeros = 0;
        size_t i       = 0;
        while (i < size) {
            // Nonzero spans cannot affect the zero-run state. Copy them in
            // bulk, then run the original strict escape validation at zeros.
            if (zeros == 0) {
                const auto* zero = static_cast<const uint8_t*>(std::memchr(data + i, 0, size - i));
                const size_t end = zero ? static_cast<size_t>(zero - data) : size;
                out.insert(out.end(), data + i, data + end);
                i = end;
                if (i == size)
                    break;
            }
            const uint8_t b = data[i++];
            if (zeros >= 2 && b == 3) {
                if (i >= size || data[i] > 3)
                    return false;
                zeros = 0;
                continue;
            }
            if (zeros >= 2 && b <= 2)
                return false;
            out.push_back(b);
            zeros = b == 0 ? zeros + 1 : 0;
        }
        return !out.empty();
    }

    size_t StartCode(const uint8_t* data, size_t size, size_t start) {
        while (start < size && size - start >= 3) {
            const auto* zero = static_cast<const uint8_t*>(std::memchr(data + start, 0, size - start - 2));
            if (!zero)
                return size;
            const size_t i = static_cast<size_t>(zero - data);
            if (data[i + 1] == 0 && data[i + 2] == 1)
                return i;
            start = i + 1;
        }
        return size;
    }
}  // namespace

bool H264AnnexBColorParser::ParseParameterSetGeometry(const uint8_t* data, size_t size, uint32_t& width,
                                                      uint32_t& height) {
    width = height = 0;
    if (!data || size < 8 || size > 8192)
        return false;
    size_t start = StartCode(data, size, 0);
    if (start == size || std::any_of(data, data + start, [](uint8_t b) { return b != 0; }))
        return false;
    Sps sps;
    Pps pps;
    bool have_sps = false, have_pps = false;
    std::vector<uint8_t> sps_raw, pps_raw;
    while (start < size) {
        const size_t begin = start + 3;
        size_t next = StartCode(data, size, begin), end = next;
        while (end > begin && data[end - 1] == 0)
            --end;
        if (end <= begin || end - begin > 4096 || (data[begin] & 0x80U) || ((data[begin] >> 5) & 3U) == 0)
            return false;
        const uint8_t type = data[begin] & 0x1fU;
        if (type != 7 && type != 8)
            return false;
        std::vector<uint8_t> rbsp, raw(data + begin, data + end);
        if (!Unescape(data + begin + 1, end - begin - 1, rbsp))
            return false;
        if (type == 7) {
            Sps parsed;
            if (!ReadSps(rbsp, parsed) || (have_sps && raw != sps_raw))
                return false;
            sps      = parsed;
            sps_raw  = std::move(raw);
            have_sps = true;
        } else {
            Pps parsed;
            if (!have_sps || !ReadPps(rbsp, parsed) || parsed.sps_id != sps.id ||
                (have_pps && raw != pps_raw))
                return false;
            pps      = parsed;
            pps_raw  = std::move(raw);
            have_pps = true;
        }
        start = next;
    }
    if (!have_sps || !have_pps || !sps.width || !sps.height || (sps.width & 1U) || (sps.height & 1U))
        return false;
    width  = sps.width;
    height = sps.height;
    return true;
}

bool H264AnnexBColorParser::BeginGeneration(uint64_t generation) {
    if (generation == 0 || generation <= generation_)
        return false;
    generation_ = generation;
    fault_ = have_sps_ = have_pps_ = false;
    sps_bytes_.clear();
    pps_bytes_.clear();
    return true;
}

bool H264AnnexBColorParser::ReadSps(const std::vector<uint8_t>& rbsp, Sps& sps) {
    Bits bits(rbsp);
    uint32_t profile = 0, flags = 0, value = 0;
    bool flag = false;
    if (!bits.Read(8, profile) || !bits.Read(8, flags) || (flags & 3U) || !bits.Read(8, value) ||
        !bits.Ue(sps.id) || sps.id > 31)
        return false;
    const bool high = profile == 100 || profile == 110 || profile == 122 || profile == 244 || profile == 44 ||
                      profile == 83 || profile == 86 || profile == 118 || profile == 128 || profile == 138 ||
                      profile == 139 || profile == 134 || profile == 135;
    if (high) {
        if (!bits.Ue(value) || value != 1 || !bits.Ue(value) || value != 0 || !bits.Ue(value) || value != 0 ||
            !bits.Flag(flag) || !bits.Flag(flag))
            return false;
        if (flag) {
            for (unsigned i = 0; i < 8; ++i) {
                if (!bits.Flag(flag) || (flag && !ScalingList(bits, i < 6 ? 16 : 64)))
                    return false;
            }
        }
    } else if (profile != 66 && profile != 77 && profile != 88) {
        return false;
    }
    if (!bits.Ue(value) || value > 12)
        return false;
    sps.frame_num_bits = value + 4;
    if (!bits.Ue(value) || value > 2)
        return false;
    sps.poc_type = value;
    if (sps.poc_type == 0) {
        if (!bits.Ue(value) || value > 12)
            return false;
        sps.poc_bits = value + 4;
    } else if (sps.poc_type == 1) {
        int32_t signed_value = 0;
        if (!bits.Flag(sps.delta_poc_always_zero) || !bits.Se(signed_value) || !bits.Se(signed_value) ||
            !bits.Ue(value) || value > 255)
            return false;
        for (uint32_t i = 0; i < value; ++i)
            if (!bits.Se(signed_value))
                return false;
    }
    uint32_t width = 0, height = 0;
    if (!bits.Ue(value) || value > 16 || !bits.Flag(flag) || !bits.Ue(width) || width > 65535 ||
        !bits.Ue(height) || height > 65535 || !bits.Flag(flag) || !flag || !bits.Flag(flag) ||
        !bits.Flag(flag))
        return false;
    uint32_t left = 0, right = 0, top = 0, bottom = 0;
    if (flag) {
        if (!bits.Ue(left) || !bits.Ue(right) || !bits.Ue(top) || !bits.Ue(bottom) ||
            uint64_t(left) + right >= uint64_t(width + 1) * 8 ||
            uint64_t(top) + bottom >= uint64_t(height + 1) * 8)
            return false;
    }
    sps.pic_size_in_mbs = uint64_t(width + 1) * (height + 1);
    sps.width           = (width + 1) * 16 - (left + right) * 2;
    sps.height          = (height + 1) * 16 - (top + bottom) * 2;
    if (!bits.Flag(flag))
        return false;
    sps.range = NativeVideoColorRange::Limited;
    if (flag && !Vui(bits, sps.space, sps.range))
        return false;
    return bits.Trailing();
}

bool H264AnnexBColorParser::ReadPps(const std::vector<uint8_t>& rbsp, Pps& pps) {
    Bits bits(rbsp);
    uint32_t value       = 0;
    int32_t signed_value = 0;
    bool flag            = false;
    if (!bits.Ue(pps.id) || pps.id > 255 || !bits.Ue(pps.sps_id) || pps.sps_id > 31 || !bits.Flag(flag) ||
        !bits.Flag(pps.bottom_field_poc_present) || !bits.Ue(value) || value != 0 || !bits.Ue(value) ||
        value > 31 || !bits.Ue(value) || value > 31 || !bits.Flag(flag) || !bits.Read(2, value) ||
        !bits.Se(signed_value) || !bits.Se(signed_value) || !bits.Se(signed_value) || !bits.Flag(flag) ||
        !bits.Flag(flag) || !bits.Flag(flag))
        return false;
    if (bits.MoreRbspData()) {
        bool transform = false, scaling = false;
        if (!bits.Flag(transform) || !bits.Flag(scaling))
            return false;
        if (scaling) {
            for (unsigned i = 0; i < (transform ? 8U : 6U); ++i)
                if (!bits.Flag(flag) || (flag && !ScalingList(bits, i < 6 ? 16 : 64)))
                    return false;
        }
        if (!bits.Se(signed_value))
            return false;
    }
    return bits.Trailing();
}

bool H264AnnexBColorParser::ReadSlice(const std::vector<uint8_t>& rbsp, bool idr, bool reference,
                                      const Sps& sps, const Pps& pps, SliceHeader& slice) {
    Bits bits(rbsp);
    uint32_t value  = 0;
    slice           = {};
    slice.idr       = idr;
    slice.reference = reference;
    if (!bits.Ue(slice.first_mb) || slice.first_mb >= sps.pic_size_in_mbs || !bits.Ue(value) || value > 9 ||
        !bits.Ue(value) || value != pps.id || !bits.Read(sps.frame_num_bits, slice.frame_num))
        return false;
    if (idr && !bits.Ue(slice.idr_pic_id))
        return false;
    if (sps.poc_type == 0) {
        if (!bits.Read(sps.poc_bits, slice.poc_lsb))
            return false;
        if (pps.bottom_field_poc_present && !bits.Se(slice.delta_poc_bottom))
            return false;
    } else if (sps.poc_type == 1 && !sps.delta_poc_always_zero) {
        if (!bits.Se(slice.delta_poc0))
            return false;
        if (pps.bottom_field_poc_present && !bits.Se(slice.delta_poc1))
            return false;
    }
    // Slice payload validation belongs to the hardware decoder.
    return bits.MoreRbspData();
}

bool H264AnnexBColorParser::ReadSei(const std::vector<uint8_t>& rbsp) {
    size_t pos = 0;
    while (pos < rbsp.size() && rbsp[pos] != 0x80) {
        size_t type = 0, length = 0;
        while (pos < rbsp.size() && rbsp[pos] == 0xff) {
            type += 255;
            ++pos;
        }
        if (pos >= rbsp.size())
            return false;
        type += rbsp[pos++];
        while (pos < rbsp.size() && rbsp[pos] == 0xff) {
            length += 255;
            ++pos;
        }
        if (pos >= rbsp.size())
            return false;
        length += rbsp[pos++];
        // User-data-unregistered does not define color metadata. Other SEI
        // messages are rejected until their interaction is explicitly vetted.
        if (type != 5 || length < 16 || length > rbsp.size() - pos)
            return false;
        pos += length;
    }
    return pos + 1 == rbsp.size() && rbsp[pos] == 0x80;
}

bool H264AnnexBColorParser::ParseAccessUnit(uint64_t generation, const uint8_t* data, size_t size,
                                            VideoColorEvidence& output) {
    output = {};
    if (generation != generation_ || generation_ == 0 || fault_)
        return false;
    auto fail = [&]() {
        fault_ = true;
        return false;
    };
    if (!data || size < 4 || size > 16 * 1024 * 1024)
        return fail();
    size_t start = StartCode(data, size, 0);
    if (start == size || std::any_of(data, data + start, [](uint8_t b) { return b != 0; }))
        return fail();
    bool seen_slice = false, seen_aud = false, seen_any = false;
    SliceHeader picture_slice{};
    uint32_t last_first_mb = 0;
    while (start < size) {
        size_t begin = start + 3;
        size_t next  = StartCode(data, size, begin);
        size_t end   = next;
        while (end > begin && data[end - 1] == 0)
            --end;
        if (end <= begin || (data[begin] & 0x80U))
            return fail();
        const uint8_t type    = data[begin] & 0x1fU;
        const uint8_t ref_idc = (data[begin] >> 5) & 3U;
        if (((type == 5 || type == 7 || type == 8) && ref_idc == 0) ||
            ((type == 6 || type == 9) && ref_idc != 0))
            return fail();
        if ((seen_slice && type != 1 && type != 5) ||
            (type != 1 && type != 5 && type != 6 && type != 7 && type != 8 && type != 9))
            return fail();
        if ((type == 7 || type == 8) && end - begin > 4096)
            return fail();
        std::vector<uint8_t> rbsp;
        if (!Unescape(data + begin + 1, end - begin - 1, rbsp))
            return fail();
        if (type == 9) {
            if (seen_any || seen_aud)
                return fail();
            Bits bits(rbsp);
            uint32_t value = 0;
            if (!bits.Read(3, value) || !bits.Trailing())
                return fail();
            seen_aud = true;
        } else if (type == 6) {
            if (!ReadSei(rbsp))
                return fail();
        } else if (type == 7) {
            Sps parsed;
            if (!ReadSps(rbsp, parsed))
                return fail();
            const std::vector<uint8_t> raw(data + begin, data + end);
            if (have_sps_ && (parsed.id != sps_.id || raw != sps_bytes_))
                return fail();
            sps_       = parsed;
            sps_bytes_ = raw;
            have_sps_  = true;
        } else if (type == 8) {
            Pps parsed;
            if (!ReadPps(rbsp, parsed) || !have_sps_ || parsed.sps_id != sps_.id)
                return fail();
            const std::vector<uint8_t> raw(data + begin, data + end);
            if (have_pps_ && (parsed.id != pps_.id || raw != pps_bytes_))
                return fail();
            pps_       = parsed;
            pps_bytes_ = raw;
            have_pps_  = true;
        } else {
            SliceHeader slice;
            if (!have_sps_ || !have_pps_ || !ReadSlice(rbsp, type == 5, ref_idc != 0, sps_, pps_, slice))
                return fail();
            if (!seen_slice) {
                if (slice.first_mb != 0)
                    return fail();
                picture_slice = slice;
                output.idr    = slice.idr;
            } else if (slice.first_mb <= last_first_mb || slice.frame_num != picture_slice.frame_num ||
                       slice.idr != picture_slice.idr || slice.reference != picture_slice.reference ||
                       slice.idr_pic_id != picture_slice.idr_pic_id ||
                       slice.poc_lsb != picture_slice.poc_lsb ||
                       slice.delta_poc_bottom != picture_slice.delta_poc_bottom ||
                       slice.delta_poc0 != picture_slice.delta_poc0 ||
                       slice.delta_poc1 != picture_slice.delta_poc1) {
                return fail();
            }
            last_first_mb = slice.first_mb;
            seen_slice    = true;
        }
        seen_any = true;
        start    = next;
    }
    if (!seen_slice)
        return fail();
    output.vui_verified     = true;
    output.bitstream_space  = sps_.space;
    output.bitstream_range  = sps_.range;
    output.bitstream_width  = sps_.width;
    output.bitstream_height = sps_.height;
    return true;
}

}  // namespace cosmo::media
