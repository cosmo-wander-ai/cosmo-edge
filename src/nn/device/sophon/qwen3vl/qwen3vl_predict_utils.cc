#include "nn/device/sophon/qwen3vl/qwen3vl_predict_utils.h"

#include <algorithm>

namespace cosmo::nn {
namespace qwen3vl {

    namespace {

        const int IMAGE_PAD_TOKEN = 151655;

        std::vector<float> LinspaceInclusive(float start, float end, int num) {
            std::vector<float> out;
            out.reserve(num);
            if (num == 1) {
                out.push_back(start);
                return out;
            }
            float step = (end - start) / static_cast<float>(num - 1);
            for (int i = 0; i < num; i++)
                out.push_back(start + step * i);
            return out;
        }

        std::vector<size_t> Argwhere(const std::vector<int>& vec, int value) {
            std::vector<size_t> indices;
            for (size_t i = 0; i < vec.size(); ++i) {
                if (vec[i] == value)
                    indices.push_back(i);
            }
            return indices;
        }

        std::vector<int> Arange(int start, int end) {
            std::vector<int> result;
            for (int i = start; i < end; ++i)
                result.push_back(i);
            return result;
        }

        int MaxOfVec(const std::vector<int>& vec) {
            if (vec.empty())
                return 0;
            int max_val = vec[0];
            for (int val : vec) {
                if (val > max_val)
                    max_val = val;
            }
            return max_val;
        }

    }  // namespace

    std::string BuildImagePrompt(const std::string& input_str, const std::vector<std::vector<int>>& grid_thw,
                                 bool append_empty_think) {
        std::string prompt = "<|im_start|>user\n";
        for (size_t i = 0; i < grid_thw.size(); i++) {
            int h = grid_thw[i][1], w = grid_thw[i][2];
            int pad_len = h * w / 4;
            prompt += "<|vision_start|>";
            for (int j = 0; j < pad_len; j++)
                prompt += "<|image_pad|>";
            prompt += "<|vision_end|>";
        }
        prompt += input_str + "<|im_end|>\n<|im_start|>assistant\n";
        if (append_empty_think)
            prompt += "<think>\n\n</think>\n\n";
        return prompt;
    }

    std::vector<int> FindTokenOffset(const std::vector<int>& input_ids, int pad_id) {
        std::vector<int> offsets;
        for (size_t i = 0; i < input_ids.size(); i++) {
            if (input_ids[i] == pad_id)
                offsets.push_back(static_cast<int>(i));
        }
        return offsets;
    }

