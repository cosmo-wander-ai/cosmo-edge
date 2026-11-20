#ifdef COSMO_NN_USE_HOST_BACKEND

#include "nn/device/cpu/cpu_dino_encode_node.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>

#include "nn/node/node_type_utils.h"
#include "nn/utils/dims_vector_utils.h"
#include "nn/utils/op.h"

namespace cosmo::nn {

namespace {

    bool IsAsciiWhitespace(unsigned char value) {
        return value == ' ' || value == '\t' || value == '\n' || value == '\r' || value == '\f' ||
               value == '\v';
    }

    bool IsAsciiControl(unsigned char value) {
        return (value < 32U && !IsAsciiWhitespace(value)) || value == 127U;
    }

    bool IsUnicodePunctuation(std::uint32_t value) {
        if (value < 128U)
            return std::ispunct(static_cast<unsigned char>(value)) != 0;
        return (value >= 0x2000U && value <= 0x206fU) || (value >= 0x2e00U && value <= 0x2e7fU) ||
               (value >= 0x3001U && value <= 0x303fU) || (value >= 0xff01U && value <= 0xff65U);
    }

    bool IsCjk(std::uint32_t value) {
        return (value >= 0x3400U && value <= 0x4dbfU) || (value >= 0x4e00U && value <= 0x9fffU) ||
               (value >= 0xf900U && value <= 0xfaffU) || (value >= 0x20000U && value <= 0x2fa1fU);
    }

    std::size_t Utf8CodePointLength(unsigned char first) {
        if ((first & 0x80U) == 0U)
            return 1;
        if ((first & 0xe0U) == 0xc0U)
            return 2;
        if ((first & 0xf0U) == 0xe0U)
            return 3;
        if ((first & 0xf8U) == 0xf0U)
            return 4;
        return 0;
    }

    bool DecodeUtf8(const std::string& text, std::size_t offset, std::uint32_t& value, std::size_t& length) {
        if (offset >= text.size())
            return false;
        const unsigned char first = static_cast<unsigned char>(text[offset]);
        length                    = Utf8CodePointLength(first);
        if (length == 0 || offset + length > text.size())
            return false;
        if (length == 1) {
            value = first;
            return true;
        }
        value = first & ((1U << (7U - static_cast<unsigned int>(length))) - 1U);
        for (std::size_t i = 1; i < length; ++i) {
            const unsigned char next = static_cast<unsigned char>(text[offset + i]);
            if ((next & 0xc0U) != 0x80U)
                return false;
            value = (value << 6U) | (next & 0x3fU);
        }
        return true;
    }

    std::vector<std::size_t> Utf8Boundaries(const std::string& text) {
        std::vector<std::size_t> boundaries{0};
        std::size_t offset = 0;
        while (offset < text.size()) {
            std::uint32_t value = 0;
            std::size_t length  = 0;
            if (!DecodeUtf8(text, offset, value, length))
                length = 1;
            offset += length;
            boundaries.push_back(offset);
        }
        return boundaries;
    }

