#include "nn/guard/CemRknnV1Loader.h"

#if defined(COSMO_NN_USE_RKNN_BACKEND) && defined(COSMO_HAS_MODEL_GUARD)

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <new>
#include <utility>

namespace cosmo::nn {
namespace {

    class ArtifactOwner final {
    public:
        ArtifactOwner(CmgRknnV1Artifact* artifact, const CmgRknnV1Api& api) noexcept
            : artifact_(artifact), api_(api) {}
        ~ArtifactOwner() noexcept {
            if (artifact_)
                api_.close_artifact(api_.context, artifact_);
        }
        CmgRknnV1Artifact* Get() const noexcept {
            return artifact_;
        }

    private:
        CmgRknnV1Artifact* artifact_ = nullptr;
        CmgRknnV1Api api_{};
    };

    bool IsApiAvailable(const CmgRknnV1Api& api) noexcept {
        return api.abi_major == CMG_RKNN_V1_ABI_MAJOR && api.open_artifact && api.get_artifact_info &&
               api.load_segment && api.close_artifact;
    }

    std::uint64_t Fingerprint(const std::uint8_t* data, size_t size) noexcept {
        constexpr std::uint64_t kOffset = UINT64_C(1469598103934665603);
        constexpr std::uint64_t kPrime  = UINT64_C(1099511628211);
        std::uint64_t value             = kOffset;
        for (size_t index = 0; index < size; ++index) {
            value ^= data[index];
            value *= kPrime;
        }
        return value;
    }

    RknnGuardLoadResult Failure(RknnGuardLoadError error, CmgRknnV1Status status) {
        RknnGuardLoadResult result;
        result.error        = error;
        result.guard_status = status;
        return result;
    }

    class ScopedFd final {
    public:
        explicit ScopedFd(int fd) noexcept : fd_(fd) {}
        ~ScopedFd() noexcept {
            if (fd_ >= 0)
                close(fd_);
        }
        [[nodiscard]] int Get() const noexcept {
            return fd_;
        }

    private:
        int fd_;
    };

    void SecureClear(void* data, size_t size) noexcept {
        volatile std::uint8_t* bytes = static_cast<volatile std::uint8_t*>(data);
        while (size-- > 0)
            *bytes++ = 0;
    }

