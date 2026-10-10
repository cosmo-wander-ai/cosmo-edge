#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <string>

#include "nn/core/common.h"
#include "nn/core/macros.h"
#include "nn/utils/image_format_utils.h"

namespace cosmo::nn {

enum class NativeImageColorSpace {
    Unspecified = 0,
    Bt601,
    Bt709,
    Bt2020,
};

enum class NativeImageColorRange {
    Unspecified = 0,
    Limited,
    Full,
};

struct PUBLIC BlobDesc {
    DeviceType device_type = DEVICE_NAIVE;

    DataType data_type = DATA_TYPE_FLOAT;

    DataFormat data_format   = DATA_FORMAT_NCHW;
    ImageFormat image_format = IMAGE_UNKNOWN;

    DimsVector dims;

    std::string name = "";

    std::string description();
};

struct PUBLIC BlobHandle {
    void* base        = nullptr;
    unsigned long phy = 0;

    // Optional borrowed DMA-BUF image. The object supplying base owns the
    // descriptor lifetime; Blob does not close fd or alter that ownership.
    struct NativeImage {
        int fd{-1};
        size_t bytes{0};
        int width{0};
        int height{0};
        int width_stride{0};
        int height_stride{0};
        ImageFormat format{IMAGE_UNKNOWN};
        NativeImageColorSpace color_space{NativeImageColorSpace::Unspecified};
        NativeImageColorRange color_range{NativeImageColorRange::Unspecified};

        [[nodiscard]] bool Valid() const {
            return fd >= 0 && bytes > 0 && width > 0 && height > 0 && width_stride >= width &&
                   height_stride >= height && format != IMAGE_UNKNOWN;
        }
    } native_image;

    // Optional owned host fallback for an otherwise native image. The provider
    // must retain the image and synchronize/cache materialization across copies.
    std::function<void*()> host_data_provider;
};

class BlobImpl;

class PUBLIC Blob {
public:
    explicit Blob(BlobDesc desc);

    Blob(BlobDesc desc, bool alloc_memory);

    Blob(BlobDesc desc, BlobHandle handle);

    ~Blob();

    BlobDesc& GetBlobDesc();

    void SetBlobDesc(BlobDesc desc);

    // Native-aware nodes can inspect DMA-BUF metadata without generating host pixels.
    BlobHandle GetHandle(bool materialize_host = true);

    void SetHandle(BlobHandle handle);

    void ClearHandle();

private:
    std::unique_ptr<BlobImpl> impl;
};

using BlobMap = std::map<std::string, std::shared_ptr<Blob>>;

}  // namespace cosmo::nn
