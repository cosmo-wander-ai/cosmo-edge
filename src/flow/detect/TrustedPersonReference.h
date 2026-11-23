#pragma once

#include <string>

#if defined(COSMO_NN_USE_AXERA_BACKEND) && defined(COSMO_AXERA_PERSON_ROUTE_CANDIDATE)
#include "flow/detect/AxeraTrustedDirectories.h"
#include "flow/detect/PersonTemplateContract.h"
#include "nn/device/axera/axera_face_deployment.h"
#include "nn/device/axera/axera_person_deployment.h"
#include "nn/device/axera/axera_person_manifest_name.h"
#include "service/detail/ServiceRegistry.h"
#include "service/system/IAppInfoService.h"
#include "util/PathUtil.h"

namespace cosmo::person_template_contract {
// Only fixed process roots and the actual trusted loader can supply identity
// facts. Request data never supplies file paths, hashes, ownership, or device ID.
// This proves a reference may be considered, not that runtime binding succeeds.
struct TrustedPersonRoutes {
    bool exact_scene{false};
    bool reusable_reference{false};
    float minimum_confidence{0.f};
};
inline TrustedPersonRoutes ResolveTrustedPersonRoutes(const std::string& scene) noexcept {
    try {
        using namespace axera_trusted_directories;
        const auto root_path = path::GetAppBaseDir();
        Fd root{OpenTrustedAbsoluteDirectory(root_path)};
        struct stat root_stat {};
        if (root.value < 0 || ::fstat(root.value, &root_stat) || !S_ISDIR(root_stat.st_mode) ||
            (root_stat.st_mode & 0022) || (root_stat.st_uid != ::geteuid() && root_stat.st_uid != 0))
            return {};
        const auto publisher = root_stat.st_uid;
        Fd resource{OpenDirectory(root.value, "resource")};
        if (!TrustedDirectory(resource.value, publisher))
            return {};
        Fd native{OpenDirectory(resource.value, "axera-native")};
        if (!TrustedDirectory(native.value, publisher))
            return {};
        Fd contracts{OpenDirectory(native.value, "contracts")};
        if (!TrustedDirectory(contracts.value, publisher))
            return {};
        Fd models{OpenDirectory(resource.value, "models")};
        if (!TrustedDirectory(models.value, publisher))
            return {};
        const auto manifest = nn::axera::person_manifest::Select(contracts.value, "1001003", scene);
        if (!manifest)
            return {};
        const auto service_id = service::ServiceRegistry::Instance().Get<service::IAppInfoService>().DevId();
        if (service_id.empty())
            return {};
        namespace deployment = nn::axera::person_deployment;
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
        const auto identity = nn::axera::face_deployment::ReadLocalSshHostKeySha256();
        if (identity.empty())
            return {};
        auto loaded = deployment::LoadIsolatedDeviceTestFromDirectory(
            contracts.value, models.value, *manifest, publisher, service_id, identity);
#else
        auto loaded =
            deployment::LoadFromDirectory(contracts.value, models.value, *manifest, publisher, service_id);
#endif
        if (loaded.error != deployment::Error::None || !loaded.deployment ||
            loaded.deployment->atomic_code != "1001003" || !loaded.deployment->model_bytes)
            return {};
        const auto& contract = loaded.deployment->contract;
        return {contract.scene_id == scene,
                CanReuseReference(true, *manifest, contract.scene_id, contract.model_sha256,
                                  loaded.deployment->source_onnx_sha256, contract.semantic_contract),
                contract.graph_confidence};
    } catch (...) {
        return {};
    }
}
inline bool VerifyTrustedReference(const std::string& scene) noexcept {
    return ResolveTrustedPersonRoutes(scene).reusable_reference;
}
}  // namespace cosmo::person_template_contract
#endif
