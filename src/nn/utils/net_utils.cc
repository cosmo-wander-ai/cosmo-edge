#include "nn/utils/net_utils.h"

#include <algorithm>
#include <cstdio>
#include <sstream>
#include <utility>
#include <vector>

#include "Eigen/Dense"
#include "nn/device/sophon/sophon_dino_encode_node.h"
#include "nn/utils/dims_vector_utils.h"
#include "tokenizers_c.h"

#ifdef COSMO_NN_USE_SOPHON_BACKEND
#include "bmcv_api.h"
#include "bmcv_api_ext.h"
#include "bmlib_runtime.h"
#include "bmruntime_cpp.h"
#endif

namespace cosmo::nn {

Size::Size(int w, int h) : width(w), height(h) {}

static ObjectInfo ScaleSize(ObjectInfo& info, Size old_, Size new_) {
    float scale_x = new_.width / float(old_.width);
    float scale_y = new_.height / float(old_.height);

    ObjectInfo new_info;
    new_info.class_id   = info.class_id;
    new_info.confidence = info.confidence;
    new_info.angle      = info.angle;
    new_info.class_name = info.class_name;

    int x_min = std::min(info.x1, info.x2) * scale_x;
    int x_max = std::max(info.x1, info.x2) * scale_x;
    int y_min = std::min(info.y1, info.y2) * scale_y;
    int y_max = std::max(info.y1, info.y2) * scale_y;

    x_min = std::min(std::max(x_min, 0), new_.width - 1);
    x_max = std::min(std::max(x_max, 0), new_.width - 1);
    y_min = std::min(std::max(y_min, 0), new_.height - 1);
    y_max = std::min(std::max(y_max, 0), new_.height - 1);

    new_info.x1 = x_min;
    new_info.x2 = x_max;
    new_info.y1 = y_min;
    new_info.y2 = y_max;

    // key point
    std::vector<std::pair<float, float>> key_points;
    for (auto item : info.key_points) {
        key_points.emplace_back(item.first * scale_x, item.second * scale_y);
    }
    new_info.key_points = key_points;

    // key point 3d
    std::vector<triple<float, float, float>> key_points_3d;
    for (auto item : info.key_points_3d) {
        key_points_3d.emplace_back(std::get<0>(item) * scale_x, std::get<1>(item) * scale_y,
                                   std::get<2>(item));
    }
    new_info.key_points_3d = key_points_3d;

    new_info.key_point_confidences = info.key_point_confidences;

    return new_info;
}

static ObjectInfo AddOffset(ObjectInfo& info, float offset_x, float offset_y) {
    ObjectInfo offset_info;
    offset_info.angle      = info.angle;
    offset_info.class_id   = info.class_id;
    offset_info.confidence = info.confidence;
    offset_info.class_name = info.class_name;

    offset_info.x1 = info.x1 + offset_x;
    offset_info.x2 = info.x2 + offset_x;
    offset_info.y1 = info.y1 + offset_y;
    offset_info.y2 = info.y2 + offset_y;

    // key point
    std::vector<std::pair<float, float>> key_points;
    for (auto item : info.key_points) {
        key_points.emplace_back(item.first + offset_x, item.second + offset_y);
    }
    offset_info.key_points = key_points;

    // key point 3d
    std::vector<triple<float, float, float>> key_points_3d;
    for (auto item : info.key_points_3d) {
        key_points_3d.emplace_back(std::get<0>(item) + offset_x, std::get<1>(item) + offset_y,
                                   std::get<2>(item));
    }
    offset_info.key_points_3d = key_points_3d;

    offset_info.key_point_confidences = info.key_point_confidences;

    return offset_info;
}

float NetUtils::Clamp(float x, float low, float high) {
    return std::max(low, std::min(x, high));
}

float NetUtils::Exp(float x, int terms) {
    float result = 1.0;
    float term   = 1.0;

    for (int i = 1; i <= terms; ++i) {
        term *= x / i;
        result += term;
    }

    return result;
}

float NetUtils::Sigmoid(float x) {
    return 1.0 / (1.0 + Exp(-x, 10));
}

void NetUtils::SwapYUV_I420toYUV_NV12(unsigned char* i420bytes, unsigned char* nv12bytes, int width,
                                      int height) {
    int nLenY = width * height;
    int nLenU = nLenY / 4;

    memcpy(nv12bytes, i420bytes, width * height);

    for (int i = 0; i < nLenU; i++) {
        nv12bytes[nLenY + 2 * i]     = i420bytes[nLenY + i];          // U
        nv12bytes[nLenY + 2 * i + 1] = i420bytes[nLenY + nLenU + i];  // V
    }
}

void NetUtils::NMS(std::vector<ObjectInfo>& input, std::vector<ObjectInfo>& output, float iou_threshold_) {
    std::sort(input.begin(), input.end(),
              [](const ObjectInfo& a, const ObjectInfo& b) { return a.confidence > b.confidence; });
    output.clear();

    int bbox_num = input.size();
    std::vector<int> merged(bbox_num, 0);
    for (int i = 0; i < bbox_num; i++) {
        if (merged.at(i))
            continue;

        std::vector<ObjectInfo> buf;
        buf.push_back(input.at(i));

        merged.at(i) = 1;

        float h0    = input.at(i).y2 - input.at(i).y1;
        float w0    = input.at(i).x2 - input.at(i).x1;
        float area0 = h0 * w0;

        for (int j = i + 1; j < bbox_num; j++) {
            if (merged.at(j))
                continue;

            float inner_x1 = input.at(i).x1 > input.at(j).x1 ? input.at(i).x1 : input.at(j).x1;
            float inner_y1 = input.at(i).y1 > input.at(j).y1 ? input.at(i).y1 : input.at(j).y1;

            float inner_x2 = input.at(i).x2 < input.at(j).x2 ? input.at(i).x2 : input.at(j).x2;
            float inner_y2 = input.at(i).y2 < input.at(j).y2 ? input.at(i).y2 : input.at(j).y2;

            float inner_h = inner_y2 - inner_y1;
            float inner_w = inner_x2 - inner_x1;

            if (inner_h <= 0 || inner_w <= 0)
                continue;

            float inner_area = inner_h * inner_w;

            float h1 = input.at(j).y2 - input.at(j).y1;
            float w1 = input.at(j).x2 - input.at(j).x1;

            float area1 = h1 * w1;

            float score = inner_area / (area0 + area1 - inner_area);
            if (score > iou_threshold_) {
                merged.at(j) = 1;
                buf.push_back(input.at(j));
            }
        }

        output.push_back(buf.at(0));
    }
}

Status NetUtils::PickDetectionObjects(std::shared_ptr<Blob>& blob, std::vector<Size> input_sizes,
                                      Size net_input_size, std::vector<int> selected_indcies,
                                      std::vector<float> selected_thresholds,
                                      std::vector<std::string> selected_label,
                                      std::vector<std::vector<ObjectInfoV1>>& detects) {
    auto desc   = blob->GetBlobDesc();
    auto dims   = desc.dims;
    auto handle = blob->GetHandle();

    float* data = static_cast<float*>(handle.base);

    int batch_size = dims.at(0);
    int row        = dims.at(1);
    int col        = dims.at(2);

    // Support partial batch: only process the number of images actually provided
    int actual_batch = static_cast<int>(input_sizes.size());
    if (actual_batch > batch_size)
        return Status(COSMO_NN_ERR_PARAM, "Incompatible batch size: input images more than model batch");

    // Fallback when no labels configured: accept all classes with default threshold 0.25
    const float default_threshold = 0.25f;
    bool use_fallback             = selected_indcies.empty();

    for (int b = 0; b < actual_batch; b++) {
        float* b_data = data + b * row * col;
        std::vector<ObjectInfoV1> extracted_objs;
        for (int i = 0; i < row; i++) {
            // center
            float x      = b_data[i * col + 0];
            float y      = b_data[i * col + 1];
            float w      = b_data[i * col + 2];
            float h      = b_data[i * col + 3];
            float c      = b_data[i * col + 4];
            int class_id = static_cast<int>(b_data[i * col + 5]);

            if (std::isnan(c))
                continue;

            int index = -1;
            if (use_fallback) {
                index = 0;  // use default for all
            } else {
                auto iter = std::find(selected_indcies.begin(), selected_indcies.end(), class_id);
                if (iter == selected_indcies.end())
                    continue;
                index = static_cast<int>(iter - selected_indcies.begin());
            }

            float thresh = (use_fallback || index >= static_cast<int>(selected_thresholds.size()))
                               ? default_threshold
                               : selected_thresholds.at(index);
            if (c < thresh)
                continue;

            // compute bbox coords
            float x1 = x - w / 2;
            float y1 = y - h / 2;
            float x2 = x + w / 2;
            float y2 = y + h / 2;

            ObjectInfoV1 obj_info;
            obj_info.x1 = x1;
            obj_info.y1 = y1;
            obj_info.x2 = x2;
            obj_info.y2 = y2;

            ClassifyInfo info;
            info.confidence = c;
            info.class_id   = class_id;
            if (use_fallback || index >= static_cast<int>(selected_label.size())) {
                char buf[32];
                snprintf(buf, sizeof(buf), "class_%d", class_id);
                info.class_name = buf;
            } else {
                info.class_name = selected_label.at(index);
            }

            obj_info.infos.push_back(info);

            extracted_objs.emplace_back(obj_info);
        }

        for (auto& obj : extracted_objs) {
            obj = AdjustSize(obj, net_input_size, input_sizes.at(b), 2);
        }

        detects.push_back(extracted_objs);
    }

    return COSMO_NN_OK;
}

ObjectInfo NetUtils::AdjustSize(ObjectInfo& info, Size old_, Size new_, int gravity) {
    float old_aspect = old_.height / (float)(old_.width + FLT_EPSILON);
    float new_aspect = new_.height / (float)(new_.width + FLT_EPSILON);

    if (gravity == 2) {
        if (new_aspect > old_aspect) {
            float old_aspect_width = new_.height / old_aspect;
            auto info_aspect       = ScaleSize(info, old_, Size(old_aspect_width, new_.height));

            float offset_x = (old_aspect_width - new_.width) / 2;
            return AddOffset(info_aspect, -offset_x, 0);
        } else {
            float old_aspect_height = new_.width * old_aspect;
            auto info_aspect        = ScaleSize(info, old_, Size(new_.width, old_aspect_height));

            float offset_y = (old_aspect_height - new_.height) / 2;
            return AddOffset(info_aspect, 0, -offset_y);
        }
    } else if (gravity == 1) {
        if (new_aspect > old_aspect) {
            float old_aspect_height = new_.width * old_aspect;
            auto info_aspect        = ScaleSize(info, old_, Size(new_.width, old_aspect_height));

            float offset_y = (old_aspect_height - new_.height) / 2;
            return AddOffset(info_aspect, 0, -offset_y);
        } else {
            float old_aspect_width = new_.height / old_aspect;
            auto info_aspect       = ScaleSize(info, old_, Size(old_aspect_width, new_.height));

            float offset_x = (old_aspect_width - new_.width) / 2;
            return AddOffset(info_aspect, -offset_x, 0);
        }
    } else {
        return ScaleSize(info, old_, new_);
    }
}

ObjectInfoV1 NetUtils::AdjustSize(ObjectInfoV1& info_v1, Size old_, Size new_, int gravity) {
    ObjectInfo info;
    info.x1 = info_v1.x1;
    info.x2 = info_v1.x2;
    info.y1 = info_v1.y1;
    info.y2 = info_v1.y2;

    auto adjusted_info =
        AdjustSize(info, Size(old_.width, old_.height), Size(new_.width, new_.height), gravity);

    ObjectInfoV1 result = info_v1;
    result.x1           = adjusted_info.x1;
    result.x2           = adjusted_info.x2;
    result.y1           = adjusted_info.y1;
    result.y2           = adjusted_info.y2;

    return result;
}

Status NetUtils::GenerateCropParam(RectCrop* rect_crop, int left, int top, int right, int bottom, Size size,
                                   CropParam& param) {
    if (!rect_crop)
        return Status(COSMO_NN_ERR_NULL_PARAM, "Op rect_crop must not be nullptr");

    int x1 = left;
    int y1 = top;
    int x2 = right;
    int y2 = bottom;
    int w  = x2 - x1 + 1;
    int h  = y2 - y1 + 1;

    if (rect_crop->square) {
        int base_len = 0;
        if (rect_crop->square_mode == 0) {
            base_len = std::max(w, h);
        } else if (rect_crop->square_mode == 1) {
            base_len = std::min(w, h);
        } else if (rect_crop->square_mode == 2) {
            base_len = (w + h) / 2;
        } else {
            return Status(COSMO_NN_ERR_PARAM, "Invalid square_mode");
        }

        int center_x = left + w / 2;
        int center_y = top + h / 2;

        x1 = center_x - base_len * (rect_crop->w_left_crop.at(0) + 0.5f);
        x2 = center_x + base_len * (rect_crop->w_right_crop.at(0) + 0.5f);
        y1 = center_y - base_len * (rect_crop->h_top_crop.at(0) + 0.5f);
        y2 = center_y + base_len * (rect_crop->h_bottom_crop.at(0) + 0.5f);

        x1 = std::max(0, x1);
        y1 = std::max(0, y1);
        x2 = std::min(x2, size.width - 1);
        y2 = std::min(y2, size.height - 1);

        param.top_left_x = x1;
        param.top_left_y = y1;
        param.width      = x2 - x1 + 1;
        param.height     = y2 - y1 + 1;
        return COSMO_NN_OK;
    }

    // Unified signed ratio: negative or 0 means crop, positive means expand
    float top_ratio    = rect_crop->h_top_crop.at(0);
    float bottom_ratio = rect_crop->h_bottom_crop.at(0);
    float left_ratio   = rect_crop->w_left_crop.at(0);
    float right_ratio  = rect_crop->w_right_crop.at(0);

    x1 -= w * left_ratio;
    y1 -= h * top_ratio;
    x2 += w * right_ratio;
    y2 += h * bottom_ratio;

    x1 = std::max(0, x1);
    y1 = std::max(0, y1);
    x2 = std::min(x2, size.width - 1);
    y2 = std::min(y2, size.height - 1);

    param.top_left_x = x1;
    param.top_left_y = y1;
    param.width      = x2 - x1 + 1;
    param.height     = y2 - y1 + 1;
    return COSMO_NN_OK;
}

Status NetUtils::GenerateCropResizeParam(CropResize* crop_resize, int left, int top, int right, int bottom,
                                         Size size, CropResizeParam& param) {
    if (!crop_resize)
        return Status(COSMO_NN_ERR_NULL_PARAM, "Op crop_resize must not be nullptr");

    int x1 = left;
    int y1 = top;
    int x2 = right;
    int y2 = bottom;
    int w  = x2 - x1 + 1;
    int h  = y2 - y1 + 1;

    if (crop_resize->square) {
        int base_len = 0;
        if (crop_resize->square_mode == 0) {
            base_len = std::max(w, h);
        } else if (crop_resize->square_mode == 1) {
            base_len = std::min(w, h);
        } else if (crop_resize->square_mode == 2) {
            base_len = (w + h) / 2;
        } else {
            return Status(COSMO_NN_ERR_PARAM, "Invalid square mode");
        }

        int center_x = left + w / 2;
        int center_y = top + h / 2;

        x1 = center_x - base_len * (crop_resize->w_left_crop.at(0) + 0.5f);
        x2 = center_x + base_len * (crop_resize->w_right_crop.at(0) + 0.5f);
        y1 = center_y - base_len * (crop_resize->h_top_crop.at(0) + 0.5f);
        y2 = center_y + base_len * (crop_resize->h_bottom_crop.at(0) + 0.5f);

        x1 = std::max(0, x1);
        y1 = std::max(0, y1);
        x2 = std::min(x2, size.width - 1);
        y2 = std::min(y2, size.height - 1);

        param.top_left_x = x1;
        param.top_left_y = y1;
        param.width      = x2 - x1 + 1;
        param.height     = y2 - y1 + 1;
        param.gravity    = crop_resize->gravity;
        param.dst_height = crop_resize->dsize[0];
        param.dst_width  = crop_resize->dsize[1];
        param.val        = crop_resize->color.at(0);
        return COSMO_NN_OK;
    }

    // Unified signed ratio: negative or 0 means crop, positive means expand
    float top_ratio    = crop_resize->h_top_crop.at(0);
    float bottom_ratio = crop_resize->h_bottom_crop.at(0);
    float left_ratio   = crop_resize->w_left_crop.at(0);
    float right_ratio  = crop_resize->w_right_crop.at(0);

    x1 -= w * left_ratio;
    y1 -= h * top_ratio;
    x2 += w * right_ratio;
    y2 += h * bottom_ratio;

    x1 = std::max(0, x1);
    y1 = std::max(0, y1);
    x2 = std::min(x2, size.width - 1);
    y2 = std::min(y2, size.height - 1);

    param.top_left_x = x1;
    param.top_left_y = y1;
    param.width      = x2 - x1 + 1;
    param.height     = y2 - y1 + 1;
    param.gravity    = crop_resize->gravity;
    param.dst_height = crop_resize->dsize[0];
    param.dst_width  = crop_resize->dsize[1];
    param.val        = crop_resize->color.at(0);
    return COSMO_NN_OK;
}

Status NetUtils::OptimizePreOps(std::vector<std::unique_ptr<Op>>& ops, bool use_skip) {
    auto begin = ops.begin();
    auto end   = ops.end();
    ops.erase(std::remove_if(begin, end, [](const std::unique_ptr<Op>& op) { return !op; }), end);

    if (use_skip) {
        begin = ops.begin();
        end   = ops.end();
        ops.erase(std::remove_if(begin, end, [](const std::unique_ptr<Op>& op) { return op->skip; }), end);
    }

    // fuse crop and resize
    if (ops.at(0)->name == "rect_crop" && ops.at(1)->name == "resize") {
        auto* crop   = dynamic_cast<RectCrop*>(ops.at(0).get());
        auto* resize = dynamic_cast<Resize*>(ops.at(1).get());

        auto crop_resize                  = std::make_unique<CropResize>("crop_resize");
        crop_resize->type                 = crop->type;
        crop_resize->bbox_hw_ratio_levels = crop->bbox_hw_ratio_levels;
        crop_resize->h_top_crop           = crop->h_top_crop;
        crop_resize->h_bottom_crop        = crop->h_bottom_crop;
        crop_resize->w_left_crop          = crop->w_left_crop;
        crop_resize->w_right_crop         = crop->w_right_crop;
        crop_resize->square               = crop->square;
        crop_resize->square_mode          = crop->square_mode;
        crop_resize->dsize                = resize->dsize;
        crop_resize->gravity              = resize->gravity;
        crop_resize->color                = resize->color;

        // erase auto-deletes the old crop and resize via unique_ptr
        ops.erase(ops.begin(), ops.begin() + 2);
        ops.insert(ops.begin(), std::move(crop_resize));
    }

    return COSMO_NN_OK;
}

Status NetUtils::ParseDetectionOutput(std::vector<std::shared_ptr<Blob>>& blobs,
                                      std::vector<Size>& image_sizes, Size net_input_size,
                                      std::vector<int>& selected_indcies,
                                      std::vector<float>& selected_thresholds,
                                      std::vector<std::string>& selected_label,
                                      std::vector<std::vector<ObjectInfoV1>>& detects) {
    auto blob_count = blobs.size();
    if (blob_count != 1)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "Only support single output");

