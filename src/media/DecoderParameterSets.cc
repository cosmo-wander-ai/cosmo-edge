#include "media/DecoderParameterSets.h"

#include <algorithm>

namespace cosmo::media {
namespace {
    constexpr size_t kMaxHeaders = 4 * 1024 * 1024;
    constexpr size_t kMaxPacket  = 64 * 1024 * 1024;
    constexpr uint8_t kStartCode[]{0, 0, 0, 1};

    struct Nal {
        const uint8_t* data;
        size_t size;
        unsigned type;
    };

    unsigned NalType(VideoCodecType codec, const uint8_t* data, size_t size) {
        if (!data || size == 0 || (data[0] & 0x80) != 0)
            return 0;
        if (codec == VideoCodecType::kH264)
            return data[0] & 0x1f;
        if (codec == VideoCodecType::kH265 && size >= 2 && (data[1] & 7) != 0)
            return (data[0] >> 1) & 0x3f;
        return 0;
    }

    bool FindStart(const uint8_t* data, size_t size, size_t from, size_t& at, size_t& length) {
        for (size_t i = from; i + 3 <= size; ++i) {
            if (data[i] != 0 || data[i + 1] != 0)
                continue;
            if (data[i + 2] == 1) {
                at     = i;
                length = 3;
                return true;
            }
            if (i + 4 <= size && data[i + 2] == 0 && data[i + 3] == 1) {
                at     = i;
                length = 4;
                return true;
            }
        }
        return false;
    }

    bool ReadAnnexB(VideoCodecType codec, const uint8_t* data, size_t size, std::vector<Nal>& nals) {
        size_t at = 0, length = 0;
        if (!data || !FindStart(data, size, 0, at, length) ||
            !std::all_of(data, data + at, [](uint8_t value) { return value == 0; }))
            return false;
        for (;;) {
            const size_t begin = at + length;
            size_t next = 0, next_length = 0;
            const bool more = FindStart(data, size, begin, next, next_length);
            size_t end      = more ? next : size;
            while (end > begin && data[end - 1] == 0)
                --end;
            if (end <= begin)
                return false;
            const auto type = NalType(codec, data + begin, end - begin);
            // HEVC type 0 is a valid TRAIL_N slice, so validate its header separately.
            if ((codec == VideoCodecType::kH264 && type == 0) ||
                (codec == VideoCodecType::kH265 &&
                 (end - begin < 2 || (data[begin] & 0x80) || !(data[begin + 1] & 7))))
                return false;
            nals.push_back({data + begin, end - begin, type});
            if (!more)
                return true;
            at     = next;
            length = next_length;
        }
    }

    bool IsParameter(VideoCodecType codec, unsigned type) {
        return codec == VideoCodecType::kH264 ? (type == 7 || type == 8 || type == 13)
                                              : (type == 32 || type == 33 || type == 34);
    }

    bool Complete(VideoCodecType codec, const std::vector<Nal>& nals, bool before_picture = false) {
        const auto end = before_picture ? std::find_if(nals.begin(), nals.end(),
                                                       [codec](const Nal& nal) {
                                                           return codec == VideoCodecType::kH264
                                                                      ? nal.type >= 1 && nal.type <= 5
                                                                      : nal.type <= 31;
                                                       })
                                        : nals.end();
        const auto has = [&](unsigned type) {
            return std::any_of(nals.begin(), end, [type](const Nal& nal) { return nal.type == type; });
        };
        return codec == VideoCodecType::kH264 ? has(7) && has(8) : has(32) && has(33) && has(34);
    }

    bool ReadLengthNal(VideoCodecType codec, const std::vector<uint8_t>& data, size_t& at,
                       unsigned expected_type, std::vector<Nal>& nals) {
        if (at >= data.size() || data.size() - at < 2)
            return false;
        const size_t size = (static_cast<size_t>(data[at]) << 8) | data[at + 1];
        at += 2;
        if (!size || size > data.size() - at || (data[at] & 0x80) ||
            (codec == VideoCodecType::kH265 && (size < 2 || !(data[at + 1] & 7))) ||
            NalType(codec, data.data() + at, size) != expected_type)
            return false;
        nals.push_back({data.data() + at, size, expected_type});
        at += size;
        return true;
    }