    void FastPosEmbedInterpolate(const std::vector<int>& grid_thw, int num_grid, int spatial_merge_size,
                                 std::vector<int>& idx_out, std::vector<float>& weight_out) {
        int t = 1, h = grid_thw[1], w = grid_thw[2];
        auto h_idxs = LinspaceInclusive(0.0f, static_cast<float>(num_grid - 1), h);
        auto w_idxs = LinspaceInclusive(0.0f, static_cast<float>(num_grid - 1), w);
        std::vector<int> h_floor(h), h_ceil(h), w_floor(w), w_ceil(w);
        std::vector<float> dh(h), dw(w);
        for (int i = 0; i < h; i++) {
            int f      = static_cast<int>(h_idxs[i]);
            h_floor[i] = f;
            h_ceil[i]  = std::min(f + 1, num_grid - 1);
            dh[i]      = h_idxs[i] - static_cast<float>(f);
        }
        for (int j = 0; j < w; j++) {
            int f      = static_cast<int>(w_idxs[j]);
            w_floor[j] = f;
            w_ceil[j]  = std::min(f + 1, num_grid - 1);
            dw[j]      = w_idxs[j] - static_cast<float>(f);
        }
        std::vector<int> base_h(h), base_h_ceil(h);
        for (int i = 0; i < h; i++) {
            base_h[i]      = h_floor[i] * num_grid;
            base_h_ceil[i] = h_ceil[i] * num_grid;
        }
        std::vector<int> idx00, idx01, idx10, idx11;
        std::vector<float> w00, w01, w10, w11;
        idx00.reserve(h * w);
        idx01.reserve(h * w);
        idx10.reserve(h * w);
        idx11.reserve(h * w);
        w00.reserve(h * w);
        w01.reserve(h * w);
        w10.reserve(h * w);
        w11.reserve(h * w);
        for (int i = 0; i < h; i++) {
            float dh_i = dh[i], one_dh_i = 1.0f - dh_i;
            int base_i = base_h[i], base_i_ceil = base_h_ceil[i];
            for (int j = 0; j < w; j++) {
                float dw_j = dw[j], one_dw_j = 1.0f - dw_j;
                idx00.push_back(base_i + w_floor[j]);
                idx01.push_back(base_i + w_ceil[j]);
                idx10.push_back(base_i_ceil + w_floor[j]);
                idx11.push_back(base_i_ceil + w_ceil[j]);
                w00.push_back(one_dh_i * one_dw_j);
                w01.push_back(one_dh_i * dw_j);
                w10.push_back(dh_i * one_dw_j);
                w11.push_back(dh_i * dw_j);
            }
        }
        int msize = spatial_merge_size, H_blk = h / msize, W_blk = w / msize;
        std::vector<int> out_order;
        out_order.reserve(h * w);
        for (int i_blk = 0; i_blk < H_blk; i_blk++)
            for (int j_blk = 0; j_blk < W_blk; j_blk++)
                for (int i2 = 0; i2 < msize; i2++)
                    for (int j2 = 0; j2 < msize; j2++)
                        out_order.push_back((i_blk * msize + i2) * w + (j_blk * msize + j2));
        idx_out.resize(t * h * w * 4);
        weight_out.resize(t * h * w * 4);
        for (size_t k = 0; k < out_order.size(); k++) {
            int src = out_order[k], base = static_cast<int>(k * 4);
            idx_out[base + 0]    = idx00[src];
            idx_out[base + 1]    = idx01[src];
            idx_out[base + 2]    = idx10[src];
            idx_out[base + 3]    = idx11[src];
            weight_out[base + 0] = w00[src];
            weight_out[base + 1] = w01[src];
            weight_out[base + 2] = w10[src];
            weight_out[base + 3] = w11[src];
        }
    }

    std::vector<std::vector<int>> RotPos(const std::vector<std::vector<int>>& grid_thw,
                                         int spatial_merge_size) {
        std::vector<std::vector<int>> pos_ids;
        for (const auto& thw : grid_thw) {
            int t = thw[0];
            int h = thw[1];
            int w = thw[2];
            std::vector<int> hpos_ids;
            for (int i = 0; i < h; ++i)
                for (int j = 0; j < w; ++j)
                    hpos_ids.push_back(i);
            int h_merged = h / spatial_merge_size;
            int w_merged = w / spatial_merge_size;
            std::vector<int> reshaped_hpos_ids;
            for (int i = 0; i < h_merged; ++i) {
                for (int j = 0; j < w_merged; ++j) {
                    for (int k = 0; k < spatial_merge_size; ++k) {
                        for (int l = 0; l < spatial_merge_size; ++l) {
                            int src_idx = ((i * spatial_merge_size + k) * w) + (j * spatial_merge_size + l);
                            reshaped_hpos_ids.push_back(hpos_ids[src_idx]);
                        }
                    }
                }
            }
            std::vector<int> wpos_ids;
            for (int i = 0; i < h; ++i)
                for (int j = 0; j < w; ++j)
                    wpos_ids.push_back(j);
            std::vector<int> reshaped_wpos_ids;
            for (int i = 0; i < h_merged; ++i) {
                for (int j = 0; j < w_merged; ++j) {
                    for (int k = 0; k < spatial_merge_size; ++k) {
                        for (int l = 0; l < spatial_merge_size; ++l) {
                            int src_idx = ((i * spatial_merge_size + k) * w) + (j * spatial_merge_size + l);
                            reshaped_wpos_ids.push_back(wpos_ids[src_idx]);
                        }
                    }
                }
            }
            std::vector<std::vector<int>> merged;
            for (size_t i = 0; i < reshaped_hpos_ids.size(); ++i)
                merged.push_back({reshaped_hpos_ids[i], reshaped_wpos_ids[i]});
            for (int i = 0; i < t; ++i)
                pos_ids.insert(pos_ids.end(), merged.begin(), merged.end());
        }
        return pos_ids;
    }