    auto output_blob = blobs.at(0);

    return PickDetectionObjects(output_blob, image_sizes, net_input_size, selected_indcies,
                                selected_thresholds, selected_label, detects);
}

Status NetUtils::ParseClassificationOutput(std::vector<std::shared_ptr<Blob>>& blobs,
                                           std::vector<int>& selected_indcies,
                                           std::vector<std::string>& selected_label,
                                           std::vector<std::vector<ObjectInfoV1>>& outputs) {
    auto blob_count = blobs.size();
    if (blob_count != 1)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "Only support single output");

    auto blob        = blobs.at(0);
    auto blob_desc   = blob->GetBlobDesc();
    auto blob_handle = blob->GetHandle();
    auto blob_dims   = blob_desc.dims;

    auto blob_ptr = reinterpret_cast<float*>(blob_handle.base);

    const int current_batch = blob_dims.at(0);
    const int n_dim         = blob_dims.size();

    if (n_dim == 2) {
        const int c = blob_dims.at(1);
        for (int i = 0; i < current_batch; i++) {
            ObjectInfoV1 obj_info;
            for (int k = 0; k < selected_indcies.size(); k++) {
                auto selected_idx = selected_indcies.at(k);
                if (selected_idx >= c)
                    continue;

                ClassifyInfo info;
                info.class_id   = selected_idx;
                info.confidence = blob_ptr[selected_idx];
                info.class_name = selected_label.at(k);

                obj_info.infos.push_back(info);
            }

            blob_ptr += c;

            std::vector<ObjectInfoV1> image_obj_infos = {obj_info};
            outputs.push_back(image_obj_infos);
        }
    } else if (n_dim == 3) {  // split_arg_max
        const int c = blob_dims.at(2);
        const int h = blob_dims.at(1);

        auto selected_indcies_begin = selected_indcies.begin();
        auto selected_indcies_end   = selected_indcies.end();
        for (int i = 0; i < current_batch; i++) {
            ObjectInfoV1 obj_info;
            for (int k = 0; k < c; k++) {
                int index = blob_ptr[k];
                auto iter = std::find(selected_indcies_begin, selected_indcies_end, index);
                if (iter == selected_indcies_end)
                    continue;

                float score = blob_ptr[k + c];

                ClassifyInfo info;
                info.class_id   = index;
                info.confidence = score;
                info.class_name = selected_label.at(std::distance(selected_indcies_begin, iter));

                obj_info.infos.push_back(info);
            }

            blob_ptr += h * c;

            std::vector<ObjectInfoV1> image_obj_infos = {obj_info};
            outputs.push_back(image_obj_infos);
        }
    } else {
        return Status(COSMO_NN_ERR_INVALID_INPUT, "Unsupported blob shape");
    }

    return COSMO_NN_OK;
}

