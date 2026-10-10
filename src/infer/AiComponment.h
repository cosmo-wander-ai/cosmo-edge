// AiComponment — Shared AI component base types.

#pragma once

#include "media/NativeVideoBuffer.h"
#include "media/VideoFrame.h"
#include "nn/core/blob.h"
#include "util/ErrorCode.h"

namespace cosmo {
cosmo::nn::DeviceType GetDeviceType();

util::ErrorEnum ConvertImagesToBlobs(const std::vector<VideoFramePtr>& images,
                                     std::vector<std::shared_ptr<cosmo::nn::Blob>>& blobs);
util::ErrorEnum ConvertImagesToBlobs(const std::vector<VideoFramePtr>& images,
                                     const std::vector<media::NativeVideoBufferPtr>& native_buffers,
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