    std::vector<std::vector<int>> GetRopeIndex(const std::vector<int>& input_ids,
                                               const std::vector<std::vector<int>>& grid_thw, int pad_id,
                                               int id_vision_start, int spatial_merge_size,
                                               int tokens_per_second) {
        std::vector<size_t> vision_start_indices = Argwhere(input_ids, id_vision_start);
        int image_nums                           = vision_start_indices.size();
        std::vector<std::vector<std::vector<int>>> llm_pos_ids_list;
        size_t st             = 0;
        int remain_images     = image_nums;
        int second_per_grid_t = (pad_id == 151656) ? 1 : 0;  // VIDEO_PAD_TOKEN
        for (int img_idx = 0; img_idx < image_nums; ++img_idx) {
            size_t ed_image = input_ids.size();
            if (remain_images > 0) {
                auto it = std::find(input_ids.begin() + st, input_ids.end(), pad_id);
                if (it != input_ids.end())
                    ed_image = it - input_ids.begin();
            }
            int t, h, w;
            if (pad_id == IMAGE_PAD_TOKEN) {
                t = grid_thw[img_idx][0];
                h = grid_thw[img_idx][1];
                w = grid_thw[img_idx][2];
            } else {
                t = 1;
                h = grid_thw[0][1];
                w = grid_thw[0][2];
            }
            --remain_images;
            size_t ed       = ed_image;
            int llm_grid_t  = t;
            int llm_grid_h  = h / spatial_merge_size;
            int llm_grid_w  = w / spatial_merge_size;
            size_t text_len = ed - st;
            int st_idx      = 0;
            if (!llm_pos_ids_list.empty()) {
                int max_val = 0;
                for (const auto& row : llm_pos_ids_list.back()) {
                    int row_max = MaxOfVec(row);
                    if (row_max > max_val)
                        max_val = row_max;
                }
                st_idx = max_val + 1;
            }
            std::vector<std::vector<int>> text_pos(3);
            std::vector<int> text_range = Arange(0, text_len);
            for (int j = 0; j < 3; ++j) {
                std::vector<int> temp(text_range);
                for (int& val : temp)
                    val += st_idx;
                text_pos[j] = temp;
            }
            llm_pos_ids_list.push_back(text_pos);
            std::vector<int> t_index;
            for (int i = 0; i < llm_grid_t; i++) {
                int time_val = i * second_per_grid_t * tokens_per_second;
                t_index.insert(t_index.end(), llm_grid_h * llm_grid_w, time_val);
            }
            std::vector<int> h_index, w_index;
            for (int n = 0; n < llm_grid_t; ++n) {
                for (int p = 0; p < llm_grid_h; ++p)
                    for (int q = 0; q < llm_grid_w; ++q)
                        h_index.push_back(p);
            }
            for (int n = 0; n < llm_grid_t; ++n) {
                for (int p = 0; p < llm_grid_h; ++p)
                    for (int q = 0; q < llm_grid_w; ++q)
                        w_index.push_back(q);
            }
            std::vector<std::vector<int>> grid_pos = {t_index, h_index, w_index};
            for (auto& row : grid_pos) {
                for (int& val : row)
                    val += text_len + st_idx;
            }
            llm_pos_ids_list.push_back(grid_pos);
            st = ed + llm_grid_t * llm_grid_h * llm_grid_w;
        }
        if (st < input_ids.size()) {
            int st_idx = 0;
            if (!llm_pos_ids_list.empty()) {
                int max_val = 0;
                for (const auto& row : llm_pos_ids_list.back()) {
                    int row_max = MaxOfVec(row);
                    if (row_max > max_val)
                        max_val = row_max;
                }
                st_idx = max_val + 1;
            }
            size_t text_len = input_ids.size() - st;
            std::vector<std::vector<int>> text_pos(3);
            std::vector<int> text_range = Arange(0, text_len);
            for (int j = 0; j < 3; ++j) {
                std::vector<int> temp(text_range);
                for (int& val : temp)
                    val += st_idx;
                text_pos[j] = temp;
            }
            llm_pos_ids_list.push_back(text_pos);
        }
        std::vector<std::vector<int>> result(3);
        for (const auto& vec : llm_pos_ids_list) {
            for (int i = 0; i < 3; ++i)
                result[i].insert(result[i].end(), vec[i].begin(), vec[i].end());
        }
        return result;
    }

}  // namespace qwen3vl
}  // namespace cosmo::nn