    bool NeedsLeadingSpace(const std::string& token) {
        if (token.empty())
            return false;
        std::uint32_t value = 0;
        std::size_t length  = 0;
        return !DecodeUtf8(token, 0, value, length) || !IsUnicodePunctuation(value);
    }

}  // namespace

// ==================== CpuTokenizer Implementation ====================

bool CpuTokenizer::LoadVocab(const std::string& vocab_path) {
    std::ifstream infile(vocab_path);
    if (!infile.good())
        return false;

    std::string line;
    int64_t idx = 0;
    while (std::getline(infile, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (!token2idx.emplace(line, idx).second)
            return false;
        idx2token.emplace(idx, line);
        idx++;
    }
    const auto special_id = [&](const std::string& token) -> int64_t {
        const auto found = token2idx.find(token);
        return found == token2idx.end() ? -1 : found->second;
    };
    unk_id_ = special_id("[UNK]");
    cls_id_ = special_id("[CLS]");
    sep_id_ = special_id("[SEP]");
    pad_id_ = special_id("[PAD]");
    return !token2idx.empty() && unk_id_ >= 0 && cls_id_ >= 0 && sep_id_ >= 0 && pad_id_ >= 0;
}

void CpuTokenizer::EncodeText(const std::string& text, std::vector<int64_t>& ids) {
    ids.clear();
    if (unk_id_ < 0 || cls_id_ < 0 || sep_id_ < 0 || pad_id_ < 0)
        return;
    ids.push_back(cls_id_);
    for (const auto& token : BasicTokenize(text))
        WordPieceTokenize(token, ids);
    ids.push_back(sep_id_);
}

std::vector<std::string> CpuTokenizer::BasicTokenize(const std::string& text) const {
    std::vector<std::string> tokens;
    std::string current;
    const auto flush = [&]() {
        if (!current.empty()) {
            tokens.push_back(current);
            current.clear();
        }
    };
    std::size_t offset = 0;
    while (offset < text.size()) {
        std::uint32_t value = 0;
        std::size_t length  = 0;
        if (!DecodeUtf8(text, offset, value, length)) {
            flush();
            ++offset;
            continue;
        }
        if ((value < 128U && IsAsciiWhitespace(static_cast<unsigned char>(value))) ||
            (value < 128U && IsAsciiControl(static_cast<unsigned char>(value)))) {
            flush();
        } else if (IsUnicodePunctuation(value) || IsCjk(value)) {
            flush();
            std::string token = text.substr(offset, length);
            if (value < 128U)
                token[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(token[0])));
            tokens.push_back(std::move(token));
        } else {
            std::string piece = text.substr(offset, length);
            if (value < 128U)
                piece[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(piece[0])));
            current += piece;
        }
        offset += length;
    }
    flush();
    return tokens;
}

void CpuTokenizer::WordPieceTokenize(const std::string& token, std::vector<int64_t>& ids) const {
    const auto boundaries = Utf8Boundaries(token);
    if (boundaries.size() <= 1)
        return;
    if (boundaries.size() - 1 > 100U) {
        ids.push_back(unk_id_);
        return;
    }
    std::size_t start = 0;
    std::vector<int64_t> pieces;
    while (start + 1 < boundaries.size()) {
        std::size_t end  = boundaries.size() - 1;
        int64_t piece_id = -1;
        while (end > start) {
            std::string piece = token.substr(boundaries[start], boundaries[end] - boundaries[start]);
            if (start != 0)
                piece = "##" + piece;
            const auto found = token2idx.find(piece);
            if (found != token2idx.end()) {
                piece_id = found->second;
                break;
            }
            --end;
        }
        if (piece_id < 0) {
            ids.push_back(unk_id_);
            return;
        }
        pieces.push_back(piece_id);
        start = end;
    }
    ids.insert(ids.end(), pieces.begin(), pieces.end());
}

std::string CpuTokenizer::DecodeIds(const std::vector<int>& ids) const {
    std::string decoded;
    for (const int id : ids) {
        if (id == cls_id_ || id == sep_id_ || id == pad_id_)
            continue;
        const auto found = idx2token.find(id);
        if (found == idx2token.end())
            continue;
        const std::string& token = found->second;
        if (token.rfind("##", 0) == 0) {
            decoded += token.substr(2);
        } else {
            if (!decoded.empty() && NeedsLeadingSpace(token))
                decoded.push_back(' ');
            decoded += token;
        }
    }
    return decoded;
}

// ==================== CpuDinoEncodeNode Implementation ====================

CpuDinoEncodeNode::CpuDinoEncodeNode() : Node() {
    node_type     = NodeType::NODE_DINO_ENCODE;
    name          = NodeTypeUtils::NodeTypeToStr(NODE_DINO_ENCODE).append("_0");
    one_blob_only = false;
}