    bool ReadConfiguration(VideoCodecType codec, const std::vector<uint8_t>& data, std::vector<Nal>& nals) {
        if (data.empty())
            return true;
        if (data[0] != 1)
            return ReadAnnexB(codec, data.data(), data.size(), nals);
        if (codec == VideoCodecType::kH264) {
            if (data.size() < 7)
                return false;
            size_t at = 6;
            for (unsigned i = 0; i < (data[5] & 31); ++i)
                if (!ReadLengthNal(codec, data, at, 7, nals))
                    return false;
            if (at >= data.size())
                return false;
            const auto count = data[at++];
            for (unsigned i = 0; i < count; ++i)
                if (!ReadLengthNal(codec, data, at, 8, nals))
                    return false;
            // AVC high-profile extension fields may follow the mandatory SPS/PPS.
            if (at < data.size()) {
                const auto profile = data[1];
                if (!(profile == 100 || profile == 110 || profile == 122 || profile == 144) ||
                    data.size() - at < 4)
                    return false;
                at += 3;
                const auto extensions = data[at++];
                for (unsigned i = 0; i < extensions; ++i)
                    if (!ReadLengthNal(codec, data, at, 13, nals))
                        return false;
            }
            return at == data.size();
        }
        if (data.size() < 23)
            return false;
        size_t at = 23;
        for (unsigned array = 0; array < data[22]; ++array) {
            if (at >= data.size() || data.size() - at < 3)
                return false;
            const unsigned type  = data[at] & 63;
            const unsigned count = (static_cast<unsigned>(data[at + 1]) << 8) | data[at + 2];
            at += 3;
            for (unsigned i = 0; i < count; ++i)
                if (!ReadLengthNal(codec, data, at, type, nals))
                    return false;
        }
        return at == data.size();
    }
}  // namespace

bool DecoderParameterSets::Configure(VideoCodecType codec, const std::vector<uint8_t>& extradata,
                                     std::string& error) {
    codec_ = codec;
    headers_.clear();
    packet_.clear();
    Reset();
    std::vector<Nal> nals;
    valid_ = (codec == VideoCodecType::kH264 || codec == VideoCodecType::kH265) &&
             extradata.size() <= kMaxHeaders && ReadConfiguration(codec, extradata, nals) &&
             (extradata.empty() || Complete(codec, nals));
    if (!valid_) {
        error = "invalid or incomplete AVC/HEVC decoder extradata";
        return false;
    }
    for (const auto& nal : nals) {
        if (!IsParameter(codec, nal.type))
            continue;
        if (headers_.size() > kMaxHeaders - 4 || nal.size > kMaxHeaders - headers_.size() - 4) {
            headers_.clear();
            valid_ = false;
            error  = "decoder parameter sets exceed size limit";
            return false;
        }
        headers_.insert(headers_.end(), std::begin(kStartCode), std::end(kStartCode));
        headers_.insert(headers_.end(), nal.data, nal.data + nal.size);
    }
    error.clear();
    return true;
}

void DecoderParameterSets::Reset() noexcept {
    pending_  = true;
    prepared_ = false;
}

bool DecoderParameterSets::Prepare(const uint8_t* data, size_t size, const uint8_t*& output,
                                   size_t& output_size, std::string& error) {
    output      = data;
    output_size = size;
    prepared_   = false;
    if (!valid_ || !data || !size || size > kMaxPacket) {
        error = "invalid decoder packet or codec extradata";
        return false;
    }
    error.clear();
    if (!pending_ || headers_.empty())
        return true;
    std::vector<Nal> nals;
    if (!ReadAnnexB(codec_, data, size, nals)) {
        error = "decoder packet is not an Annex B access unit";
        return false;
    }
    if (Complete(codec_, nals, true)) {
        prepared_ = true;
        return true;
    }
    const bool random_access = std::any_of(nals.begin(), nals.end(), [&](const Nal& nal) {
        return codec_ == VideoCodecType::kH264 ? nal.type == 5 : nal.type >= 16 && nal.type <= 21;
    });
    if (!random_access)
        return true;
    // Out-of-band parameters precede in-band updates. Never overwrite or drop
    // any NAL in the original access unit; its newer parameter sets take precedence.
    packet_.assign(headers_.begin(), headers_.end());
    packet_.insert(packet_.end(), data, data + size);
    output      = packet_.data();
    output_size = packet_.size();
    prepared_   = true;
    return true;
}

void DecoderParameterSets::Commit() noexcept {
    if (prepared_)
        pending_ = false;
    prepared_ = false;
}

}  // namespace cosmo::media