Status NetUtils::ParseKeypointOutput(std::vector<std::shared_ptr<Blob>>& blobs, std::vector<Rect>& rects,
                                     std::vector<std::vector<ObjectInfoV1>>& outputs) {
    auto blob_count = blobs.size();
    if (blob_count != 1)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "Only support single output");

    auto output_blob = blobs.at(0);
    auto output_desc = output_blob->GetBlobDesc();

    const int current_batch  = output_desc.dims.at(0);
    const int keypoint_count = output_desc.dims.at(1) / 2;

    auto data = reinterpret_cast<float*>(output_blob->GetHandle().base);
    for (int i = 0; i < current_batch; i++) {
        auto b_data = data + i * keypoint_count * 2;

        ObjectInfoV1 obj_info;
        for (int j = 0; j < keypoint_count; j++) {
            float px = rects.at(i).x + b_data[j * 2] * rects.at(i).width;
            float py = rects.at(i).y + b_data[j * 2 + 1] * rects.at(i).height;
            obj_info.key_points.emplace_back(px, py);
        }
        std::vector<ObjectInfoV1> _obj_infos = {obj_info};
        outputs.push_back(_obj_infos);
    }
    return COSMO_NN_OK;
}

std::string NetUtils::TokenizerDecode(void* handle, std::vector<int>& ids) {
    bool skip_special_token = false;
    tokenizers_decode(handle, reinterpret_cast<const uint32_t*>(ids.data()), ids.size(),
                      static_cast<int>(skip_special_token));
    const char* data;
    size_t len;
    tokenizers_get_decode_str(handle, &data, &len);
    return std::string(data, len);
}

