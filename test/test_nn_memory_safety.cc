#include <array>
#include <limits>
#include <memory>
#include <vector>

#include "catch_amalgamated.hpp"
#include "mem/Allocator.h"
#include "mem/MemoryPoolMng.h"
#include "nn/core/blob.h"
#include "nn/utils/blob_memory_size_info.h"
#include "nn/utils/blob_memory_size_utils.h"
#include "nn/utils/dims_vector_utils.h"

#ifdef COSMO_NN_USE_HOST_BACKEND
#include "nn/device/cpu/cpu_affine_crop_node.h"
#include "nn/device/cpu/cpu_crop_resize_node.h"
#include "nn/device/cpu/cpu_resize_node.h"
#include "nn/device/cpu/cpu_sequence_node.h"
#endif

#ifdef COSMO_NN_USE_SOPHON_BACKEND
#include "nn/device/sophon/sophon_memory_utils.h"
#endif

namespace cosmo {
namespace {

    class CountingAllocator final : public mem::Allocator {
    public:
        mem::Block* Allocate(size_t size) override {
            (void)size;
            ++allocation_count;
            return nullptr;
        }

        void Free(mem::Block* block) override {
            (void)block;
        }

        size_t allocation_count = 0;
    };

    TEST_CASE("NN dimension counting rejects invalid and overflowing shapes", "[nn-memory-safety]") {
        REQUIRE(nn::DimsVectorUtils::Count({1, 3, 640, 640}) == 1228800);
        REQUIRE(nn::DimsVectorUtils::Count({1, 0, 640, 640}) == 0);
        REQUIRE(nn::DimsVectorUtils::Count({1, -1, 640, 640}) == -1);
        REQUIRE(nn::DimsVectorUtils::Count({std::numeric_limits<int>::max(), 2}) == -1);
        REQUIRE(nn::DimsVectorUtils::Count({1, 2}, -1) == -1);
        REQUIRE(nn::DimsVectorUtils::Count({1, 2}, 2, 1) == -1);
    }

    TEST_CASE("Native blob inspection does not request its host fallback", "[nn-memory-safety]") {
        auto owner                                            = std::make_shared<std::array<uint8_t, 12>>();
        std::weak_ptr<std::array<uint8_t, 12>> retained_owner = owner;
        int host_requests                                     = 0;
        nn::BlobHandle handle;
        handle.native_image.fd    = 7;
        handle.host_data_provider = [owner, &host_requests]() -> void* {
            ++host_requests;
            return owner->data();
        };
        nn::Blob blob(nn::BlobDesc{}, std::move(handle));
        owner.reset();
        const auto native = blob.GetHandle(false);
        CHECK(native.base == nullptr);
        CHECK(native.native_image.fd == 7);
        CHECK(host_requests == 0);
        CHECK_FALSE(retained_owner.expired());
        const auto host = blob.GetHandle();
        CHECK(host.base != nullptr);
        CHECK(host_requests == 1);
    }

#ifdef COSMO_NN_USE_HOST_BACKEND
    TEST_CASE("CPU pixel consumers reject an unavailable deferred image",
              "[nn-memory-safety][deferred-host]") {
        nn::BlobDesc desc;
        desc.device_type  = nn::DEVICE_NAIVE;
        desc.data_type    = nn::DATA_TYPE_UINT8;
        desc.data_format  = nn::DATA_FORMAT_NHWC;
        desc.image_format = nn::IMAGE_BGR;
        desc.dims         = {1, 4, 4, 3};
        int host_requests = 0;
        nn::BlobHandle deferred;
        deferred.host_data_provider = [&]() -> void* {
            ++host_requests;
            return nullptr;
        };
        auto image = std::make_shared<nn::Blob>(desc, deferred);
        std::array<uint8_t, 48> output{};
        output.fill(91);
        nn::BlobHandle target;
        target.base = output.data();
        auto top    = std::make_shared<nn::Blob>(desc, target);
        std::vector<std::shared_ptr<nn::Blob>> images{image}, params{image}, tops{top};
        SECTION("resize") {
            nn::CpuResizeNode node;
            CHECK_FALSE(bool(node.Forward(images, tops)));
        }
        SECTION("crop resize") {
            nn::CpuCropResizeNode node;
            CHECK_FALSE(bool(node.Forward(images, params, tops)));
        }
        SECTION("affine crop") {
            nn::CpuAffineCropNode node;
            CHECK_FALSE(bool(node.Forward(images, params, tops)));
        }
        SECTION("sequence") {
            nn::CpuSequenceNode node;
            CHECK_FALSE(bool(node.Forward(images, params, tops)));
        }
        CHECK(host_requests == 1);
        for (const auto value : output)
            CHECK(value == 91);
    }
#endif

