#include "nn/device/axera/axera_person_deployment.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "cryptopp/sha.h"
#include "nlohmann/json.hpp"
#include "nn/device/axera/axera_hs_contract.h"
#include "nn/device/axera/axera_mouse_contract.h"
#include "nn/device/axera/axera_person_manifest_name.h"
#include "nn/device/axera/axera_rubbish_contract.h"

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace cosmo::nn::axera::person_deployment {
namespace {
    using Json                      = nlohmann::json;
    constexpr const char* kSemantic = "ax650-nv12-1080p-pitch2048-bt709-limited-person-bgr-pad140-nms-v1";
    constexpr std::size_t kManifestLimit = 128 * 1024, kEvidenceLimit = 1024 * 1024;
    constexpr std::size_t kEvidenceArtifactLimit = 64 * 1024 * 1024;
    constexpr std::size_t kModelLimit            = 1024ULL * 1024 * 1024;
    constexpr const char* kHsProductionRoles[]   = {"source_graph", "toolchain", "calibration", "compile_log",
                                                    "device_smoke"};
    constexpr const char* kRoles[] = {"source_graph",  "toolchain",           "calibration", "compile_log",
                                      "single_person", "multiple_people",     "bframes",     "color_witness",
                                      "capacity",      "numerical_evaluation"};
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
    // Isolated testing is how these device properties are established. The
    // missing observations must be declared below, never recorded as passes.
    constexpr const char* kIsolatedRoles[] = {"source_graph", "toolchain", "calibration", "compile_log"};
#endif

    bool SafeName(const std::string& s) {
        if (s.empty() || s.size() > 200 || s == "." || s == "..")
            return false;
        return std::all_of(s.begin(), s.end(), [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
                   c == '-' || c == '.';
        });
    }
    bool Digest(const std::string& s) {
        return s.size() == 64 && std::all_of(s.begin(), s.end(), [](char c) {
                   return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
               });
    }
    bool Keys(const Json& j, std::initializer_list<const char*> expected) {
        if (!j.is_object() || j.size() != expected.size())
            return false;
        for (const auto* key : expected)
            if (!j.contains(key))
                return false;
        return true;
    }
    bool Parse(const std::vector<std::uint8_t>& bytes, Json& result) {
        bool valid = true;
        std::vector<std::set<std::string>> objects;
        auto callback = [&](int depth, Json::parse_event_t event, Json& value) {
            if (depth > 32)
                valid = false;
            if (event == Json::parse_event_t::object_start)
                objects.emplace_back();
            if (event == Json::parse_event_t::key) {
                if (objects.empty() || !objects.back().insert(value.get<std::string>()).second)
                    valid = false;
            }
            if (event == Json::parse_event_t::object_end) {
                if (objects.empty())
                    valid = false;
                else
                    objects.pop_back();
            }
            return true;
        };
        result = Json::parse(bytes.begin(), bytes.end(), callback, false);
        return valid && objects.empty() && !result.is_discarded();
    }
    std::string Sha256(const std::vector<std::uint8_t>& bytes) {
        std::array<std::uint8_t, CryptoPP::SHA256::DIGESTSIZE> result{};
        CryptoPP::SHA256().CalculateDigest(result.data(), bytes.data(), bytes.size());
        constexpr char chars[] = "0123456789abcdef";
        std::string hex;
        hex.reserve(64);
        for (auto v : result) {
            hex.push_back(chars[v >> 4]);
            hex.push_back(chars[v & 15]);
        }
        return hex;
    }
    bool SmallString(const std::string& s) {
        return !s.empty() && s.size() <= 200 && s.find('\0') == s.npos;
    }

#if defined(__unix__) || defined(__APPLE__)
    struct Fd {
        int fd{-1};
        ~Fd() {
            if (fd >= 0)
                ::close(fd);
        }
    };
    bool SameFile(const struct stat& a, const struct stat& b) {
#if defined(__APPLE__)
        return a.st_dev == b.st_dev && a.st_ino == b.st_ino && a.st_size == b.st_size &&
               a.st_nlink == b.st_nlink && a.st_mode == b.st_mode && a.st_uid == b.st_uid &&
               a.st_mtimespec.tv_sec == b.st_mtimespec.tv_sec &&
               a.st_mtimespec.tv_nsec == b.st_mtimespec.tv_nsec &&
               a.st_ctimespec.tv_sec == b.st_ctimespec.tv_sec &&
               a.st_ctimespec.tv_nsec == b.st_ctimespec.tv_nsec;
#else
        return a.st_dev == b.st_dev && a.st_ino == b.st_ino && a.st_size == b.st_size &&
               a.st_nlink == b.st_nlink && a.st_mode == b.st_mode && a.st_uid == b.st_uid &&
               a.st_mtim.tv_sec == b.st_mtim.tv_sec && a.st_mtim.tv_nsec == b.st_mtim.tv_nsec &&
               a.st_ctim.tv_sec == b.st_ctim.tv_sec && a.st_ctim.tv_nsec == b.st_ctim.tv_nsec;
#endif
    }
    bool Read(int dir, const std::string& name, uid_t publisher, std::size_t limit,
              std::vector<std::uint8_t>& output) {
        if (!SafeName(name))
            return false;
        Fd file{::openat(dir, name.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK)};
        if (file.fd < 0)
            return false;
        struct stat before {
        }, after{};
        if (::fstat(file.fd, &before) || !S_ISREG(before.st_mode) || before.st_nlink != 1 ||
            before.st_uid != publisher || (before.st_mode & 0022) || before.st_size <= 0 ||
            static_cast<std::uintmax_t>(before.st_size) > limit)
            return false;
        output.resize(static_cast<std::size_t>(before.st_size));
        std::size_t offset = 0;
        while (offset < output.size()) {
            const auto n = ::read(file.fd, output.data() + offset, output.size() - offset);
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0)
                return false;
            offset += static_cast<std::size_t>(n);
        }
        return !::fstat(file.fd, &after) && SameFile(before, after);
    }
    bool ReadOnlyDirectory(int dir, uid_t publisher) {
        struct stat statbuf {};
        // The authorized publisher and service may both be root. Ownership and
        // DAC prevent non-authorized group/other modification; the trusted
        // publisher must use atomic replacement, never in-place mutation.
        return !::fstat(dir, &statbuf) && S_ISDIR(statbuf.st_mode) && statbuf.st_uid == publisher &&
               !(statbuf.st_mode & 0022);
    }
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
    std::string ReadHostKeyFromRoot(int root_fd, uid_t publisher) {
        Fd root{::fcntl(root_fd, F_DUPFD_CLOEXEC, 0)};
        if (root.fd < 0 || !ReadOnlyDirectory(root.fd, publisher))
            return {};
        Fd etc{::openat(root.fd, "etc", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)};
        if (etc.fd < 0 || !ReadOnlyDirectory(etc.fd, publisher))
            return {};
        Fd ssh{::openat(etc.fd, "ssh", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)};
        if (ssh.fd < 0 || !ReadOnlyDirectory(ssh.fd, publisher))
            return {};
        std::vector<std::uint8_t> key;
        if (!Read(ssh.fd, "ssh_host_ed25519_key.pub", publisher, 4096, key))
            return {};
        constexpr char prefix[] = "ssh-ed25519 ";
        if (key.size() <= sizeof(prefix) - 1 || !std::equal(prefix, prefix + sizeof(prefix) - 1, key.begin()))
            return {};
        return Sha256(key);
    }
#endif
#endif

    bool Codes(const Json& tensor, face_binding::SdkCodes& codes, std::uint32_t& quant,
               const std::vector<std::size_t>& shape, std::size_t bytes, const std::string& type,
               const std::string& layout, face_binding::ModelStrideRule rule,
               const std::vector<std::size_t>& strides) {
        if (!Keys(tensor, {"name", "type", "layout", "shape", "bytes", "strides", "sdk_dtype", "sdk_layout",
                           "sdk_memtype", "sdk_colorspace", "quantization"}) ||
            !SmallString(tensor.at("name").get<std::string>()) || tensor.at("type") != type ||
            tensor.at("layout") != layout || tensor.at("shape").get<std::vector<std::size_t>>() != shape ||
            tensor.at("bytes").get<std::size_t>() != bytes)
            return false;
        const auto got = tensor.at("strides").get<std::vector<std::size_t>>();
        if (got != strides &&
            !(got.empty() && rule == face_binding::ModelStrideRule::AllowUnconstrainedWithExplicitRuntimeIo))
            return false;
        codes = {tensor.at("sdk_dtype").get<int>(), tensor.at("sdk_layout").get<int>(),
                 tensor.at("sdk_memtype").get<int>(), tensor.at("sdk_colorspace").get<int>(), true};
        quant = tensor.at("quantization").get<std::uint32_t>();
        return true;
    }
}  // namespace