Status NetUtils::ParseDINOOutput(std::vector<std::shared_ptr<Blob>>& blobs, void* tokenizer_handle,
                                 std::vector<Size>& input_sizes, std::vector<int>& padding_input_ids,
                                 float text_threshold, float box_threshold,
                                 std::vector<std::vector<ObjectInfoV1>>& detects) {
    auto logits = blobs.at(0);
    auto box    = blobs.at(1);

    auto logits_dims = logits->GetBlobDesc().dims;

    const int h = logits_dims.at(1);
    const int w = logits_dims.at(2);

    std::vector<int> indexes;
    auto logits_data = reinterpret_cast<float*>(logits->GetHandle().base);

    // Calculate max sigmoid value per box for filtering
    // Note: logits_data is already sigmoid-transformed by dino_decode_node
    std::vector<float> max_per_box;
    std::vector<float> scores;
    max_per_box.resize(h);
    for (int i = 0; i < h; i++) {
        float max_data = 0.0f;
        for (int j = 0; j < w; j++) {
            float x = logits_data[i * w + j];  // Already sigmoid-transformed
            if (max_data < x) {
                max_data = x;
            }
        }
        max_per_box[i] = max_data;
        if (max_data > box_threshold) {
            indexes.push_back(i);
            scores.push_back(max_data);
        }
    }

    auto boxes_data = reinterpret_cast<float*>(box->GetHandle().base);

    std::vector<int> ids;
    std::vector<ObjectInfoV1> objs;
    // Adjust the right_idx to exclude [SEP] token at the end
    const int left_idx  = 0;
    const int right_idx = w - 1;

    for (int i = 0; i < indexes.size(); i++) {
        auto index      = indexes.at(i);
        auto logit_data = logits_data + index * w;
        auto box_data   = boxes_data + index * 4;

        // Find first matching token (skip [CLS] at idx 0 and [SEP] at idx w-1)
        // This matches the demo's behavior: only use the first token that exceeds text_threshold
        ids.clear();
        bool found_token = false;
        // Start from left_idx + 1 to skip [CLS], end at right_idx to skip [SEP]
        for (int j = left_idx + 1; j < right_idx; j++) {
            float x = logit_data[j];  // Already sigmoid-transformed
            if (x > text_threshold) {
                ids.push_back(padding_input_ids.at(j));
                found_token = true;
                break;  // Only use the first matching token, like the demo
            }
        }

        // Skip this box if no valid token found
        if (!found_token || ids.empty()) {
            continue;
        }

        float cx     = Clamp(*(box_data), 0.f, 1.f);
        float cy     = Clamp(*(box_data + 1), 0.f, 1.f);
        float width  = Clamp(*(box_data + 2), 0.f, 1.f);
        float height = Clamp(*(box_data + 3), 0.f, 1.f);

        auto size = input_sizes.at(0);
        ObjectInfoV1 obj;
        // Convert normalized box to pixel coordinates
        // boxes format: [cx, cy, w, h] (normalized)
        // Use precise conversion to match demo behavior
        obj.x1 = size.width * (cx - width / 2.0f);
        obj.x2 = size.width * (cx + width / 2.0f);
        obj.y1 = size.height * (cy - height / 2.0f);
        obj.y2 = size.height * (cy + height / 2.0f);

        // Apply clamping to ensure coordinates stay within image bounds
        obj.x1 = Clamp(obj.x1, 0.f, size.width - 1.f);
        obj.x2 = Clamp(obj.x2, 0.f, size.width - 1.f);
        obj.y1 = Clamp(obj.y1, 0.f, size.height - 1.f);
        obj.y2 = Clamp(obj.y2, 0.f, size.height - 1.f);

        ClassifyInfo info;
        // Use the score calculated during filtering (max sigmoid value per box)
        info.confidence = scores[i];

        // Try to use SophonTokenizer's idx2token mapping first (for Sophon device)
        // Otherwise fall back to TokenizerDecode (for CUDA device)
        if (tokenizer_handle) {
            SophonTokenizer* sophon_tokenizer = reinterpret_cast<SophonTokenizer*>(tokenizer_handle);
            if (sophon_tokenizer && !ids.empty() &&
                sophon_tokenizer->idx2token.find(ids[0]) != sophon_tokenizer->idx2token.end()) {
                // Use idx2token mapping directly, matching demo behavior
                info.class_name = sophon_tokenizer->idx2token[ids[0]];
            } else {
                // Fall back to TokenizerDecode for other tokenizer types
                info.class_name = TokenizerDecode(tokenizer_handle, ids);
            }
        } else {
            info.class_name = "unknown";
        }

        obj.infos.push_back(info);

        objs.push_back(obj);
    }

    detects.push_back(objs);
    return COSMO_NN_OK;
}