void CpuDinoEncodeNode::LoadParam(Op* op) {
    if (!op)
        return;
    auto dino_encode = dynamic_cast<DinoEncoder*>(op);
    if (!dino_encode)
        return;

    dst_height = dino_encode->dst_height;
    dst_width  = dino_encode->dst_width;
    is_bgr     = dino_encode->is_bgr;
    mean       = dino_encode->mean;
    auto std   = dino_encode->std;

    scale.resize(std.size());
    std::transform(std.begin(), std.end(), scale.begin(), [](float x) { return 1.0f / x; });
}

size_t CpuDinoEncodeNode::GetBottomCount() {
    return 2;
}

size_t CpuDinoEncodeNode::GetTopCount() {
    return 8;
}

DeviceType CpuDinoEncodeNode::GetTopBlobDeviceType() {
    return DeviceType::DEVICE_NAIVE;
}

Status CpuDinoEncodeNode::InferTopShapes() {
    if (dst_width <= 0 || dst_height <= 0)
        return Status(COSMO_NN_ERR_PARAM, "DINO input dimensions are not configured");
    const int proposals_count = 100 * 100 + 50 * 50 + 25 * 25 + 13 * 13;

    top_blob_shapes = {
        {max_batch, 3, dst_height, dst_width},
        {max_batch, kTokenLength},
        {max_batch, kTokenLength, kTokenLength},
        {max_batch, kTokenLength},
        {max_batch, kTokenLength},
        {max_batch, kTokenLength, kTokenLength},
        {max_batch, kTokenLength},
        {max_batch, proposals_count, 4},
    };
    top_blob_data_types = {
        DataType::DATA_TYPE_FLOAT, DataType::DATA_TYPE_INT32, DataType::DATA_TYPE_FLOAT,
        DataType::DATA_TYPE_INT32, DataType::DATA_TYPE_INT32, DataType::DATA_TYPE_FLOAT,
        DataType::DATA_TYPE_FLOAT, DataType::DATA_TYPE_FLOAT,
    };
    return Status{COSMO_NN_OK};
}

std::string CpuDinoEncodeNode::Strip(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos)
        return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

bool CpuDinoEncodeNode::Endswith(const std::string& s, const std::string& suffix) {
    if (suffix.length() > s.length())
        return false;
    return s.compare(s.length() - suffix.length(), suffix.length(), suffix) == 0;
}

void CpuDinoEncodeNode::CreateTokenizer(std::string path) {
    if (path.empty())
        return;

    std::string vocab_path = path;
    if (vocab_path.find(".json") != std::string::npos) {
        size_t last_slash = vocab_path.find_last_of("/\\");
        if (last_slash != std::string::npos)
            vocab_path = vocab_path.substr(0, last_slash + 1) + "vocab.txt";
        else
            vocab_path = "vocab.txt";
    }

    tokenizer.reset(new CpuTokenizer());
    if (!tokenizer->LoadVocab(vocab_path)) {
        tokenizer.reset();
        return;
    }

    special_token_ids.clear();
    for (const auto& token_str : special_tokens) {
        if (tokenizer->token2idx.find(token_str) != tokenizer->token2idx.end())
            special_token_ids.push_back(tokenizer->token2idx[token_str]);
    }
    if (std::find(special_token_ids.begin(), special_token_ids.end(), tokenizer->ClsId()) ==
        special_token_ids.end())
        special_token_ids.push_back(tokenizer->ClsId());
    if (std::find(special_token_ids.begin(), special_token_ids.end(), tokenizer->SepId()) ==
        special_token_ids.end())
        special_token_ids.push_back(tokenizer->SepId());
}

std::vector<int64_t> CpuDinoEncodeNode::Encode(const std::string& text) {
    if (!tokenizer)
        return {};
    std::vector<int64_t> ids;
    tokenizer->EncodeText(text, ids);
    return ids;
}

