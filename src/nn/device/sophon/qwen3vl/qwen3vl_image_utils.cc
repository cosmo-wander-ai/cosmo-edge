#include "nn/device/sophon/qwen3vl/qwen3vl_image_utils.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace cosmo::nn {
namespace qwen3vl {

    namespace {

        const int IMAGE_FACTOR = 32;
        const int MAX_RATIO    = 200;

        int round_by_factor(int number, int factor) {
            return static_cast<int>(std::round(static_cast<double>(number) / factor)) * factor;
        }

        int ceil_by_factor(double number, int factor) {
            return static_cast<int>(std::ceil(number / factor)) * factor;
        }

        int floor_by_factor(double number, int factor) {
            return static_cast<int>(std::floor(number / factor)) * factor;
        }

        std::pair<int, int> smart_resize(int height, int width, int min_pixels, int max_pixels,
                                         int factor = IMAGE_FACTOR) {
            double aspect_ratio = static_cast<double>(std::max(height, width)) / std::min(height, width);
            if (aspect_ratio > MAX_RATIO)
                throw std::invalid_argument("Aspect ratio too large");
            int h_bar = std::max(factor, round_by_factor(height, factor));
            int w_bar = std::max(factor, round_by_factor(width, factor));
            if (h_bar * w_bar > max_pixels) {
                double beta = std::sqrt(static_cast<double>(height * width) / max_pixels);
                h_bar       = std::max(factor, floor_by_factor(static_cast<double>(height) / beta, factor));
                w_bar       = std::max(factor, floor_by_factor(static_cast<double>(width) / beta, factor));
            } else if (h_bar * w_bar < min_pixels) {
                double beta = std::sqrt(static_cast<double>(min_pixels) / (height * width));
                h_bar       = std::max(factor, ceil_by_factor(static_cast<double>(height) * beta, factor));
                w_bar       = std::max(factor, ceil_by_factor(static_cast<double>(width) * beta, factor));
            }
            return {h_bar, w_bar};
        }

        void tile(const std::vector<float>& x, std::vector<float>& y, int n) {
            for (int i = 0; i < n; i++)
                std::copy(x.begin(), x.end(), y.begin() + i * static_cast<int>(x.size()));
        }

        std::vector<int> calc_grid_thw(int resized_height, int resized_width, const Config& config) {
            int grid_t = 1;
            int grid_h = resized_height / config.patch_size;
            int grid_w = resized_width / config.patch_size;
            return {grid_t, grid_h, grid_w};
        }

        void rearrange_patches(const std::vector<float>& image, std::vector<float>& out,
                               const Config& config) {
            int grid_t         = config.grid_thw[0];
            int grid_h         = config.grid_thw[1];
            int grid_w         = config.grid_thw[2];
            int channel        = 3;
            int grid_prod      = grid_t * grid_h * grid_w;
            int conv_dim       = channel * config.temporal_patch_size * config.patch_size * config.patch_size;
            int total_elements = grid_prod * conv_dim;
            size_t image_size  = image.size();
            std::vector<float> in;
            if (image_size * 2 == static_cast<size_t>(total_elements)) {
                in.assign(total_elements, 0);
                tile(image, in, config.temporal_patch_size);
            } else if (image_size != static_cast<size_t>(total_elements)) {
                throw std::runtime_error("Image size mismatch for rearrange_patches");
            } else {
                in = image;
            }
            int merge_h = grid_h / config.spatial_merge_size;
            int merge_w = grid_w / config.spatial_merge_size;
            out.assign(total_elements, 0);
            for (size_t i = 0; i < in.size(); i++) {
                int idx = static_cast<int>(i);
                int pw  = idx % config.patch_size;
                idx /= config.patch_size;
                int mw = idx % config.spatial_merge_size;
                idx /= config.spatial_merge_size;
                int gw = idx % merge_w;
                idx /= merge_w;
                int ph = idx % config.patch_size;
                idx /= config.patch_size;
                int mh = idx % config.spatial_merge_size;
                idx /= config.spatial_merge_size;
                int gh = idx % merge_h;
                idx /= merge_h;
                int c = idx % channel;
                idx /= channel;
                int s = idx % config.temporal_patch_size;
                idx /= config.temporal_patch_size;
                int t        = idx;
                int new_idx  = t;
                new_idx      = new_idx * merge_h + gh;
                new_idx      = new_idx * merge_w + gw;
                new_idx      = new_idx * config.spatial_merge_size + mh;
                new_idx      = new_idx * config.spatial_merge_size + mw;
                new_idx      = new_idx * channel + c;
                new_idx      = new_idx * config.temporal_patch_size + s;
                new_idx      = new_idx * config.patch_size + ph;
                new_idx      = new_idx * config.patch_size + pw;
                out[new_idx] = in[i];
            }
        }

        inline uint8_t clamp_u8(int v) {
            if (v < 0)
                return 0;
            if (v > 255)
                return 255;
            return static_cast<uint8_t>(v);
        }

