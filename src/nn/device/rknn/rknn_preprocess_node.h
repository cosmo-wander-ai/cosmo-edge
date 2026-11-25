#pragma once

#if defined(COSMO_NN_USE_RKNN_BACKEND) && defined(COSMO_MEDIA_USE_ROCKCHIP_BACKEND)

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "nn/device/cpu/cpu_crop_resize_node.h"
#include "nn/node/node.h"
#include "nn/utils/rect.h"

namespace cosmo::nn {

bool RknnFastPreprocessEnabled();
bool RknnForceRgaFailure();
bool RknnMppDmaBufEnabled();
bool RknnForceMppDmaBufFailure();
bool IsRknnNativeNormalizeContract(const std::vector<float>& mean, const std::vector<float>& std_dev,
                                   float scale, const DimsVector& input_dims);
void MapPackedU8ToNativeInt8(const uint8_t* source, int8_t* destination, size_t pixels, bool swap_red_blue);

class RknnResizeNode final : public Node {
public:
    RknnResizeNode();
    ~RknnResizeNode() override;

    void LoadParam(Op* op) override;
    DeviceType GetTopBlobDeviceType() override;
    Status InferTopShapes() override;
    size_t GetBottomCount() override;
    size_t GetTopCount() override;
    Status Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                   std::vector<std::shared_ptr<Blob>>& top_blobs) override;

private:
    Status ResizeSingle(const std::shared_ptr<Blob>& bottom, const std::shared_ptr<Blob>& top,
                        bool allow_bound_target);
    bool ResizeWithRga(Blob& bottom, Blob& top, const Rect& region, bool allow_bound_target);
    bool AcquireRgaBoundTarget(uint32_t& handle);
    void ReleaseRgaBoundTarget();
    void ResizeWithCpu(Blob& bottom, Blob& top, const Rect& region, bool output_rgb) const;

    int out_height_{0};
    int out_width_{0};
    int gravity_{0};
    uint8_t padding_value_{114};
    // Failed acceleration stays disabled for this node's lifetime. The packed
    // RGB path remains available when only native YUV CSC/import is unsupported.
    bool native_rga_unavailable_{false};
    // A failed full-range BT.709 CSC does not disable NV12-only resizing, or
    // native CSC for a later frame with different color metadata.
    bool native_full_range_csc_unavailable_{false};
    bool native_small_frame_unavailable_{false};
    std::array<int, 4> native_full_range_geometry_{};
    bool rga_unavailable_{false};
    uint32_t rga_bound_target_handle_{0};
    uint64_t rga_bound_target_generation_{0};
    bool rga_bound_target_unavailable_{false};
    bool rga_bound_guard_logged_{false};
};

class RknnCropResizeNode final : public CpuCropResizeNode {
public:
    RknnCropResizeNode();
    ~RknnCropResizeNode() override;

    void LoadParam(Op* op) override;
    Status Forward(std::vector<std::shared_ptr<Blob>>& image_blobs,
                   std::vector<std::shared_ptr<Blob>>& rect_blobs,
                   std::vector<std::shared_ptr<Blob>>& top_blobs) override;

private:
    Status ResizeRegions(std::vector<std::shared_ptr<Blob>>& image_blobs,
                         std::vector<std::shared_ptr<Blob>>& rect_blobs, Blob& output);
    bool AcquireRgaBoundTarget(uint32_t& handle);
    void ReleaseRgaBoundTarget();
    void InvalidateRgaBoundFrame();

    bool fast_contract_{false};
    bool native_rga_unavailable_{false};
    std::array<int, 12> avoid_native_csc_crop_{};
    std::array<int, 12> rejected_native_crop_{};
    bool rga_unavailable_{false};
    uint32_t rga_bound_target_handle_{0};
    uint64_t rga_bound_target_generation_{0};
    bool rga_bound_target_unavailable_{false};
    bool rga_bound_guard_logged_{false};
};

class RknnNormalizeNode final : public Node {
public:
    RknnNormalizeNode();
    ~RknnNormalizeNode() override = default;

    void LoadParam(Op* op) override;
    DeviceType GetTopBlobDeviceType() override;
    bool NeedBottomShapesInfered() override;
    Status InferTopShapesWithBottoms(std::vector<DimsVector> dims, std::vector<DataType> types) override;
    size_t GetBottomCount() override;
    size_t GetTopCount() override;
    Status Forward(std::vector<std::shared_ptr<Blob>>& bottom_blobs,
                   std::vector<std::shared_ptr<Blob>>& top_blobs) override;

private:
    bool NeedSwapRedBlue(ImageFormat format) const;
    bool CanBypassBoundInput(const Blob& bottom) const;
    Status ForwardNative(const Blob& bottom, Blob& top);
    Status ForwardFloat(const Blob& bottom, Blob& top);

    std::vector<float> mean_{};
    std::vector<float> std_dev_{};
    std::vector<float> scale_{};
    float uniform_scale_{1.0f};
    bool is_bgr_{true};
    bool native_contract_{false};
};

}  // namespace cosmo::nn

#endif
