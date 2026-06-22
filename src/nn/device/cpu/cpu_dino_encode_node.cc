#ifdef COSMO_NN_USE_CPU_BACKEND

#include "nn/device/cpu/cpu_dino_encode_node.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

#include "nn/node/node_type_utils.h"
#include "nn/utils/dims_vector_utils.h"
#include "nn/utils/op.h"

namespace cosmo::nn {

// ==================== CpuTokenizer Implementation ====================

bool CpuTokenizer::LoadVocab(const std::string& vocab_path) {
    std::ifstream infile(vocab_path);
    if (!infile.good())
        return false;

    std::string line;
    int idx = 0;
    while (std::getline(infile, line)) {
        token2idx[line] = idx;
        idx2token[idx]  = line;
        idx++;
    }
    return true;
}

std::vector<std::string> CpuTokenizer::StringSplit(const std::string& str, char delim) {
    std::vector<std::string> elems;
    size_t lastPos = str.find_first_not_of(delim, 0);
    size_t pos     = str.find_first_of(delim, lastPos);
    while (pos != std::string::npos || lastPos != std::string::npos) {
        elems.push_back(str.substr(lastPos, pos - lastPos));
        lastPos = str.find_first_not_of(delim, pos);
        pos     = str.find_first_of(delim, lastPos);
    }
    return elems;
}

void CpuTokenizer::Tokenize(const std::string& token, std::vector<int64_t>& idx) {
    idx.clear();
    idx.push_back(101);  // [CLS]
    {
        auto tokens = StringSplit(token, '.');
        for (const auto& t : tokens) {
            if (token2idx.find(t) != token2idx.end())
                idx.push_back(token2idx[t]);
            if (token2idx.find(".") != token2idx.end())
                idx.push_back(token2idx["."]);
        }
    }
    idx.push_back(102);  // [SEP]
}

void CpuTokenizer::EncodeText(const std::string& text, std::vector<int64_t>& ids) {
    Tokenize(text, ids);
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
    return COSMO_NN_OK;
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
    if (std::find(special_token_ids.begin(), special_token_ids.end(), 101) == special_token_ids.end())
        special_token_ids.push_back(101);
    if (std::find(special_token_ids.begin(), special_token_ids.end(), 102) == special_token_ids.end())
        special_token_ids.push_back(102);
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
    if (!tokenizer) {
        CreateTokenizer(shared_resource->tokenizer_path);
        if (!tokenizer)
            return Status(COSMO_NN_ERR_PARAM, "create tokenizer failed");
        shared_resource->tokenizer_handle = tokenizer.get();
    }

    timer.Start();

    auto image_blob  = images.at(0);
    auto prompt_blob = prompts.at(0);

    auto top_image                     = top_blobs.at(0);
    auto top_position_ids              = top_blobs.at(1);
    auto top_text_self_attention_masks = top_blobs.at(2);
    auto top_input_ids                 = top_blobs.at(3);
    auto top_token_type_ids            = top_blobs.at(4);
    auto top_attention_mask            = top_blobs.at(5);
    auto top_text_token_mask           = top_blobs.at(6);
    auto top_proposals                 = top_blobs.at(7);

    // Process prompt
    std::string caption = GetPromptStr(prompt_blob);
    std::transform(caption.begin(), caption.end(), caption.begin(), ::tolower);
    caption = Strip(caption);
    while (!caption.empty() && Endswith(caption, "."))
        caption.pop_back();
    if (caption.empty())
        return Status(COSMO_NN_ERR_PARAM, "prompt is empty");

    auto token = Encode(caption);
    if (token.empty())
        return Status(COSMO_NN_ERR_PARAM, "encode failed");

    if (token[0] != 101)
        token.insert(token.begin(), 101);
    if (token.back() != 102)
        token.push_back(102);

    auto token_len    = token.size();
    auto input_ids_cp = token;

    if (static_cast<int>(token_len) < kTokenLength)
        token.resize(kTokenLength, 0);
    else if (static_cast<int>(token_len) > kTokenLength) {
        token.resize(kTokenLength);
        token[kTokenLength - 1] = 102;
    }

    shared_resource->prompt_token_ids.clear();
    for (auto id : token)
        shared_resource->prompt_token_ids.push_back(static_cast<int32_t>(id));

    // ========== Process Image ==========
    auto img_dims = image_blob->GetBlobDesc().dims;
    int src_h     = img_dims.at(1);
    int src_w     = img_dims.at(2);
    int channels  = 3;

    auto* src_data = static_cast<const uint8_t*>(image_blob->GetHandle().base);

    // Bilinear resize to dst_height x dst_width
    std::vector<uint8_t> resized(dst_height * dst_width * channels);
    {
        const float x_ratio = static_cast<float>(src_w) / dst_width;
        const float y_ratio = static_cast<float>(src_h) / dst_height;
        for (int dy = 0; dy < dst_height; dy++) {
            float fy     = (dy + 0.5f) * y_ratio - 0.5f;
            int sy       = std::max(0, std::min(static_cast<int>(fy), src_h - 1));
            float frac_y = fy - sy;
            int sy1      = std::min(sy + 1, src_h - 1);
            for (int dx = 0; dx < dst_width; dx++) {
                float fx     = (dx + 0.5f) * x_ratio - 0.5f;
                int sx       = std::max(0, std::min(static_cast<int>(fx), src_w - 1));
                float frac_x = fx - sx;
                int sx1      = std::min(sx + 1, src_w - 1);
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
                top_img_ptr[c * spatial + y * dst_width + x] =
                    resized[(y * dst_width + x) * channels + c] * alpha + beta;
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
        token_mask_ptr[i] = (token[i] > 0) ? 1.0f : 0.0f;

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
    return COSMO_NN_OK;
}

}  // namespace cosmo::nn

#endif  // COSMO_NN_USE_CPU_BACKEND
