#pragma once

#if defined(COSMO_NN_USE_RKNN_BACKEND) && defined(COSMO_MEDIA_USE_ROCKCHIP_BACKEND)

#include <cstddef>
#include <cstdint>
#include <vector>

#include "nn/device/cpu/cpu_crop_resize_node.h"
#include "nn/node/node.h"

namespace cosmo::nn {

bool RknnFastPreprocessEnabled();
bool RknnForceRgaFailure();
bool RknnMppDmaBufEnabled();
bool RknnForceMppDmaBufFailure();
bool IsRknnDetectorResizeContract(int out_height, int out_width, int gravity,
                                  const std::vector<int>& padding_color);
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
    bool ResizeWithRga(const Blob& bottom, Blob& top, bool allow_bound_target);
    bool AcquireRgaBoundTarget(uint32_t& handle);
    void ReleaseRgaBoundTarget();
    void ResizeWithCpu(const Blob& bottom, Blob& top, bool output_rgb) const;

    int out_height_{0};
    int out_width_{0};
    int gravity_{0};
    std::vector<int> padding_color_{114, 114, 114};
    bool detector_contract_{false};
    // Failed acceleration stays disabled for this node's lifetime. The packed
    // RGB path remains available when only native YUV CSC/import is unsupported.
    bool native_rga_unavailable_{false};
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
    bool ForwardWithRga(std::vector<std::shared_ptr<Blob>>& image_blobs,
                        std::vector<std::shared_ptr<Blob>>& rect_blobs,
                        std::vector<std::shared_ptr<Blob>>& top_blobs);
    bool AcquireRgaBoundTarget(uint32_t& handle);
    void ReleaseRgaBoundTarget();
    void InvalidateRgaBoundFrame();

    bool fast_contract_{false};
    bool native_rga_unavailable_{false};
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
    bool detector_sized_{false};
};

}  // namespace cosmo::nn

#endif
