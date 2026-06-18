#include "nn/device/sophon/sophon_dino_encode_node.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>

#include "bmcv_api.h"
#include "bmcv_api_ext.h"
#include "bmlib_runtime.h"
#include "bmruntime_cpp.h"
#include "nn/device/sophon/sophon_node.h"
#include "nn/node/node_type_utils.h"
#include "nn/utils/dims_vector_utils.h"
#include "nn/utils/image_format_utils.h"
#include "nn/utils/op.h"
#include "nn/utils/string_format.h"

namespace cosmo::nn {

// ==================== SophonTokenizer Implementation ====================

bool SophonTokenizer::LoadVocab(const std::string& vocab_path) {
    std::ifstream infile(vocab_path);
    if (!infile.good()) {
        std::cerr << "Failed to open vocab file: " << vocab_path << std::endl;
        return false;
    }

    std::string line;
    int idx = 0;
    while (std::getline(infile, line)) {
        token2idx[line] = idx;
        idx2token[idx]  = line;
        idx++;
    }
    infile.close();
    return true;
}

std::vector<std::string> SophonTokenizer::StringSplit(const std::string& str, char delim) {
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

void SophonTokenizer::Tokenize(const std::string& token, std::vector<int64_t>& idx) {
    idx.clear();
    idx.push_back(101);  // [CLS]
    {
        std::vector<std::string> tokens = StringSplit(token, '.');
        for (const auto& t : tokens) {
            if (token2idx.find(t) != token2idx.end()) {
                idx.push_back(token2idx[t]);
            }
            if (token2idx.find(".") != token2idx.end()) {
                idx.push_back(token2idx["."]);
            }
        }
    }
    idx.push_back(102);  // [SEP]
}

void SophonTokenizer::EncodeText(const std::string& text, std::vector<int64_t>& ids) {
    Tokenize(text, ids);
}

// ==================== SophonDinoEncodeNode Implementation ====================

SophonDinoEncodeNode::SophonDinoEncodeNode() : Node() {
    node_type     = NodeType::NODE_DINO_ENCODE;
    name          = NodeTypeUtils::NodeTypeToStr(NODE_DINO_ENCODE).append("_0");
    one_blob_only = false;
}

SophonDinoEncodeNode::~SophonDinoEncodeNode() {}

void SophonDinoEncodeNode::LoadParam(Op* op) {
    if (!op)
        return;

    auto dino_encode = dynamic_cast<DinoEncoder*>(op);
    dst_height       = dino_encode->dst_height;
    dst_width        = dino_encode->dst_width;
    is_bgr           = dino_encode->is_bgr;
    mean             = dino_encode->mean;
    auto std         = dino_encode->std;

    scale.resize(std.size());
    std::transform(std.begin(), std.end(), scale.begin(), [](float x) { return 1.0f / x; });
}

size_t SophonDinoEncodeNode::GetBottomCount() {
    return 2;
}

size_t SophonDinoEncodeNode::GetTopCount() {
    return 8;
}

DeviceType SophonDinoEncodeNode::GetTopBlobDeviceType() {
    return DEVICE_SOPHON_TPU;
}

Status SophonDinoEncodeNode::InferTopShapes() {
    // Generate proposals count: 100*100 + 50*50 + 25*25 + 13*13 = 13294
    const int proposals_count = 100 * 100 + 50 * 50 + 25 * 25 + 13 * 13;

    top_blob_shapes     = {{max_batch, 3, dst_height, dst_width},  // 0: samples
                           {max_batch, length},                    // 1: position_ids
                           {max_batch, length, length},            // 2: text_self_attention_masks
                           {max_batch, length},                    // 3: input_ids
                           {max_batch, length},                    // 4: token_type_ids
                           {max_batch, length, length},            // 5: attention_mask
                           {max_batch, length},                    // 6: text_token_mask
                           {max_batch, proposals_count, 4}};       // 7: proposals
    top_blob_data_types = {DataType::DATA_TYPE_FLOAT,              // samples
                           DataType::DATA_TYPE_INT32,              // position_ids
                           DataType::DATA_TYPE_FLOAT,              // text_self_attention_masks
                           DataType::DATA_TYPE_INT32,              // input_ids
                           DataType::DATA_TYPE_INT32,              // token_type_ids
                           DataType::DATA_TYPE_FLOAT,              // attention_mask
                           DataType::DATA_TYPE_FLOAT,              // text_token_mask
                           DataType::DATA_TYPE_FLOAT};             // proposals
    return COSMO_NN_OK;
}

std::string SophonDinoEncodeNode::Strip(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (first == std::string::npos)
        return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
}

bool SophonDinoEncodeNode::Endswith(const std::string& s, const std::string& suffix) {
    if (suffix.length() > s.length())
        return false;
    return s.compare(s.length() - suffix.length(), suffix.length(), suffix) == 0;
}

void SophonDinoEncodeNode::CreateTokenizer(std::string path) {
    if (path.empty())
        return;

    // Try to load vocab.txt (simple tokenizer)
    std::string vocab_path = path;
    if (vocab_path.find(".json") != std::string::npos) {
        // If it's a json file, try to find vocab.txt in the same directory
        size_t last_slash = vocab_path.find_last_of("/\\");
        if (last_slash != std::string::npos) {
            vocab_path = vocab_path.substr(0, last_slash + 1) + "vocab.txt";
        } else {
            vocab_path = "vocab.txt";
        }
    }

    tokenizer.reset(new SophonTokenizer());
    if (!tokenizer->LoadVocab(vocab_path)) {
        tokenizer.reset();
        return;
    }

    // Initialize special token ids
    special_token_ids.clear();
    for (const auto& token_str : special_tokens) {
        if (tokenizer->token2idx.find(token_str) != tokenizer->token2idx.end()) {
            special_token_ids.push_back(tokenizer->token2idx[token_str]);
        }
    }
    // Also add hardcoded special tokens
    if (std::find(special_token_ids.begin(), special_token_ids.end(), 101) == special_token_ids.end()) {
        special_token_ids.push_back(101);  // [CLS]
    }
    if (std::find(special_token_ids.begin(), special_token_ids.end(), 102) == special_token_ids.end()) {
        special_token_ids.push_back(102);  // [SEP]
    }
}

std::vector<int64_t> SophonDinoEncodeNode::Encode(const std::string& text) {
    if (!tokenizer)
        return std::vector<int64_t>();

    std::vector<int64_t> ids;
    tokenizer->EncodeText(text, ids);
    return ids;
}

std::string SophonDinoEncodeNode::GetPromptStr(std::shared_ptr<Blob>& prompt) {
    auto blob_desc   = prompt->GetBlobDesc();
    auto blob_handle = prompt->GetHandle();

    auto dims     = blob_desc.dims;
    const int len = DimsVectorUtils::Count(dims);

    return std::string(reinterpret_cast<char*>(blob_handle.base), len);
}

Status SophonDinoEncodeNode::Forward(std::vector<std::shared_ptr<Blob>>& images,
                                     std::vector<std::shared_ptr<Blob>>& prompts,
                                     std::vector<std::shared_ptr<Blob>>& top_blobs) {
    if (!tokenizer) {
        CreateTokenizer(shared_resource->tokenizer_path);
        if (!tokenizer)
            return Status(COSMO_NN_ERR_PARAM, "create tokenizer failed");
        shared_resource->tokenizer_handle = tokenizer.get();
    }

    timer.Start();
    RETURN_ON_FAIL(CheckNodeInputOutput(images, top_blobs, true));
    RETURN_ON_FAIL(CheckNodeInputOutput(prompts, top_blobs, false));

    bm_status_t ret       = BM_SUCCESS;
    bm_handle_t pbmhandle = shared_resource->m_handle;
    if (nullptr == pbmhandle)
        return Status(COSMO_NN_ERR_SOPHON_HANDLE_FAILED, "bm_handle_t is null !");

    // bottoms
    auto image_blob  = images.at(0);
    auto prompt_blob = prompts.at(0);

    // tops
    auto top_image                     = top_blobs.at(0);  // samples
    auto top_position_ids              = top_blobs.at(1);
    auto top_text_self_attention_masks = top_blobs.at(2);
    auto top_input_ids                 = top_blobs.at(3);
    auto top_token_type_ids            = top_blobs.at(4);
    auto top_attention_mask            = top_blobs.at(5);
    auto top_text_token_mask           = top_blobs.at(6);
    auto top_proposals                 = top_blobs.at(7);

    // Get prompt string
    auto prompt = GetPromptStr(prompt_blob);
    // Convert to lowercase and strip
    std::string caption = prompt;
    std::transform(caption.begin(), caption.end(), caption.begin(), ::tolower);
    caption = Strip(caption);

    // Remove trailing dots
    while (!caption.empty() && Endswith(caption, ".")) {
        caption.pop_back();
    }

    if (caption.empty())
        return Status(COSMO_NN_ERR_PARAM, "prompt is empty after processing");

    // Encode text
    auto token = Encode(caption);
    if (token.empty())
        return Status(COSMO_NN_ERR_PARAM, "prompt is empty");
    if (token.size() > length - 2)
        return Status(COSMO_NN_ERR_PARAM, "prompt is too long");

    // Ensure [CLS] and [SEP] are present at the correct positions
    // Following the same logic as in the demo
    if (token.empty() || token[0] != 101) {
        token.insert(token.begin(), 101);  // [CLS] at beginning
    }
    if (token.empty() || token.back() != 102) {
        token.push_back(102);  // [SEP] at end
    }

    const auto token_len = token.size();
    const auto input_ids = token;
    // Pad to fixed length with zeros
    if (token_len < length) {
        token.insert(token.end(), length - token_len, 0);
    }
    // Ensure we don't exceed the maximum length
    else if (token_len > length) {
        token.resize(length);
        // Make sure the last element is [SEP] if it was originally there
        if (token_len > 0 && input_ids.back() == 102) {
            token[length - 1] = 102;  // [SEP]
        }
    }

    shared_resource->prompt_token_ids.clear();
    for (auto id : token) {
        shared_resource->prompt_token_ids.push_back(static_cast<int32_t>(id));
    }

    // ========== Process Image ==========
    auto image_blob_desc   = image_blob->GetBlobDesc();
    auto image_blob_handle = image_blob->GetHandle();
    auto image_dims        = image_blob_desc.dims;
    auto image_fmt         = image_blob_desc.image_format;

    if (!ImageFormatIsBGR(image_fmt) && !ImageFormatIsRGB(image_fmt))
        return Status(COSMO_NN_ERR_PARAM, "image format is not supported");

    int batch = image_dims.at(0);
    if (batch > max_batch)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "batch size too large");

    SetCurrentBatch(top_image, batch);
    SetCurrentBatch(top_position_ids, batch);
    SetCurrentBatch(top_text_self_attention_masks, batch);
    SetCurrentBatch(top_input_ids, batch);
    SetCurrentBatch(top_token_type_ids, batch);
    SetCurrentBatch(top_attention_mask, batch);
    SetCurrentBatch(top_text_token_mask, batch);
    SetCurrentBatch(top_proposals, batch);

    uint32_t src_width  = image_dims.at(2);
    uint32_t src_height = image_dims.at(1);

    // Check if we need to swap RB
    // is_bgr=false means model expects RGB format
    bool swap_rb = !(is_bgr == ImageFormatIsBGR(image_fmt));

    // Create source image
    // Input data is BGR_PACKED (HWC format) from LoadImage, so we need to create PACKED format first
    // Then convert to PLANAR format for further processing
    bm_image src_packed_image;
    bm_image_format_ext src_packed_format =
        ImageFormatIsBGR(image_fmt) ? FORMAT_BGR_PACKED : FORMAT_RGB_PACKED;
    ret = bm_image_create(pbmhandle, src_height, src_width, src_packed_format, DATA_TYPE_EXT_1N_BYTE,
                          &src_packed_image);
    if (ret != BM_SUCCESS) {
        return Status(COSMO_NN_ERR_SOPHON_BMIMAGE_CREAT_FAILED, "bm_image creat failed.");
    }

    bm_device_mem_t* src_dev_mem = reinterpret_cast<bm_device_mem_t*>(image_blob_handle.base);
    ret                          = bm_image_attach(src_packed_image, src_dev_mem);
    if (ret != BM_SUCCESS) {
#ifdef COSMO_NN_SOPHON_1684X
        bm_image_destroy(src_packed_image);
#else
        bm_image_destroy(&src_packed_image);
#endif
        return Status(COSMO_NN_ERR_SOPHON_ATTACH_FAILED, "bm_image attach failed.");
    }

    // Convert PACKED to PLANAR format
    bm_image src_image;
    bm_image_format_ext src_format = ImageFormatIsBGR(image_fmt) ? FORMAT_BGR_PLANAR : FORMAT_RGB_PLANAR;
    ret = bm_image_create(pbmhandle, src_height, src_width, src_format, DATA_TYPE_EXT_1N_BYTE, &src_image);
    if (ret != BM_SUCCESS) {
        bm_image_detach(src_packed_image);
#ifdef COSMO_NN_SOPHON_1684X
        bm_image_destroy(src_packed_image);
#else
        bm_image_destroy(&src_packed_image);
#endif
        return Status(COSMO_NN_ERR_SOPHON_BMIMAGE_CREAT_FAILED, "bm_image creat failed.");
    }

    // Convert from PACKED to PLANAR
    ret = bmcv_image_vpp_convert(pbmhandle, 1, src_packed_image, &src_image, NULL);
    if (ret != BM_SUCCESS) {
        bm_image_detach(src_packed_image);
#ifdef COSMO_NN_SOPHON_1684X
        bm_image_destroy(src_packed_image);
        bm_image_destroy(src_image);
#else
        bm_image_destroy(&src_packed_image);
        bm_image_destroy(&src_image);
#endif
        return Status(COSMO_NN_ERR_SOPHON_INFER_ERR, "bmcv_image_vpp_convert PACKED to PLANAR failed.");
    }

    // Detach and destroy packed image (no longer needed)
    bm_image_detach(src_packed_image);
#ifdef COSMO_NN_SOPHON_1684X
    bm_image_destroy(src_packed_image);
#else
    bm_image_destroy(&src_packed_image);
#endif

    // Create intermediate image for format conversion (if needed)
    // If we need to convert BGR->RGB, do it before resize to ensure proper conversion
    bm_image format_converted_image;
    bm_image_format_ext target_format = is_bgr ? FORMAT_BGR_PLANAR : FORMAT_RGB_PLANAR;
    bool need_format_convert          = (src_format != target_format);

    bm_image* resize_input_image = &src_image;

    if (need_format_convert) {
        // Create format-converted image (same size as source)
        ret = bm_image_create(pbmhandle, src_height, src_width, target_format, DATA_TYPE_EXT_1N_BYTE,
                              &format_converted_image);
        if (ret != BM_SUCCESS) {
            bm_image_detach(src_image);
#ifdef COSMO_NN_SOPHON_1684X
            bm_image_destroy(src_image);
#else
            bm_image_destroy(&src_image);
#endif
            return Status(COSMO_NN_ERR_SOPHON_BMIMAGE_CREAT_FAILED, "bm_image creat failed.");
        }

        // Convert format first
        ret = bmcv_image_vpp_convert(pbmhandle, 1, src_image, &format_converted_image, NULL);
        if (ret != BM_SUCCESS) {
            bm_image_detach(src_image);
#ifdef COSMO_NN_SOPHON_1684X
            bm_image_destroy(src_image);
            bm_image_destroy(format_converted_image);
#else
            bm_image_destroy(&src_image);
            bm_image_destroy(&format_converted_image);
#endif
            return Status(COSMO_NN_ERR_SOPHON_INFER_ERR, "bmcv_image_vpp_convert format conversion failed.");
        }
        resize_input_image = &format_converted_image;
    }

    // Create resize image (intermediate)
    // Use RGB format if is_bgr=false, BGR format if is_bgr=true
    bm_image resize_image;
    bm_image_format_ext resize_format = target_format;
    ret = bm_image_create(pbmhandle, dst_height, dst_width, resize_format, DATA_TYPE_EXT_1N_BYTE,
                          &resize_image);
    if (ret != BM_SUCCESS) {
        bm_image_detach(src_image);
#ifdef COSMO_NN_SOPHON_1684X
        bm_image_destroy(src_image);
#else
        bm_image_destroy(&src_image);
#endif
        return Status(COSMO_NN_ERR_SOPHON_BMIMAGE_CREAT_FAILED, "bm_image creat failed.");
    }

    // Resize image (format should already be converted if needed)
    // Use bmcv_image_vpp_convert_padding to specify LINEAR interpolation (matching Demo's cv::INTER_LINEAR)
    // Even if we don't need padding, we can use this function to specify the interpolation method
    bmcv_rect_t crop_rect;
    crop_rect.start_x = 0;
    crop_rect.start_y = 0;
    crop_rect.crop_w  = (*resize_input_image).width;
    crop_rect.crop_h  = (*resize_input_image).height;

    // Create padding_attr with no padding (full resize to dst size)
    bmcv_padding_atrr_t padding_attr;
    memset(&padding_attr, 0, sizeof(padding_attr));
    padding_attr.dst_crop_sty = 0;
    padding_attr.dst_crop_stx = 0;
    padding_attr.dst_crop_h   = dst_height;
    padding_attr.dst_crop_w   = dst_width;
    padding_attr.if_memset    = 0;  // No padding needed

    // Use BMCV_INTER_LINEAR to match Demo's cv::INTER_LINEAR
    ret = bmcv_image_vpp_convert_padding(pbmhandle, 1, *resize_input_image, &resize_image, &padding_attr,
                                         &crop_rect, BMCV_INTER_LINEAR);
    if (ret != BM_SUCCESS) {
        bm_image_detach(src_image);
#ifdef COSMO_NN_SOPHON_1684X
        bm_image_destroy(src_image);
        bm_image_destroy(resize_image);
#else
        bm_image_destroy(&src_image);
        bm_image_destroy(&resize_image);
#endif
        if (need_format_convert) {
            bm_image_detach(format_converted_image);
#ifdef COSMO_NN_SOPHON_1684X
            bm_image_destroy(format_converted_image);
#else
            bm_image_destroy(&format_converted_image);
#endif
        }
        return Status(COSMO_NN_ERR_SOPHON_INFER_ERR, "bmcv_image_vpp_convert resize failed.");
    }

    // Create normalized image (output)
    // Use same format as resize_image to maintain channel order
    bm_image norm_image;
    ret =
        bm_image_create(pbmhandle, dst_height, dst_width, resize_format, DATA_TYPE_EXT_FLOAT32, &norm_image);
    if (ret != BM_SUCCESS) {
        bm_image_detach(src_image);
#ifdef COSMO_NN_SOPHON_1684X
        bm_image_destroy(src_image);
#else
        bm_image_destroy(&src_image);
#endif
        bm_image_detach(resize_image);
#ifdef COSMO_NN_SOPHON_1684X
        bm_image_destroy(resize_image);
#else
        bm_image_destroy(&resize_image);
#endif
        return Status(COSMO_NN_ERR_SOPHON_BMIMAGE_CREAT_FAILED, "bm_image creat failed.");
    }

    bm_device_mem_t* dst_dev_mem = reinterpret_cast<bm_device_mem_t*>(top_image->GetHandle().base);
    unsigned long long mem_addr;
    bm_device_mem_t output_addr[3];
    mem_addr       = bm_mem_get_device_addr(*dst_dev_mem);
    int size       = dst_width * dst_height;
    output_addr[0] = bm_mem_from_device(mem_addr, size * sizeof(float));
    auto g_addr    = (unsigned long long)((float*)mem_addr + size);
    output_addr[1] = bm_mem_from_device(g_addr, size * sizeof(float));
    auto r_addr    = (unsigned long long)((float*)g_addr + size);
    output_addr[2] = bm_mem_from_device(r_addr, size * sizeof(float));
    ret            = bm_image_attach(norm_image, output_addr);
    if (ret != BM_SUCCESS) {
        bm_image_detach(src_image);
#ifdef COSMO_NN_SOPHON_1684X
        bm_image_destroy(src_image);
#else
        bm_image_destroy(&src_image);
#endif
        bm_image_detach(resize_image);
#ifdef COSMO_NN_SOPHON_1684X
        bm_image_destroy(resize_image);
        bm_image_destroy(norm_image);
#else
        bm_image_destroy(&resize_image);
        bm_image_destroy(&norm_image);
#endif
        return Status(COSMO_NN_ERR_SOPHON_ATTACH_FAILED, "bm_image attach failed.");
    }

    // Normalize: (img / 255.0 - mean) / std = img * (1/255) * (1/std) - mean * (1/std)
    // For FP16 models, model_input_scale should be 1.0, so we use standard normalization
    // For INT8 quantized models, model_input_scale is used for quantization
    bmcv_convert_to_attr converto_attr;
    float bm_model_input_scale = shared_resource->model_input_scale;
    // Standard normalization: (img / 255.0 - mean) / std
    // Convert to: img * (1/255) * (1/std) - mean * (1/std)
    // For GroundingDINO FP16 model, use standard normalization to match demo
    float alpha = (1.0f / 255.0f);

    // For FORMAT_RGB_PLANAR: alpha_0=R, alpha_1=G, alpha_2=B
    // Demo uses RGB format with mean=[0.485, 0.456, 0.406] for R, G, B channels
    // After BGR->RGB conversion via bmcv_image_vpp_convert, resize_image is in RGB format
    // So we should use normal normalization params: R->mean[0], G->mean[1], B->mean[2]
    // The format conversion correctly swaps the channels, so resize_image data is already RGB
    converto_attr.alpha_0 = alpha * scale[0];  // R channel
    converto_attr.beta_0  = -mean[0] * scale[0];
    converto_attr.alpha_1 = alpha * scale[1];  // G channel
    converto_attr.beta_1  = -mean[1] * scale[1];
    converto_attr.alpha_2 = alpha * scale[2];  // B channel
    converto_attr.beta_2  = -mean[2] * scale[2];

    ret = bmcv_image_convert_to(pbmhandle, 1, converto_attr, &resize_image, &norm_image);
    if (ret != BM_SUCCESS) {
        bm_image_detach(src_image);
#ifdef COSMO_NN_SOPHON_1684X
        bm_image_destroy(src_image);
#else
        bm_image_destroy(&src_image);
#endif
        bm_image_detach(resize_image);
#ifdef COSMO_NN_SOPHON_1684X
        bm_image_destroy(resize_image);
#else
        bm_image_destroy(&resize_image);
#endif
        bm_image_detach(norm_image);
#ifdef COSMO_NN_SOPHON_1684X
        bm_image_destroy(norm_image);
#else
        bm_image_destroy(&norm_image);
#endif
        return Status(COSMO_NN_ERR_SOPHON_INFER_ERR, "bmcv_image_convert_to failed.");
    }

    // Cleanup images
    bm_image_detach(src_image);
#ifdef COSMO_NN_SOPHON_1684X
    bm_image_destroy(src_image);
#else
    bm_image_destroy(&src_image);
#endif
    if (need_format_convert) {
        bm_image_detach(format_converted_image);
#ifdef COSMO_NN_SOPHON_1684X
        bm_image_destroy(format_converted_image);
#else
        bm_image_destroy(&format_converted_image);
#endif
    }
    bm_image_detach(resize_image);
#ifdef COSMO_NN_SOPHON_1684X
    bm_image_destroy(resize_image);
#else
    bm_image_destroy(&resize_image);
#endif
    bm_image_detach(norm_image);
#ifdef COSMO_NN_SOPHON_1684X
    bm_image_destroy(norm_image);
#else
    bm_image_destroy(&norm_image);
#endif

    // ========== Process Text Inputs ==========
    // Prepare text inputs on host
    std::vector<int32_t> position_ids(length, 0);
    std::vector<float> text_token_mask(length, 0.0f);
    std::vector<float> attention_mask(length * length, 0.0f);
    std::vector<float> text_self_attention_masks(length * length, 0.0f);

    // Fill input_ids, token_type_ids, text_token_mask
    for (size_t i = 0; i < token_len; i++) {
        text_token_mask[i] = (token[i] > 0) ? 1.0f : 0.0f;
    }

    // Initialize attention_mask and text_self_attention_masks (diagonal)
    for (size_t i = 0; i < length; i++) {
        attention_mask[i * length + i]            = 1.0f;
        text_self_attention_masks[i * length + i] = 1.0f;
    }

    // Find special token positions
    std::vector<int> idxs;
    for (size_t i = 0; i < input_ids.size(); i++) {
        for (size_t j = 0; j < special_token_ids.size(); j++) {
            if (input_ids[i] == static_cast<int64_t>(special_token_ids[j])) {
                idxs.push_back(i);
                break;
            }
        }
    }

    // Fill attention masks and position_ids based on special tokens
    int previous_col = 0;
    for (size_t i = 0; i < idxs.size(); i++) {
        const int col = idxs[i];
        if (col == 0 || col == static_cast<int>(token_len - 1)) {
            attention_mask[col * length + col]            = 1.0f;
            text_self_attention_masks[col * length + col] = 1.0f;
            position_ids[col]                             = 0;
        } else {
            for (int j = previous_col + 1; j <= col; j++) {
                for (int k = previous_col + 1; k <= col; k++) {
                    attention_mask[j * length + k]            = 1.0f;
                    text_self_attention_masks[j * length + k] = 1.0f;
                }
                position_ids[j] = j - (previous_col + 1);
            }
        }
        previous_col = col;
    }

    // Generate proposals
    const int proposals_count = 100 * 100 + 50 * 50 + 25 * 25 + 13 * 13;  // 13294
    std::vector<std::vector<int>> spatial_shapes{{100, 100}, {50, 50}, {25, 25}, {13, 13}};
    std::vector<float> proposals(proposals_count * 4, 0.0f);
    int idx = 0;
    for (size_t i = 0; i < spatial_shapes.size(); i++) {
        int valid_H = spatial_shapes[i][0];
        int valid_W = spatial_shapes[i][1];
        float wh    = 0.05f * std::pow(2.0f, static_cast<float>(i));
        for (int h_i = 0; h_i < spatial_shapes[i][0]; h_i++) {
            for (int w_i = 0; w_i < spatial_shapes[i][1]; w_i++) {
                proposals[idx++] = (static_cast<float>(w_i) + 0.5f) / valid_W;  // cx
                proposals[idx++] = (static_cast<float>(h_i) + 0.5f) / valid_H;  // cy
                proposals[idx++] = wh;                                          // w
                proposals[idx++] = wh;                                          // h
            }
        }
    }

    // Copy data to device
    bm_device_mem_t* position_ids_mem =
        reinterpret_cast<bm_device_mem_t*>(top_position_ids->GetHandle().base);
    bm_device_mem_t* text_self_attention_masks_mem =
        reinterpret_cast<bm_device_mem_t*>(top_text_self_attention_masks->GetHandle().base);
    bm_device_mem_t* input_ids_mem = reinterpret_cast<bm_device_mem_t*>(top_input_ids->GetHandle().base);
    bm_device_mem_t* token_type_ids_mem =
        reinterpret_cast<bm_device_mem_t*>(top_token_type_ids->GetHandle().base);
    bm_device_mem_t* attention_mask_mem =
        reinterpret_cast<bm_device_mem_t*>(top_attention_mask->GetHandle().base);
    bm_device_mem_t* text_token_mask_mem =
        reinterpret_cast<bm_device_mem_t*>(top_text_token_mask->GetHandle().base);
    bm_device_mem_t* proposals_mem = reinterpret_cast<bm_device_mem_t*>(top_proposals->GetHandle().base);

    // Copy position_ids (Input 1)
    size_t position_ids_size = position_ids.size() * sizeof(int32_t);
    ret = bm_memcpy_s2d_partial(pbmhandle, *position_ids_mem, position_ids.data(), position_ids_size);
    if (ret != BM_SUCCESS) {
        return Status(COSMO_NN_ERR_SOPHON_MEMCPY_ERR, "bm_memcpy_s2d_partial position_ids failed.");
    }

    // Copy text_self_attention_masks (Input 2)
    size_t text_self_attention_masks_size = text_self_attention_masks.size() * sizeof(float);
    ret = bm_memcpy_s2d_partial(pbmhandle, *text_self_attention_masks_mem, text_self_attention_masks.data(),
                                text_self_attention_masks_size);
    if (ret != BM_SUCCESS) {
        return Status(COSMO_NN_ERR_SOPHON_MEMCPY_ERR,
                      "bm_memcpy_s2d_partial text_self_attention_masks failed.");
    }

    // Copy input_ids (Input 3)
    std::vector<int32_t> input_ids_int32(token.begin(), token.end());
    size_t input_ids_size = input_ids_int32.size() * sizeof(int32_t);
    ret = bm_memcpy_s2d_partial(pbmhandle, *input_ids_mem, input_ids_int32.data(), input_ids_size);
    if (ret != BM_SUCCESS) {
        return Status(COSMO_NN_ERR_SOPHON_MEMCPY_ERR, "bm_memcpy_s2d_partial input_ids failed.");
    }

    // Copy token_type_ids (Input 4)
    std::vector<int32_t> token_type_ids_int32(length, 0);
    size_t token_type_ids_size = token_type_ids_int32.size() * sizeof(int32_t);
    ret = bm_memcpy_s2d_partial(pbmhandle, *token_type_ids_mem, token_type_ids_int32.data(),
                                token_type_ids_size);
    if (ret != BM_SUCCESS) {
        return Status(COSMO_NN_ERR_SOPHON_MEMCPY_ERR, "bm_memcpy_s2d_partial token_type_ids failed.");
    }

    // Copy attention_mask (Input 5)
    size_t attention_mask_size = attention_mask.size() * sizeof(float);
    ret = bm_memcpy_s2d_partial(pbmhandle, *attention_mask_mem, attention_mask.data(), attention_mask_size);
    if (ret != BM_SUCCESS) {
        return Status(COSMO_NN_ERR_SOPHON_MEMCPY_ERR, "bm_memcpy_s2d_partial attention_mask failed.");
    }

    // Copy text_token_mask (Input 6)
    size_t text_token_mask_size = text_token_mask.size() * sizeof(float);
    ret =
        bm_memcpy_s2d_partial(pbmhandle, *text_token_mask_mem, text_token_mask.data(), text_token_mask_size);
    if (ret != BM_SUCCESS) {
        return Status(COSMO_NN_ERR_SOPHON_MEMCPY_ERR, "bm_memcpy_s2d_partial text_token_mask failed.");
    }

    // Copy proposals (Input 7)
    size_t proposals_size = proposals.size() * sizeof(float);
    ret = bm_memcpy_s2d_partial(pbmhandle, *proposals_mem, proposals.data(), proposals_size);
    if (ret != BM_SUCCESS) {
        return Status(COSMO_NN_ERR_SOPHON_MEMCPY_ERR, "bm_memcpy_s2d_partial proposals failed.");
    }

    timer.Stop();
    return COSMO_NN_OK;
}

}  // namespace cosmo::nn