    TEST_CASE("Blob memory sizing fails closed before integer overflow", "[nn-memory-safety]") {
        nn::BlobMemorySizeInfo valid{nn::DATA_TYPE_FLOAT, {1024}};
        REQUIRE(nn::GetBlobMemoryBytesSize(valid) == 4096);

        nn::BlobMemorySizeInfo zero{nn::DATA_TYPE_FLOAT, {0}};
        REQUIRE(nn::GetBlobMemoryBytesSize(zero) == -1);

        nn::BlobMemorySizeInfo negative{nn::DATA_TYPE_FLOAT, {-1}};
        REQUIRE(nn::GetBlobMemoryBytesSize(negative) == -1);

        nn::BlobMemorySizeInfo oversized{nn::DATA_TYPE_FLOAT, {512 * 1024 * 1024}};
        REQUIRE(nn::GetBlobMemoryBytesSize(oversized) == -1);

        nn::BlobMemorySizeInfo product_overflow{
            nn::DATA_TYPE_FLOAT, {std::numeric_limits<int>::max(), std::numeric_limits<int>::max()}};
        REQUIRE(nn::GetBlobMemoryBytesSize(product_overflow) == -1);

        nn::BlobMemorySizeInfo unsupported_shape{nn::DATA_TYPE_FLOAT, {1, 2, 3}};
        REQUIRE(nn::GetBlobMemoryBytesSize(unsupported_shape) == -1);
    }

    TEST_CASE("NC4 blob sizing rejects rounded channel overflow", "[nn-memory-safety]") {
        nn::BlobDesc desc;
        desc.data_type   = nn::DATA_TYPE_FLOAT;
        desc.data_format = nn::DATA_FORMAT_NC4HW4;
        desc.dims        = {1, std::numeric_limits<int>::max(), 1, 1};

        auto info = nn::Calculate1DMemorySize(desc);
        REQUIRE(info.dims == std::vector<int>{-1});
        REQUIRE(nn::GetBlobMemoryBytesSize(info) == -1);
    }

    TEST_CASE("Memory pool never narrows oversized requests to a smaller pool", "[nn-memory-safety]") {
        auto allocator      = std::make_unique<CountingAllocator>();
        auto* allocator_ptr = allocator.get();
        mem::MemoryPoolMng pool(std::move(allocator), {1024, 2048});

        REQUIRE(pool.Acquire(std::numeric_limits<size_t>::max()) == nullptr);
        REQUIRE(allocator_ptr->allocation_count == 0);
    }

#ifdef COSMO_NN_USE_SOPHON_BACKEND
    TEST_CASE("Sophon device memory views enforce descriptor bounds", "[nn-memory-safety][sophon]") {
        const auto base = bm_mem_from_device(0x100000U, 4096);
        bm_device_mem_t view{};
        REQUIRE(nn::MakeDeviceMemoryView(base, 1024, 2048, &view));
        REQUIRE(bm_mem_get_device_addr(view) == 0x100400U);
        REQUIRE(bm_mem_get_device_size(view) == 2048);
        REQUIRE_FALSE(nn::MakeDeviceMemoryView(base, 4090, 16, &view));
        REQUIRE_FALSE(nn::MakeDeviceMemoryView(base, 0, 0, &view));

        std::array<bm_device_mem_t, 3> planes{};
        REQUIRE(nn::MakeThreePlaneDeviceMemoryView(base, 0, 1024, &planes));
        REQUIRE(bm_mem_get_device_addr(planes[2]) == 0x100800U);
        REQUIRE_FALSE(nn::MakeThreePlaneDeviceMemoryView(base, 2048, 1024, &planes));
    }
#endif

}  // namespace
}  // namespace cosmo