std::string CpuDinoEncodeNode::GetPromptStr(std::shared_ptr<Blob>& prompt) {
    auto dims = prompt->GetBlobDesc().dims;
    int len   = DimsVectorUtils::Count(dims);
    return std::string(reinterpret_cast<char*>(prompt->GetHandle().base), len);
}

Status CpuDinoEncodeNode::Forward(std::vector<std::shared_ptr<Blob>>& images,
                                  std::vector<std::shared_ptr<Blob>>& prompts,
                                  std::vector<std::shared_ptr<Blob>>& top_blobs) {
    if (!shared_resource)
        return Status(COSMO_NN_ERR_PARAM, "shared resource is null");
    if (images.size() != 1 || prompts.size() != 1 || top_blobs.size() != GetTopCount())
        return Status(COSMO_NN_ERR_PARAM, "GroundingDINO host preprocessing requires one image and prompt");
    if (!tokenizer) {
        CreateTokenizer(shared_resource->tokenizer_path);
        if (!tokenizer)
            return Status(COSMO_NN_ERR_PARAM, "create tokenizer failed");
        shared_resource->tokenizer_handle = tokenizer.get();
    }

    timer.Start();

    auto image_blob  = images.at(0);
    auto prompt_blob = prompts.at(0);
    if (!image_blob || !prompt_blob || std::any_of(top_blobs.begin(), top_blobs.end(), [](const auto& blob) {
            return !blob || !blob->GetHandle().base;
        })) {
        return Status(COSMO_NN_ERR_PARAM, "GroundingDINO preprocessing received a null blob");
    }

    auto top_image                     = top_blobs.at(0);
    auto top_position_ids              = top_blobs.at(1);
    auto top_text_self_attention_masks = top_blobs.at(2);
    auto top_input_ids                 = top_blobs.at(3);
    auto top_token_type_ids            = top_blobs.at(4);
    auto top_attention_mask            = top_blobs.at(5);
    auto top_text_token_mask           = top_blobs.at(6);
    auto top_proposals                 = top_blobs.at(7);

    // Process prompt
    const auto prompt_desc = prompt_blob->GetBlobDesc();
    if (prompt_desc.data_type != DATA_TYPE_UINT8 || prompt_desc.dims.empty() ||
        DimsVectorUtils::Count(prompt_desc.dims) <= 0 || !prompt_blob->GetHandle().base) {
        return Status(COSMO_NN_ERR_PARAM, "GroundingDINO prompt must be a non-empty UINT8 blob");
    }
    std::string caption = GetPromptStr(prompt_blob);
    caption             = Strip(caption);
    while (!caption.empty() && Endswith(caption, "."))
        caption.pop_back();
    if (caption.empty())
        return Status(COSMO_NN_ERR_PARAM, "prompt is empty");
    caption.push_back('.');

    auto token = Encode(caption);
    if (token.empty())
        return Status(COSMO_NN_ERR_PARAM, "encode failed");

    if (token.front() != tokenizer->ClsId() || token.back() != tokenizer->SepId())
        return Status(COSMO_NN_ERR_PARAM, "tokenizer did not emit CLS/SEP tokens");
    if (token.size() > static_cast<std::size_t>(kTokenLength))
        return Status(COSMO_NN_ERR_PARAM, "prompt exceeds GroundingDINO token limit");

    const auto token_len = token.size();
    auto input_ids_cp    = token;

    token.resize(kTokenLength, tokenizer->PadId());

    shared_resource->prompt_token_ids.clear();
    for (auto id : token)
        shared_resource->prompt_token_ids.push_back(static_cast<int32_t>(id));

    // ========== Process Image ==========
    const auto image_desc = image_blob->GetBlobDesc();
    auto img_dims         = image_desc.dims;
    if (dst_width <= 0 || dst_height <= 0 || mean.size() != 3 || scale.size() != 3 || img_dims.size() != 4 ||
        img_dims.at(0) != 1 || img_dims.at(3) != 3 || image_desc.data_type != DATA_TYPE_UINT8 ||
        image_desc.data_format != DATA_FORMAT_NHWC ||
        (!ImageFormatIsBGR(image_desc.image_format) && !ImageFormatIsRGB(image_desc.image_format))) {
        return Status(COSMO_NN_ERR_PARAM, "GroundingDINO image must be NHWC packed BGR/RGB UINT8");
    }
    int src_h    = img_dims.at(1);
    int src_w    = img_dims.at(2);
    int channels = 3;
    if (src_h <= 0 || src_w <= 0 || !image_blob->GetHandle().base)
        return Status(COSMO_NN_ERR_PARAM, "GroundingDINO image dimensions or buffer are invalid");

    auto* src_data = static_cast<const uint8_t*>(image_blob->GetHandle().base);

    // Bilinear resize to dst_height x dst_width
    std::vector<uint8_t> resized(dst_height * dst_width * channels);
    {
        const float x_ratio = static_cast<float>(src_w) / dst_width;
        const float y_ratio = static_cast<float>(src_h) / dst_height;
        for (int dy = 0; dy < dst_height; dy++) {
            float fy     = (dy + 0.5f) * y_ratio - 0.5f;
            int sy       = static_cast<int>(std::floor(fy));
            float frac_y = fy - static_cast<float>(sy);
            if (sy < 0) {
                sy     = 0;
                frac_y = 0.0f;
            } else if (sy >= src_h - 1) {
                sy     = src_h - 1;
                frac_y = 0.0f;
            }
            int sy1 = std::min(sy + 1, src_h - 1);
            for (int dx = 0; dx < dst_width; dx++) {
                float fx     = (dx + 0.5f) * x_ratio - 0.5f;
                int sx       = static_cast<int>(std::floor(fx));
                float frac_x = fx - static_cast<float>(sx);
                if (sx < 0) {
                    sx     = 0;
                    frac_x = 0.0f;
                } else if (sx >= src_w - 1) {
                    sx     = src_w - 1;
                    frac_x = 0.0f;
                }
                int sx1 = std::min(sx + 1, src_w - 1);
                for (int c = 0; c < channels; c++) {
                    float v00 = src_data[(sy * src_w + sx) * channels + c];
                    float v01 = src_data[(sy * src_w + sx1) * channels + c];
                    float v10 = src_data[(sy1 * src_w + sx) * channels + c];
                    float v11 = src_data[(sy1 * src_w + sx1) * channels + c];
                    float val = v00 * (1 - frac_x) * (1 - frac_y) + v01 * frac_x * (1 - frac_y) +
                                v10 * (1 - frac_x) * frac_y + v11 * frac_x * frac_y;
                    resized[(dy * dst_width + dx) * channels + c] =
                        static_cast<uint8_t>(std::min(255.0f, std::max(0.0f, val + 0.5f)));
                }
            }
        }
    }

    // Normalize HWC uint8 -> NCHW float32: (pixel/255 - mean) / std
    float* top_img_ptr = static_cast<float*>(top_image->GetHandle().base);
    int spatial        = dst_height * dst_width;
    for (int c = 0; c < channels; c++) {
        float alpha = (1.0f / 255.0f) * scale[c];
        float beta  = -mean[c] * scale[c];
        for (int y = 0; y < dst_height; y++) {
            for (int x = 0; x < dst_width; x++) {
                const bool input_is_bgr  = ImageFormatIsBGR(image_desc.image_format);
                const int source_channel = (is_bgr == input_is_bgr) ? c : channels - 1 - c;
                top_img_ptr[c * spatial + y * dst_width + x] =
                    resized[(y * dst_width + x) * channels + source_channel] * alpha + beta;
            }
        }
    }

    // ========== Process Text Inputs ==========
    auto* pos_ids_ptr    = static_cast<int32_t*>(top_position_ids->GetHandle().base);
    auto* self_attn_ptr  = static_cast<float*>(top_text_self_attention_masks->GetHandle().base);
    auto* input_ids_ptr  = static_cast<int32_t*>(top_input_ids->GetHandle().base);
    auto* type_ids_ptr   = static_cast<int32_t*>(top_token_type_ids->GetHandle().base);
    auto* attn_mask_ptr  = static_cast<float*>(top_attention_mask->GetHandle().base);
    auto* token_mask_ptr = static_cast<float*>(top_text_token_mask->GetHandle().base);
    auto* proposals_ptr  = static_cast<float*>(top_proposals->GetHandle().base);

    // Initialize
    std::memset(pos_ids_ptr, 0, kTokenLength * sizeof(int32_t));
    std::memset(self_attn_ptr, 0, kTokenLength * kTokenLength * sizeof(float));
    std::memset(type_ids_ptr, 0, kTokenLength * sizeof(int32_t));
    std::memset(attn_mask_ptr, 0, kTokenLength * kTokenLength * sizeof(float));
    std::memset(token_mask_ptr, 0, kTokenLength * sizeof(float));

    // Fill input_ids
    for (int i = 0; i < kTokenLength; i++)
        input_ids_ptr[i] = static_cast<int32_t>(token[i]);

    // Fill text_token_mask
    for (size_t i = 0; i < token_len; i++)
        token_mask_ptr[i] = (token[i] != tokenizer->PadId()) ? 1.0f : 0.0f;

    // Initialize diagonal for attention masks
    for (int i = 0; i < kTokenLength; i++) {
        attn_mask_ptr[i * kTokenLength + i] = 1.0f;
        self_attn_ptr[i * kTokenLength + i] = 1.0f;
    }

    // Find special token positions
    std::vector<int> idxs;
    for (size_t i = 0; i < input_ids_cp.size(); i++) {
        for (auto sid : special_token_ids) {
            if (input_ids_cp[i] == sid) {
                idxs.push_back(static_cast<int>(i));
                break;
            }
        }
    }

    // Fill attention masks and position_ids
    int previous_col = 0;
    for (size_t i = 0; i < idxs.size(); i++) {
        int col = idxs[i];
        if (col == 0 || col == static_cast<int>(token_len - 1)) {
            pos_ids_ptr[col] = 0;
        } else {
            for (int j = previous_col + 1; j <= col; j++) {
                for (int k = previous_col + 1; k <= col; k++) {
                    attn_mask_ptr[j * kTokenLength + k] = 1.0f;
                    self_attn_ptr[j * kTokenLength + k] = 1.0f;
                }
                pos_ids_ptr[j] = j - (previous_col + 1);
            }
        }
        previous_col = col;
    }

    // Generate proposals
    const int proposals_count = 100 * 100 + 50 * 50 + 25 * 25 + 13 * 13;
    std::vector<std::vector<int>> spatial_shapes{{100, 100}, {50, 50}, {25, 25}, {13, 13}};
    int pidx = 0;
    for (size_t i = 0; i < spatial_shapes.size(); i++) {
        int valid_H = spatial_shapes[i][0];
        int valid_W = spatial_shapes[i][1];
        float wh    = 0.05f * std::pow(2.0f, static_cast<float>(i));
        for (int h_i = 0; h_i < valid_H; h_i++) {
            for (int w_i = 0; w_i < valid_W; w_i++) {
                proposals_ptr[pidx++] = (static_cast<float>(w_i) + 0.5f) / valid_W;
                proposals_ptr[pidx++] = (static_cast<float>(h_i) + 0.5f) / valid_H;
                proposals_ptr[pidx++] = wh;
                proposals_ptr[pidx++] = wh;
            }
        }
    }

    timer.Stop();
    return Status{COSMO_NN_OK};
}

}  // namespace cosmo::nn

#endif  // COSMO_NN_USE_HOST_BACKEND
