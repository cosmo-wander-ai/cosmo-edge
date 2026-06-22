// AiComponment — Shared AI component base types.

#pragma once

#include "media/VideoFrame.h"
#include "nn/core/blob.h"
#include "nn/utils/profiler.h"
#include "util/ErrorCode.h"

namespace cosmo {
class AppProfiler : public cosmo::nn::IProfiler {
public:
    void ReportGraphInfo(const char* msg) override;

    void ReportNodeTime(const char* node_name, double time) override;
};

cosmo::nn::DeviceType GetDeviceType();

util::ErrorEnum ConvertImagesToBlobs(const std::vector<VideoFramePtr>& images,
                                     std::vector<std::shared_ptr<cosmo::nn::Blob>>& blobs);

util::ErrorEnum ConvertDatasToBlobs(const std::vector<std::vector<int>>& datas,
                                    std::vector<std::shared_ptr<cosmo::nn::Blob>>& blobs);

util::ErrorEnum ConvertDatasToBlobsFloat(const std::vector<std::vector<int>>& datas,
                                         std::vector<std::shared_ptr<cosmo::nn::Blob>>& blobs);

util::ErrorEnum ConvertDatasToBlobs(const std::vector<std::vector<std::vector<int>>>& datas,
                                    std::vector<std::shared_ptr<cosmo::nn::Blob>>& blobs);

util::ErrorEnum ConvertDatasToBlobsFloat(const std::vector<std::vector<std::vector<int>>>& datas,
                                         std::vector<std::shared_ptr<cosmo::nn::Blob>>& blobs);

std::shared_ptr<cosmo::nn::Blob> ConvertImageToBlob(VideoFramePtr image);

void FreeBlobs(std::vector<std::shared_ptr<cosmo::nn::Blob>>& blobs);

}  // namespace cosmo