    CmgRknnV1Status OpenFrozen(void*, const char* path, const char* certificate_path,
                               CmgRknnV1Artifact** artifact) {
        if (artifact)
            *artifact = nullptr;
        if (!path || !certificate_path || certificate_path[0] != '/')
            return CMG_RKNN_V1_RESOURCE_INVALID_ARGUMENT;
        const int raw_fd = open(certificate_path, O_RDONLY | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW);
        if (raw_fd < 0)
            return errno == ENOENT ? CMG_RKNN_V1_CERTIFICATE_UNAVAILABLE : CMG_RKNN_V1_CERTIFICATE_REJECTED;
        ScopedFd fd(raw_fd);
        std::array<std::uint8_t, CMG_RKNN_V1_DEVICE_CERTIFICATE_SIZE> certificate{};
        struct stat info {};
        if (fstat(fd.Get(), &info) != 0 || !S_ISREG(info.st_mode) ||
            info.st_size != static_cast<off_t>(certificate.size())) {
            return CMG_RKNN_V1_CERTIFICATE_REJECTED;
        }
        size_t offset = 0;
        while (offset < certificate.size()) {
            const ssize_t count = read(fd.Get(), certificate.data() + offset, certificate.size() - offset);
            if (count < 0 && errno == EINTR)
                continue;
            if (count <= 0) {
                SecureClear(certificate.data(), certificate.size());
                return CMG_RKNN_V1_CERTIFICATE_REJECTED;
            }
            offset += static_cast<size_t>(count);
        }
        CmgRknnV1OpenOptions options{};
        options.struct_size             = sizeof(options);
        options.installed_model_path    = path;
        options.device_certificate      = certificate.data();
        options.device_certificate_size = certificate.size();
        const auto status               = CmgRknnV1OpenArtifact(&options, artifact);
        SecureClear(certificate.data(), certificate.size());
        return status;
    }
    CmgRknnV1Status GetFrozenInfo(void*, const CmgRknnV1Artifact* artifact, CmgRknnV1ArtifactInfo* info) {
        return CmgRknnV1GetArtifactInfo(artifact, info);
    }
    CmgRknnV1Status LoadFrozen(void*, CmgRknnV1Artifact* artifact, std::uint32_t index,
                               std::uint64_t* context) {
        return CmgRknnV1LoadSegment(artifact, index, context);
    }
    void CloseFrozen(void*, CmgRknnV1Artifact* artifact) noexcept {
        CmgRknnV1CloseArtifact(artifact);
    }
    void DestroyNativeContext(void*, std::uint64_t context) noexcept {
        if (context != 0)
            (void)rknn_destroy(static_cast<rknn_context>(context));
    }

}  // namespace

GuardOwnedRknnContext::GuardOwnedRknnContext(std::uint64_t context, RknnContextApi api) noexcept
    : context_(context), api_(api) {}
GuardOwnedRknnContext::~GuardOwnedRknnContext() noexcept {
    Reset();
}
GuardOwnedRknnContext::GuardOwnedRknnContext(GuardOwnedRknnContext&& other) noexcept
    : context_(std::exchange(other.context_, 0)), api_(other.api_) {}
GuardOwnedRknnContext& GuardOwnedRknnContext::operator=(GuardOwnedRknnContext&& other) noexcept {
    if (this != &other) {
        Reset();
        context_ = std::exchange(other.context_, 0);
        api_     = other.api_;
    }
    return *this;
}
std::uint64_t GuardOwnedRknnContext::Get() const noexcept {
    return context_;
}
std::uint64_t GuardOwnedRknnContext::Release() noexcept {
    return std::exchange(context_, 0);
}
void GuardOwnedRknnContext::Reset() noexcept {
    if (context_ != 0 && api_.destroy)
        api_.destroy(api_.context, context_);
    context_ = 0;
}

const CmgRknnV1Api& FrozenCmgRknnV1Api() noexcept {
    static const CmgRknnV1Api api{CMG_RKNN_V1_ABI_MAJOR, nullptr,    OpenFrozen,
                                  GetFrozenInfo,         LoadFrozen, CloseFrozen};
    return api;
}
const RknnContextApi& NativeRknnContextApi() noexcept {
    static const RknnContextApi api{nullptr, DestroyNativeContext};
    return api;
}

RknnGuardLoadResult LoadCemRknnV1Artifact(const CmgRknnV1Api& guard_api, const RknnContextApi& context_api,
                                          const char* model_path, const char* certificate_path) {
    if (!model_path || model_path[0] == '\0' || !certificate_path || certificate_path[0] == '\0')
        return Failure(RknnGuardLoadError::kInvalidArgument, CMG_RKNN_V1_RESOURCE_INVALID_ARGUMENT);
    if (!IsApiAvailable(guard_api) || !context_api.destroy)
        return Failure(RknnGuardLoadError::kAbiUnavailable, CMG_RKNN_V1_RESOURCE_ABI_MISMATCH);

    CmgRknnV1Artifact* raw_artifact = nullptr;
    const CmgRknnV1Status open_status =
        guard_api.open_artifact(guard_api.context, model_path, certificate_path, &raw_artifact);
    if (open_status != CMG_RKNN_V1_OK) {
        if (raw_artifact) {
            guard_api.close_artifact(guard_api.context, raw_artifact);
            return Failure(RknnGuardLoadError::kAbiContractViolation, CMG_RKNN_V1_RESOURCE_INTERNAL);
        }
        return Failure(RknnGuardLoadError::kArtifactOpenFailed, open_status);
    }
    if (!raw_artifact)
        return Failure(RknnGuardLoadError::kAbiContractViolation, CMG_RKNN_V1_RESOURCE_INTERNAL);
    ArtifactOwner artifact(raw_artifact, guard_api);

    CmgRknnV1ArtifactInfo info{};
    info.struct_size                  = sizeof(info);
    const CmgRknnV1Status info_status = guard_api.get_artifact_info(guard_api.context, artifact.Get(), &info);
    if (info_status != CMG_RKNN_V1_OK)
        return Failure(RknnGuardLoadError::kArtifactInfoFailed, info_status);
    if (info.struct_size != CMG_RKNN_V1_ARTIFACT_INFO_SIZE || info.reserved != 0 ||
        info.source_format != CMG_RKNN_V1_SOURCE_RAW_RKNN || info.segment_count == 0)
        return Failure(RknnGuardLoadError::kArtifactInfoInvalid, CMG_RKNN_V1_RESOURCE_INTERNAL);

    RknnGuardLoadResult result;
    result.model_fingerprint = Fingerprint(info.model_identity_sha256, sizeof(info.model_identity_sha256));
    try {
        result.contexts.reserve(info.segment_count);
    } catch (const std::bad_alloc&) {
        return Failure(RknnGuardLoadError::kNoMemory, CMG_RKNN_V1_RESOURCE_NO_MEMORY);
    }
    for (std::uint32_t index = 0; index < info.segment_count; ++index) {
        std::uint64_t context = 0;
        const CmgRknnV1Status load_status =
            guard_api.load_segment(guard_api.context, artifact.Get(), index, &context);
        GuardOwnedRknnContext owned(context, context_api);
        if (load_status != CMG_RKNN_V1_OK) {
            if (context != 0)
                return Failure(RknnGuardLoadError::kAbiContractViolation, CMG_RKNN_V1_RESOURCE_INTERNAL);
            return Failure(RknnGuardLoadError::kSegmentLoadFailed, load_status);
        }
        if (context == 0)
            return Failure(RknnGuardLoadError::kAbiContractViolation, CMG_RKNN_V1_RESOURCE_INTERNAL);
        try {
            result.contexts.push_back(std::move(owned));
        } catch (const std::bad_alloc&) {
            return Failure(RknnGuardLoadError::kNoMemory, CMG_RKNN_V1_RESOURCE_NO_MEMORY);
        }
    }
    return result;
}

}  // namespace cosmo::nn

#endif