Status NetUtils::GetImageSize(DimsVector dims, DataFormat layout, Size& size) {
    if (dims.size() != 4)
        return Status(COSMO_NN_ERR_PARAM, "dims size must be 4");

    if (layout == DATA_FORMAT_NCHW) {
        size.width  = dims.at(3);
        size.height = dims.at(2);
    } else if (layout == DATA_FORMAT_NHWC) {
        size.width  = dims.at(2);
        size.height = dims.at(1);
    } else {
        return Status(COSMO_NN_ERR_PARAM, "GetImageSize: unsupported layout");
    }
    return COSMO_NN_OK;
}

void NetUtils::CalculateWarpAffineMatrix(std::vector<std::pair<float, float>>& src_points,
                                         std::vector<std::pair<float, float>>& dst_points,
                                         std::vector<float>& matrix) {
    Eigen::Matrix<float, 8, 6> A;
    Eigen::Matrix<float, 8, 1> B;
    // cppcheck-suppress constStatement
    A << src_points.at(0).first, src_points.at(0).second, 1, 0, 0, 0, 0, 0, 0, src_points.at(0).first,
        src_points.at(0).second, 1, src_points.at(1).first, src_points.at(1).second, 1, 0, 0, 0, 0, 0, 0,
        src_points.at(1).first, src_points.at(1).second, 1, src_points.at(2).first, src_points.at(2).second,
        1, 0, 0, 0, 0, 0, 0, src_points.at(2).first, src_points.at(2).second, 1, src_points.at(3).first,
        src_points.at(3).second, 1, 0, 0, 0, 0, 0, 0, src_points.at(3).first, src_points.at(3).second, 1;

    B << dst_points.at(0).first, dst_points.at(0).second, dst_points.at(1).first, dst_points.at(1).second,
        dst_points.at(2).first, dst_points.at(2).second, dst_points.at(3).first, dst_points.at(3).second;

    Eigen::Matrix<float, 6, 1> X = A.colPivHouseholderQr().solve(B);
    matrix.clear();
    for (int i = 0; i < 6; i++) {
        matrix.push_back(X(i));
    }
}