static Result LoadInternal(int pinned_contracts_fd, int pinned_models_root_fd,
                           const std::string& manifest_basename, std::uint32_t expected_publisher_uid,
                           const std::string& expected_service_id, bool isolated,
                           const std::string& expected_device_identity, bool hs, bool mouse = false,
                           bool rubbish = false) {
#if !defined(__unix__) && !defined(__APPLE__)
    (void)pinned_contracts_fd;
    (void)pinned_models_root_fd;
    (void)manifest_basename;
    (void)expected_publisher_uid;
    (void)expected_service_id;
    (void)isolated;
    (void)expected_device_identity;
    (void)hs;
    (void)mouse;
    (void)rubbish;
    return {Error::Unavailable, nullptr};
#else
    const bool special = hs || mouse || rubbish;
    try {
#if !defined(COSMO_AXERA_MOUSE_ROUTE_CANDIDATE) ||                                                           \
    (!defined(COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST) && !defined(COSMO_AXERA_MOUSE_ROUTE_PRODUCTION))
        if (mouse)
            return {Error::InvalidManifest, nullptr};
#endif
#if !defined(COSMO_AXERA_RUBBISH_ROUTE_CANDIDATE) ||                                                         \
    (!defined(COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST) && !defined(COSMO_AXERA_RUBBISH_ROUTE_PRODUCTION))
        if (rubbish)
            return {Error::InvalidManifest, nullptr};
#endif
#ifndef COSMO_AXERA_MOUSE_ROUTE_PRODUCTION
        if (mouse && !isolated)
            return {Error::InvalidManifest, nullptr};
#endif
#ifndef COSMO_AXERA_RUBBISH_ROUTE_PRODUCTION
        if (rubbish && !isolated)
            return {Error::InvalidManifest, nullptr};
#endif
#if !defined(COSMO_AXERA_HS_ROUTE_CANDIDATE) ||                                                              \
    (!defined(COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST) && !defined(COSMO_AXERA_HS_ROUTE_PRODUCTION))
        if (hs)
            return {Error::InvalidManifest, nullptr};
#endif
#ifndef COSMO_AXERA_HS_ROUTE_PRODUCTION
        if (hs && !isolated)
            return {Error::InvalidManifest, nullptr};
#endif
        Fd dir{::fcntl(pinned_contracts_fd, F_DUPFD_CLOEXEC, 0)};
        Fd models_root{::fcntl(pinned_models_root_fd, F_DUPFD_CLOEXEC, 0)};
        if (dir.fd < 0 || models_root.fd < 0 || !SmallString(expected_service_id) ||
            !ReadOnlyDirectory(dir.fd, static_cast<uid_t>(expected_publisher_uid)) ||
            !ReadOnlyDirectory(models_root.fd, static_cast<uid_t>(expected_publisher_uid)))
            return {Error::InvalidAuthority, nullptr};
        std::vector<std::uint8_t> manifest_bytes;
        Json j;
        if (!Read(dir.fd, manifest_basename, expected_publisher_uid, kManifestLimit, manifest_bytes) ||
            !Parse(manifest_bytes, j))
            return {Error::InvalidManifest, nullptr};
        if (isolated) {
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
            if (!Digest(expected_device_identity) ||
                !Keys(j, {"schema", "status", "service_id", "authority_revision", "atomic_code", "scene_id",
                          "scene_threshold", "model", "runtime", "evidence", "reference_weight_identity",
                          "test_scope", "not_proven"}) ||
                j.at("schema") != (hs        ? "cosmoedge.headshoulder-isolated-test.v1"
                                   : rubbish ? "cosmoedge.rubbish-isolated-test.v1"
                                   : mouse   ? "cosmoedge.mouse-isolated-test.v1"
                                             : "cosmoedge.person-isolated-test.v1") ||
                j.at("status") != "ISOLATED_DEVICE_TEST_ONLY" ||
                !Keys(j.at("test_scope"), {"device_identity", "run_id"}) ||
                j.at("test_scope").at("device_identity") != expected_device_identity ||
                !SmallString(j.at("test_scope").at("run_id").get<std::string>()))
                return {Error::InvalidManifest, nullptr};
            const auto limitations = j.at("not_proven").get<std::vector<std::string>>();
            const std::set<std::string> required =
                special ? std::set<std::string>{"color_witness",       "capacity",
                                                "reference_identity",  "full_pipeline",
                                                "real_world_accuracy", "reference_template_binding"}
                        : std::set<std::string>{"color_witness",
                                                "capacity",
                                                "reference_identity",
                                                "full_pipeline",
                                                "single_person",
                                                "multiple_people",
                                                "bframes",
                                                "numerical_evaluation"};
            if (limitations.size() != required.size() ||
                std::set<std::string>(limitations.begin(), limitations.end()) != required)
                return {Error::InvalidManifest, nullptr};
#else
            return {Error::InvalidManifest, nullptr};
#endif
        } else if (special) {
#if defined(COSMO_AXERA_HS_ROUTE_PRODUCTION) || defined(COSMO_AXERA_MOUSE_ROUTE_PRODUCTION) ||               \
    defined(COSMO_AXERA_RUBBISH_ROUTE_PRODUCTION)
            if (!Keys(j, {"schema", "status", "service_id", "authority_revision", "atomic_code", "scene_id",
                          "scene_threshold", "model", "runtime", "evidence", "reference_weight_identity",
                          "not_proven"}) ||
                j.at("schema") != (hs        ? "cosmoedge.headshoulder-deployment.v1"
                                   : rubbish ? "cosmoedge.rubbish-deployment.v1"
                                             : "cosmoedge.mouse-deployment.v1") ||
                j.at("status") != "DEPLOYABLE")
                return {Error::InvalidManifest, nullptr};
            const auto limitations = j.at("not_proven").get<std::vector<std::string>>();
            const std::set<std::string> required{"color_witness",       "capacity",
                                                 "reference_identity",  "full_pipeline",
                                                 "real_world_accuracy", "reference_template_binding"};
            if (limitations.size() != required.size() ||
                std::set<std::string>(limitations.begin(), limitations.end()) != required)
                return {Error::InvalidManifest, nullptr};
#else
            return {Error::InvalidManifest, nullptr};
#endif
        } else if (!Keys(j,
                         {"schema", "status", "service_id", "authority_revision", "atomic_code", "scene_id",
                          "scene_threshold", "model", "runtime", "evidence", "reference_weight_identity"}) ||
                   j.at("schema") != "cosmoedge.person-deployment.v1" || j.at("status") != "DEPLOYABLE") {
            return {Error::InvalidManifest, nullptr};
        }
        if (j.at("service_id") != expected_service_id)
            return {Error::InvalidManifest, nullptr};
        auto deployment                         = std::make_unique<Deployment>();
        deployment->service_id                  = expected_service_id;
        deployment->atomic_code                 = j.at("atomic_code").get<std::string>();
        deployment->contract.authority_revision = j.at("authority_revision").get<std::string>();
        deployment->contract.scene_id           = j.at("scene_id").get<std::string>();
        deployment->reference_weight_identity   = j.at("reference_weight_identity").get<std::string>();
        deployment->scene_threshold             = j.at("scene_threshold").get<float>();
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
        if (isolated) {
            deployment->isolated_device_test_only = true;
            deployment->test_device_identity      = expected_device_identity;
            deployment->test_run_id               = j.at("test_scope").at("run_id").get<std::string>();
        }
#endif
        if ((hs        ? !hs_contract::ManifestMatches(manifest_basename, deployment->atomic_code,
                                                       deployment->contract.scene_id)
             : rubbish ? !rubbish_contract::ManifestMatches(manifest_basename, deployment->atomic_code,
                                                            deployment->contract.scene_id)
             : mouse   ? !mouse_contract::ManifestMatches(manifest_basename, deployment->atomic_code,
                                                          deployment->contract.scene_id)
                       : (deployment->atomic_code != "1001003" ||
                        !person_manifest::Matches(manifest_basename, deployment->atomic_code,
                                                    deployment->contract.scene_id))) ||
            !SafeName(deployment->atomic_code) || !SmallString(deployment->contract.authority_revision) ||
            !SmallString(deployment->contract.scene_id) ||
            deployment->reference_weight_identity != "unverified")
            return {Error::InvalidManifest, nullptr};
        const auto& model = j.at("model");
        if (special ? !Keys(model, {"directory", "file", "sha256", "source_onnx_sha256",
                                    "runtime_graph_sha256", "source_class_id", "source_class_name",
                                    "scene_class_label", "reference_mapping_status"})
                    : !Keys(model, {"directory", "file", "sha256", "source_onnx_sha256"}))
            return {Error::InvalidManifest, nullptr};
        const auto model_directory        = model.at("directory").get<std::string>();
        const auto model_name             = model.at("file").get<std::string>();
        deployment->contract.model_sha256 = model.at("sha256").get<std::string>();
        deployment->source_onnx_sha256    = model.at("source_onnx_sha256").get<std::string>();
        if (hs && (deployment->contract.model_sha256 != hs_contract::ModelSha256() ||
                   deployment->source_onnx_sha256 != hs_contract::SourceOnnxSha256() ||
                   model.at("runtime_graph_sha256") != hs_contract::RuntimeGraphSha256() ||
                   model.at("source_class_id") != 0 ||
                   model.at("source_class_name") != hs_contract::kSourceClassLabel ||
                   model.at("scene_class_label") != hs_contract::kClassLabel ||
                   model.at("reference_mapping_status") != hs_contract::kReferenceMappingStatus ||
                   !hs_contract::IsThreshold(deployment->scene_threshold)))
            return {Error::InvalidManifest, nullptr};
        if (mouse && (deployment->contract.model_sha256 != mouse_contract::ModelSha256() ||
                      deployment->source_onnx_sha256 != mouse_contract::SourceOnnxSha256() ||
                      model.at("runtime_graph_sha256") != mouse_contract::RuntimeGraphSha256() ||
                      model.at("source_class_id") != 0 ||
                      model.at("source_class_name") != mouse_contract::kSourceClassLabel ||
                      model.at("scene_class_label") != mouse_contract::kClassLabel ||
                      model.at("reference_mapping_status") != mouse_contract::kReferenceMappingStatus ||
                      !mouse_contract::IsThreshold(deployment->scene_threshold)))
            return {Error::InvalidManifest, nullptr};
        if (rubbish && (deployment->contract.model_sha256 != rubbish_contract::ModelSha256() ||
                        deployment->source_onnx_sha256 != rubbish_contract::SourceOnnxSha256() ||
                        model.at("runtime_graph_sha256") != rubbish_contract::RuntimeGraphSha256() ||
                        model.at("source_class_id") != 0 ||
                        model.at("source_class_name") != rubbish_contract::kSourceClassLabel ||
                        model.at("scene_class_label") != rubbish_contract::kClassLabel ||
                        model.at("reference_mapping_status") != rubbish_contract::kReferenceMappingStatus ||
                        !rubbish_contract::IsThreshold(deployment->scene_threshold)))
            return {Error::InvalidManifest, nullptr};
        if (!SafeName(model_directory) || !SafeName(model_name) || model_name == manifest_basename ||
            model_name.size() < 9 || model_name.substr(model_name.size() - 8) != ".axmodel" ||
            !Digest(deployment->contract.model_sha256) || !Digest(deployment->source_onnx_sha256))
            return {Error::InvalidManifest, nullptr};
        Fd model_dir{::openat(models_root.fd, model_directory.c_str(),
                              O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)};
        if (model_dir.fd < 0 || !ReadOnlyDirectory(model_dir.fd, static_cast<uid_t>(expected_publisher_uid)))
            return {Error::InvalidAuthority, nullptr};
        const auto& runtime = j.at("runtime");
        if (!Keys(runtime,
                  {"semantic_contract", "source_width", "source_height", "plane_pitch_bytes", "source_format",
                   "color_matrix", "color_range", "y", "uv", "output", "capacity", "required_capacity",
                   "confidence", "iou", "model_stride_rule", "max_batch_size", "dynamic_batch_size"}) ||
            runtime.at("semantic_contract") != (hs        ? hs_contract::kSemantic
                                                : rubbish ? rubbish_contract::kSemantic
                                                : mouse   ? mouse_contract::kSemantic
                                                          : kSemantic) ||
            runtime.at("source_width") != 1920 || runtime.at("source_height") != 1080 ||
            runtime.at("plane_pitch_bytes") != 2048 || runtime.at("source_format") != "NV12" ||
            runtime.at("color_matrix") != "BT709" || runtime.at("color_range") != "LIMITED" ||
            runtime.at("capacity") != 64 || !runtime.at("required_capacity").is_number_integer() ||
            runtime.at("max_batch_size") != 1 || runtime.at("dynamic_batch_size") != false)
            return {Error::InvalidManifest, nullptr};
        auto& c                       = deployment->contract;
        c.semantic_contract           = hs        ? hs_contract::kSemantic
                                        : rubbish ? rubbish_contract::kSemantic
                                        : mouse   ? mouse_contract::kSemantic
                                                  : kSemantic;
        c.result_capacity             = runtime.at("capacity").get<std::size_t>();
        c.required_capacity           = runtime.at("required_capacity").get<std::size_t>();
        c.graph_confidence            = runtime.at("confidence").get<float>();
        c.graph_iou                   = runtime.at("iou").get<float>();
        c.expected_max_batch_size     = 1;
        c.expected_dynamic_batch_size = false;
        if (!c.required_capacity || c.required_capacity > c.result_capacity || c.graph_confidence != .1f ||
            c.graph_iou != .35f || !std::isfinite(c.graph_confidence) || c.graph_confidence <= 0.f ||
            c.graph_confidence > 1.f || !std::isfinite(c.graph_iou) || c.graph_iou < 0.f ||
            c.graph_iou >= 1.f || !std::isfinite(deployment->scene_threshold) ||
            deployment->scene_threshold < c.graph_confidence || deployment->scene_threshold > 1.f)
            return {Error::InvalidManifest, nullptr};
        const auto stride_rule = runtime.at("model_stride_rule").get<std::string>();
        if (stride_rule == "explicit-model-strides")
            c.model_stride_rule = face_binding::ModelStrideRule::RequireExplicitMetadata;
        else if (stride_rule == "unconstrained-model-explicit-runtime-strides")
            c.model_stride_rule = face_binding::ModelStrideRule::AllowUnconstrainedWithExplicitRuntimeIo;
        else
            return {Error::InvalidManifest, nullptr};
        const auto& y   = runtime.at("y");
        const auto& uv  = runtime.at("uv");
        const auto& out = runtime.at("output");
        // The person graph requires K64. Derive the output
        // contract from the selected K, never from the reported byte count.
        const std::size_t ybytes = 2048 * 1080, uvbytes = 2048 * 540;
        const std::size_t rows = c.result_capacity + 1, elements = rows * 6;
        if (!Codes(y, c.expected_sdk_codes[0], c.expected_quantization_values[0], {1, 1, 1080, 2048}, ybytes,
                   "uint8", "nchw", c.model_stride_rule, {ybytes, ybytes, 2048, 1}) ||
            !Codes(uv, c.expected_sdk_codes[1], c.expected_quantization_values[1], {1, 1, 540, 2048}, uvbytes,
                   "uint8", "nchw", c.model_stride_rule, {uvbytes, uvbytes, 2048, 1}) ||
            !Codes(out, c.expected_sdk_codes[2], c.expected_quantization_values[2], {1, rows, 6},
                   elements * sizeof(float), "float32", "other", c.model_stride_rule, {elements, 6, 1}))
            return {Error::InvalidManifest, nullptr};
        c.y_name        = y.at("name").get<std::string>();
        c.uv_name       = uv.at("name").get<std::string>();
        c.output_name   = out.at("name").get<std::string>();
        c.output_layout = face_binding::Layout::Other;
        if (special) {
            const auto sdk_matches = [&](std::size_t i, int dtype) {
                const auto& v = c.expected_sdk_codes[i];
                return v.present && v.dtype == dtype && v.layout == 0 && v.memtype == 0 &&
                       v.colorspace == 0 && c.expected_quantization_values[i] == 0;
            };
            if (c.required_capacity != 64 || c.y_name != "prep_y_plane" || c.uv_name != "prep_uv_plane" ||
                c.output_name != "detections_v1" ||
                c.model_stride_rule !=
                    face_binding::ModelStrideRule::AllowUnconstrainedWithExplicitRuntimeIo ||
                !sdk_matches(0, 1) || !sdk_matches(1, 1) || !sdk_matches(2, 3))
                return {Error::InvalidManifest, nullptr};
        }
        if (c.y_name == c.uv_name || c.y_name == c.output_name || c.uv_name == c.output_name)
            return {Error::InvalidManifest, nullptr};
        const auto& evidence = j.at("evidence");
        std::vector<const char*> roles(std::begin(kRoles), std::end(kRoles));
#ifdef COSMO_AXERA_HS_ROUTE_PRODUCTION
        if (hs && !isolated)
            roles.assign(std::begin(kHsProductionRoles), std::end(kHsProductionRoles));
#endif
#ifdef COSMO_AXERA_MOUSE_ROUTE_PRODUCTION
        if (mouse && !isolated)
            roles.assign(std::begin(kHsProductionRoles), std::end(kHsProductionRoles));
#endif
#ifdef COSMO_AXERA_RUBBISH_ROUTE_PRODUCTION
        if (rubbish && !isolated)
            roles.assign(std::begin(kHsProductionRoles), std::end(kHsProductionRoles));
#endif
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
        if (isolated)
            roles.assign(std::begin(kIsolatedRoles), std::end(kIsolatedRoles));
#endif
        if (!evidence.is_object() || evidence.size() != roles.size())
            return {Error::InvalidEvidence, nullptr};
        std::set<std::string> filenames{manifest_basename, model_name};
        for (const auto* role : roles) {
            const auto& record = evidence.at(role);
            if (!Keys(record, {"file", "sha256"}))
                return {Error::InvalidEvidence, nullptr};
            const auto filename = record.at("file").get<std::string>();
            const auto hash     = record.at("sha256").get<std::string>();
            if (!SafeName(filename) || !Digest(hash) || !filenames.insert(filename).second)
                return {Error::InvalidEvidence, nullptr};
            std::vector<std::uint8_t> bytes;
            Json proof;
            if (!Read(dir.fd, filename, expected_publisher_uid, kEvidenceLimit, bytes) ||
                Sha256(bytes) != hash || !Parse(bytes, proof) ||
                !Keys(proof, {"schema", "role", "status", "compiled_sha256", "source_onnx_sha256",
                              "authority_revision", "artifact_file", "artifact_sha256"}) ||
                proof.at("schema") != (hs        ? "cosmoedge.headshoulder-evidence.v1"
                                       : rubbish ? "cosmoedge.rubbish-evidence.v1"
                                       : mouse   ? "cosmoedge.mouse-evidence.v1"
                                                 : "cosmoedge.person-evidence.v1") ||
                proof.at("role") != role || proof.at("status") != "PASS" ||
                proof.at("compiled_sha256") != c.model_sha256 ||
                proof.at("source_onnx_sha256") != deployment->source_onnx_sha256 ||
                proof.at("authority_revision") != c.authority_revision)
                return {Error::InvalidEvidence, nullptr};
            const auto artifact      = proof.at("artifact_file").get<std::string>();
            const auto artifact_hash = proof.at("artifact_sha256").get<std::string>();
            std::vector<std::uint8_t> artifact_bytes;
#ifdef COSMO_AXERA_HS_ROUTE_PRODUCTION
            if (hs && !isolated && std::string(role) == "device_smoke" &&
                artifact_hash != hs_contract::DeviceSmokeEvidenceSha256())
                return {Error::InvalidEvidence, nullptr};
#endif
#ifdef COSMO_AXERA_MOUSE_ROUTE_PRODUCTION
            if (mouse && !isolated && std::string(role) == "device_smoke" &&
                artifact_hash != mouse_contract::DeviceSmokeEvidenceSha256())
                return {Error::InvalidEvidence, nullptr};
#endif
#ifdef COSMO_AXERA_RUBBISH_ROUTE_PRODUCTION
            if (rubbish && !isolated && std::string(role) == "device_smoke" &&
                artifact_hash != rubbish_contract::DeviceSmokeEvidenceSha256())
                return {Error::InvalidEvidence, nullptr};
#endif
            if (!SafeName(artifact) || !Digest(artifact_hash) || !filenames.insert(artifact).second ||
                !Read(dir.fd, artifact, expected_publisher_uid, kEvidenceArtifactLimit, artifact_bytes) ||
                Sha256(artifact_bytes) != artifact_hash)
                return {Error::InvalidEvidence, nullptr};
            deployment->evidence.push_back({role, hash});
        }
        std::vector<std::uint8_t> model_bytes;
        if (!Read(model_dir.fd, model_name, expected_publisher_uid, kModelLimit, model_bytes))
            return {Error::InvalidModel, nullptr};
        // No path reopening: Binding::Bind must verify this exact byte vector.
        deployment->model_bytes = std::make_shared<const std::vector<std::uint8_t>>(std::move(model_bytes));
        return {Error::None, std::move(deployment)};
    } catch (...) {
        return {Error::InvalidManifest, nullptr};
    }
#endif
}