        // Bilinear resize: input/output are both RGB packed (HWC, 3 channels, uint8)
        // src_stride_bytes: bytes per row (>= src_w*3)
        static void resize_rgb_u8_bilinear(const uint8_t* src, int src_w, int src_h, int src_stride_bytes,
                                           uint8_t* dst, int dst_w, int dst_h) {
            if (!src || !dst || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0)
                throw std::invalid_argument("resize_rgb_u8_bilinear: invalid args");

            const float scale_x = static_cast<float>(src_w) / static_cast<float>(dst_w);
            const float scale_y = static_cast<float>(src_h) / static_cast<float>(dst_h);

            for (int dy = 0; dy < dst_h; ++dy) {
                const float sy = (dy + 0.5f) * scale_y - 0.5f;
                int y0         = static_cast<int>(std::floor(sy));
                float fy       = sy - y0;
                if (y0 < 0) {
                    y0 = 0;
                    fy = 0.f;
                }
                int y1 = y0 + 1;
                if (y1 >= src_h) {
                    y1 = src_h - 1;
                }

                const uint8_t* row0 = src + y0 * src_stride_bytes;
                const uint8_t* row1 = src + y1 * src_stride_bytes;

                for (int dx = 0; dx < dst_w; ++dx) {
                    const float sx = (dx + 0.5f) * scale_x - 0.5f;
                    int x0         = static_cast<int>(std::floor(sx));
                    float fx       = sx - x0;
                    if (x0 < 0) {
                        x0 = 0;
                        fx = 0.f;
                    }
                    int x1 = x0 + 1;
                    if (x1 >= src_w) {
                        x1 = src_w - 1;
                    }

                    const uint8_t* p00 = row0 + x0 * 3;
                    const uint8_t* p01 = row0 + x1 * 3;
                    const uint8_t* p10 = row1 + x0 * 3;
                    const uint8_t* p11 = row1 + x1 * 3;

                    const float w00 = (1.f - fx) * (1.f - fy);
                    const float w01 = fx * (1.f - fy);
                    const float w10 = (1.f - fx) * fy;
                    const float w11 = fx * fy;

                    uint8_t* out = dst + (dy * dst_w + dx) * 3;
                    for (int c = 0; c < 3; ++c) {
                        const float v = w00 * p00[c] + w01 * p01[c] + w10 * p10[c] + w11 * p11[c];
                        out[c]        = clamp_u8(static_cast<int>(std::lround(v)));
                    }
                }
            }
        }

    }  // namespace

    bool process_image_from_mat(const unsigned char* bgr_data, int width, int height, int stride_bytes,
                                Config& config, std::vector<float>& pixel_values) {
        if (!bgr_data || width <= 0 || height <= 0)
            return false;

        if (stride_bytes <= 0)
            stride_bytes = width * 3;
        if (stride_bytes < width * 3)
            return false;

        // BGR -> RGB packed
        std::vector<uint8_t> rgb(static_cast<size_t>(height) * width * 3);
        for (int y = 0; y < height; ++y) {
            const uint8_t* row =
                reinterpret_cast<const uint8_t*>(bgr_data) + static_cast<size_t>(y) * stride_bytes;
            uint8_t* out = rgb.data() + static_cast<size_t>(y) * width * 3;
            for (int x = 0; x < width; ++x) {
                const uint8_t b = row[x * 3 + 0];
                const uint8_t g = row[x * 3 + 1];
                const uint8_t r = row[x * 3 + 2];
                out[x * 3 + 0]  = r;
                out[x * 3 + 1]  = g;
                out[x * 3 + 2]  = b;
            }
        }

        auto resized       = smart_resize(height, width, config.MIN_PIXELS, config.MAX_PIXELS);
        int resized_height = resized.first;
        int resized_width  = resized.second;

        // resize (RGB u8)
        std::vector<uint8_t> resized_rgb(static_cast<size_t>(resized_height) * resized_width * 3);
        resize_rgb_u8_bilinear(rgb.data(), width, height, width * 3, resized_rgb.data(), resized_width,
                               resized_height);

        // normalize to float, output CHW: ((v/255) - 0.5)/0.5 = v/127.5 - 1
        std::vector<float> image_new;
        image_new.resize(static_cast<size_t>(resized_height) * resized_width * 3);
        const float inv_127_5 = 1.0f / 127.5f;
        const size_t plane    = static_cast<size_t>(resized_height) * resized_width;
        for (int y = 0; y < resized_height; ++y) {
            for (int x = 0; x < resized_width; ++x) {
                const size_t idx           = static_cast<size_t>(y) * resized_width + x;
                const uint8_t* p           = resized_rgb.data() + idx * 3;
                image_new[idx + 0 * plane] = static_cast<float>(p[0]) * inv_127_5 - 1.0f;  // R
                image_new[idx + 1 * plane] = static_cast<float>(p[1]) * inv_127_5 - 1.0f;  // G
                image_new[idx + 2 * plane] = static_cast<float>(p[2]) * inv_127_5 - 1.0f;  // B
            }
        }

        config.grid_thw = calc_grid_thw(resized_height, resized_width, config);
        rearrange_patches(image_new, pixel_values, config);
        return true;
    }

}  // namespace qwen3vl
}  // namespace cosmo::nn