void NetUtils::WarpAffineMatrixInverse(std::vector<float>& matrix, std::vector<float>& inverse) {
    auto m00 = matrix.at(0);
    auto m01 = matrix.at(1);
    auto m02 = matrix.at(2);
    auto m10 = matrix.at(3);
    auto m11 = matrix.at(4);
    auto m12 = matrix.at(5);

    // Inverse transform matrix
    float D   = m00 * m11 - m01 * m10;
    D         = D != 0 ? 1. / D : 0;
    float A11 = m11 * D;
    float A22 = m00 * D;
    float A12 = -m01 * D;
    float A21 = -m10 * D;

    float B1 = -A11 * m02 - A12 * m12;
    float B2 = -A21 * m02 - A22 * m12;

    inverse.clear();
    inverse = {A11, A12, B1, A21, A22, B2};
}

void NetUtils::MaskScale(uint8_t* src, size_t src_width, size_t src_height, uint8_t* dst, size_t dst_width,
                         size_t dst_height, int channel) {
    float scale_x = src_width * 1.0 / dst_width;
    float scale_y = src_height * 1.0 / dst_height;

    size_t src_s = src_width * src_height;
    size_t dst_s = dst_width * dst_height;
    for (size_t c = 0; c < channel; c++) {
        auto src_c = src + c * src_s;
        auto dst_c = dst + c * dst_s;

        for (size_t i = 0; i < dst_height; i++) {
            float y  = i * scale_y;
            int y0   = static_cast<int>(y);
            int y1   = std::min(y0 + 1, (int)src_height - 1);
            float dy = y - y0;
            int yi   = y0;
            if (dy >= 0.5) {
                yi = y1;
            }

            for (size_t j = 0; j < dst_width; j++) {
                float x  = j * scale_x;
                int x0   = static_cast<int>(x);
                int x1   = std::min(x0 + 1, (int)src_width - 1);
                float dx = x - x0;
                int xi   = x0;
                if (dx >= 0.5) {
                    xi = x1;
                }

                dst_c[i * dst_width + j] = src_c[yi * src_width + xi];
            }
        }
    }
}