Result LoadFromDirectory(int pinned_contracts_fd, int pinned_models_root_fd,
                         const std::string& manifest_basename, std::uint32_t expected_publisher_uid,
                         const std::string& expected_service_id) {
    return LoadInternal(pinned_contracts_fd, pinned_models_root_fd, manifest_basename, expected_publisher_uid,
                        expected_service_id, false, {}, false);
}
#if defined(COSMO_AXERA_HS_ROUTE_CANDIDATE) && defined(COSMO_AXERA_HS_ROUTE_PRODUCTION)
Result LoadHsProductionFromDirectory(int pinned_contracts_fd, int pinned_models_root_fd,
                                     const std::string& manifest_basename,
                                     std::uint32_t expected_publisher_uid,
                                     const std::string& expected_service_id) {
    return LoadInternal(pinned_contracts_fd, pinned_models_root_fd, manifest_basename, expected_publisher_uid,
                        expected_service_id, false, {}, true);
}
#endif

#if defined(COSMO_AXERA_MOUSE_ROUTE_CANDIDATE) && defined(COSMO_AXERA_MOUSE_ROUTE_PRODUCTION)
Result LoadMouseProductionFromDirectory(int c, int m, const std::string& name, std::uint32_t uid,
                                        const std::string& service) {
    return LoadInternal(c, m, name, uid, service, false, {}, false, true);
}
#endif
#if defined(COSMO_AXERA_RUBBISH_ROUTE_CANDIDATE) && defined(COSMO_AXERA_RUBBISH_ROUTE_PRODUCTION)
Result LoadRubbishProductionFromDirectory(int c, int m, const std::string& name, std::uint32_t uid,
                                          const std::string& service) {
    return LoadInternal(c, m, name, uid, service, false, {}, false, false, true);
}
#endif
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
std::string ReadLocalSshHostKeySha256() noexcept {
#if defined(__unix__) || defined(__APPLE__)
    try {
        Fd root{::open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW)};
        return root.fd < 0 ? std::string{} : ReadHostKeyFromRoot(root.fd, 0);
    } catch (...) {
        return {};
    }