Status NetUtils::WarpAffine(std::shared_ptr<Blob> blob, std::vector<std::pair<float, float>>& src_points,
                            std::shared_ptr<Blob>& out_blob, float x_scale, float y_scale) {
    if (src_points.size() != 4)
        return Status(COSMO_NN_ERR_INVALID_INPUT, "src_points and dst_points size must be 4");

    auto in_desc  = blob->GetBlobDesc();
    auto out_desc = out_blob->GetBlobDesc();
    if (!(ImageFormatIsRGB(in_desc.image_format) || ImageFormatIsBGR(in_desc.image_format) ||
          ImageFormatIsYUV(in_desc.image_format)))
        return Status(COSMO_NN_ERR_INVALID_INPUT, "in image format not support");

    if (!(ImageFormatIsRGB(out_desc.image_format) || ImageFormatIsBGR(out_desc.image_format) ||
          ImageFormatIsYUV(in_desc.image_format)))
        return Status(COSMO_NN_ERR_INVALID_INPUT, "out image format not support");

    Size in_size, out_size;
    RETURN_ON_FAIL(GetImageSize(in_desc.dims, in_desc.data_format, in_size));
    RETURN_ON_FAIL(GetImageSize(out_desc.dims, out_desc.data_format, out_size));

    std::vector<std::pair<float, float>> dst_points = {
        {x_scale * out_size.width, y_scale * out_size.height},
        {x_scale * out_size.width, out_size.height * (1 - y_scale)},
        {out_size.width * (1 - x_scale), out_size.height * (1 - y_scale)},
        {out_size.width * (1 - x_scale), y_scale * out_size.height}};

    std::vector<float> matrix, invert;
    CalculateWarpAffineMatrix(src_points, dst_points, matrix);
    WarpAffineMatrixInverse(matrix, invert);

    auto in_handle = blob->GetHandle();
    auto in_data   = reinterpret_cast<uint8_t*>(in_handle.base);
    auto in_w = in_size.width, in_h = in_size.height;
    auto in_c = ImageFormatChannels(in_desc.image_format);

    auto out_handle = out_blob->GetHandle();
    auto out_data   = reinterpret_cast<uint8_t*>(out_handle.base);
    auto out_w = out_size.width, out_h = out_size.height;
    auto out_c = ImageFormatChannels(out_desc.image_format);

#ifdef COSMO_NN_USE_SOPHON_BACKEND
    (void)in_c;
    (void)out_c;
    (void)in_data;
    (void)out_data;
    bm_handle_t pbmhandle;
    int ret = bm_dev_request(&pbmhandle, 0);
    if (in_desc.device_type != DeviceType::DEVICE_SOPHON_TPU) {
        bm_dev_free(pbmhandle);
        return Status(COSMO_NN_ERR_INVALID_INPUT, "in device type not support");
    }
    if (out_desc.device_type != DeviceType::DEVICE_SOPHON_TPU) {
        bm_dev_free(pbmhandle);
        return Status(COSMO_NN_ERR_INVALID_INPUT, "out device type not support");
    }

    bm_device_mem_t* src_dev_mem = reinterpret_cast<bm_device_mem_t*>(in_handle.base);
    bm_device_mem_t* dst_dev_mem = reinterpret_cast<bm_device_mem_t*>(out_handle.base);

    bm_image src_image;
    ret = bm_image_create(pbmhandle, in_h, in_w, FORMAT_BGR_PACKED, DATA_TYPE_EXT_1N_BYTE, &src_image);
    if (ret != BM_SUCCESS) {
        bm_dev_free(pbmhandle);
        return Status(COSMO_NN_ERR_SOPHON_BMIMAGE_CREAT_FAILED, "bm_image creat failed.");
    }
    ret = bm_image_attach(src_image, src_dev_mem);
    if (ret != BM_SUCCESS) {
#ifdef COSMO_NN_SOPHON_1684X
        bm_image_destroy(src_image);
#else
        bm_image_destroy(&src_image);
#endif
        bm_dev_free(pbmhandle);
        return Status(COSMO_NN_ERR_SOPHON_ATTACH_FAILED, "bm_image attach failed.");
    }

    bm_image input_image;
    bm_image_create(pbmhandle, in_h, in_w, FORMAT_BGR_PLANAR, DATA_TYPE_EXT_1N_BYTE, &input_image);
    bm_image_alloc_dev_mem(input_image, 1);
    bmcv_rect_t crop_rect = {0, 0, (unsigned int)in_w, (unsigned int)in_h};
    bmcv_image_vpp_convert(pbmhandle, 1, src_image, &input_image, &crop_rect);

    bm_image affine_image;
    bm_image_create(pbmhandle, out_h, out_w, FORMAT_BGR_PLANAR, DATA_TYPE_EXT_1N_BYTE, &affine_image);
    bm_image_alloc_dev_mem(affine_image, 1);

    bmcv_affine_image_matrix matrix_image[4];
    bmcv_affine_matrix s_matrix;
    memcpy(s_matrix.m, invert.data(), 6 * sizeof(float));
    matrix_image[0].matrix_num = 1;
    matrix_image[0].matrix     = &s_matrix;

    ret = bmcv_image_warp_affine(pbmhandle, 1, matrix_image, &input_image, &affine_image);
    if (ret != BM_SUCCESS) {
        bm_image_detach(src_image);
#ifdef COSMO_NN_SOPHON_1684X
        bm_image_destroy(input_image);
        bm_image_destroy(src_image);
        bm_image_destroy(affine_image);
#else
        bm_image_destroy(&input_image);
        bm_image_destroy(&src_image);
        bm_image_destroy(&affine_image);
#endif
        bm_dev_free(pbmhandle);
        return Status(COSMO_NN_ERR_SOPHON_INFER_ERR, "bm_image warp affine failed.");
    }

    bm_image bgr_packed_image;
    bm_image_create(pbmhandle, out_h, out_w, FORMAT_BGR_PACKED, DATA_TYPE_EXT_1N_BYTE, &bgr_packed_image);
    unsigned long long mem_addr = bm_mem_get_device_addr(*dst_dev_mem);
    int size                    = out_w * out_h;
    bm_device_mem_t addr[3];
    addr[0] = bm_mem_from_device(mem_addr, size);
    addr[1] = bm_mem_from_device((unsigned long long)((uint8_t*)mem_addr + size), size);
    addr[2] = bm_mem_from_device((unsigned long long)((uint8_t*)mem_addr + 2 * size), size);
    bm_image_attach(bgr_packed_image, addr);
    bmcv_image_storage_convert(pbmhandle, 1, &affine_image, &bgr_packed_image);

    bm_image_detach(src_image);
    bm_image_detach(bgr_packed_image);
#ifdef COSMO_NN_SOPHON_1684X
    bm_image_destroy(input_image);
    bm_image_destroy(src_image);
    bm_image_destroy(affine_image);
    bm_image_destroy(bgr_packed_image);
#else
    bm_image_destroy(&input_image);
    bm_image_destroy(&src_image);
    bm_image_destroy(&affine_image);
    bm_image_destroy(&bgr_packed_image);
#endif
    bm_dev_free(pbmhandle);
#endif

    return COSMO_NN_OK;
}

}  // namespace cosmo::nn