#else
    return {};
#endif
}
#ifdef COSMO_AXERA_FACE_HOSTKEY_TEST_SEAM
std::string ReadSshHostKeyFromRootForTesting(int root_fd, std::uint32_t publisher_uid) noexcept {
#if defined(__unix__) || defined(__APPLE__)
    try {
        return ReadHostKeyFromRoot(root_fd, static_cast<uid_t>(publisher_uid));
    } catch (...) {
        return {};
    }
#else
    (void)root_fd;
    (void)publisher_uid;
    return {};
#endif
}
#endif
Result LoadIsolatedDeviceTestFromDirectory(int pinned_contracts_fd, int pinned_models_root_fd,
                                           const std::string& manifest_basename,
                                           std::uint32_t expected_publisher_uid,
                                           const std::string& expected_service_id,
                                           const std::string& expected_device_identity) {
    return LoadInternal(pinned_contracts_fd, pinned_models_root_fd, manifest_basename, expected_publisher_uid,
                        expected_service_id, true, expected_device_identity, false);
}
#ifdef COSMO_AXERA_HS_ROUTE_CANDIDATE
Result LoadHsIsolatedDeviceTestFromDirectory(int pinned_contracts_fd, int pinned_models_root_fd,
                                             const std::string& manifest_basename,
                                             std::uint32_t expected_publisher_uid,
                                             const std::string& expected_service_id,
                                             const std::string& expected_device_identity) {
    return LoadInternal(pinned_contracts_fd, pinned_models_root_fd, manifest_basename, expected_publisher_uid,
                        expected_service_id, true, expected_device_identity, true);
}
#endif
#ifdef COSMO_AXERA_MOUSE_ROUTE_CANDIDATE
Result LoadMouseIsolatedDeviceTestFromDirectory(int c, int m, const std::string& name, std::uint32_t uid,
                                                const std::string& service, const std::string& identity) {
    return LoadInternal(c, m, name, uid, service, true, identity, false, true);
}
#endif
#ifdef COSMO_AXERA_RUBBISH_ROUTE_CANDIDATE
Result LoadRubbishIsolatedDeviceTestFromDirectory(int c, int m, const std::string& name, std::uint32_t uid,
                                                  const std::string& service, const std::string& identity) {
    return LoadInternal(c, m, name, uid, service, true, identity, false, false, true);
}
#endif
#endif
}  // namespace cosmo::nn::axera::person_deployment
