#include "nn/device/axera/axera_vehicle_deployment.h"

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

#if defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace cosmo::nn::axera::vehicle_deployment {
namespace {
    using Json                      = nlohmann::json;
    constexpr const char* kSemantic = "ax650-nv12-1080p-pitch2048-bt709-limited-vehicle640-pad140-k64-nms-v1";
    constexpr std::size_t kManifestLimit = 128 * 1024, kEvidenceLimit = 1024 * 1024;
    constexpr std::size_t kEvidenceArtifactLimit = 64 * 1024 * 1024;
    constexpr std::size_t kModelLimit            = 1024ULL * 1024 * 1024;
    constexpr const char* kRoles[]               = {
        "source_graph",         "toolchain", "calibration",   "compile_log", "vehicle_sample",
        "other_classes_sample", "bframes",   "color_witness", "capacity",    "numerical_evaluation"};
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

[[maybe_unused]] static Result LoadInternal(int pinned_contracts_fd, int pinned_models_root_fd,
                                            const std::string& manifest_basename,
                                            std::uint32_t expected_publisher_uid,
                                            const std::string& expected_service_id, bool isolated,
                                            const std::string& expected_device_identity) {
    (void)expected_device_identity;
#if !defined(__unix__) && !defined(__APPLE__)
    (void)pinned_contracts_fd;
    (void)pinned_models_root_fd;
    (void)manifest_basename;
    (void)expected_publisher_uid;
    (void)expected_service_id;
    (void)isolated;
    (void)expected_device_identity;
    return {Error::Unavailable, nullptr};
#else
    try {
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
                          "classes", "model", "runtime", "evidence", "reference_weight_identity",
                          "test_scope", "not_proven"}) ||
                j.at("schema") != "cosmoedge.vehicle-isolated-test.v1" ||
                j.at("status") != "ISOLATED_DEVICE_TEST_ONLY" ||
                !Keys(j.at("test_scope"), {"device_identity", "run_id"}) ||
                j.at("test_scope").at("device_identity") != expected_device_identity ||
                !SmallString(j.at("test_scope").at("run_id").get<std::string>()))
                return {Error::InvalidManifest, nullptr};
            const auto limitations = j.at("not_proven").get<std::vector<std::string>>();
            const std::set<std::string> required{
                "color_witness",        "capacity", "reference_identity",  "full_pipeline", "vehicle_sample",
                "other_classes_sample", "bframes",  "numerical_evaluation"};
            if (limitations.size() != required.size() ||
                std::set<std::string>(limitations.begin(), limitations.end()) != required)
                return {Error::InvalidManifest, nullptr};
#else
            return {Error::InvalidManifest, nullptr};
#endif
        } else if (!Keys(j,
                         {"schema", "status", "service_id", "authority_revision", "atomic_code", "scene_id",
                          "classes", "model", "runtime", "evidence", "reference_weight_identity"}) ||
                   j.at("schema") != "cosmoedge.vehicle-deployment.v1" || j.at("status") != "DEPLOYABLE") {
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
        const auto& classes                     = j.at("classes");
        if (!classes.is_array() || classes.size() != 3)
            return {Error::InvalidManifest, nullptr};
        for (std::size_t i = 0; i < 3; ++i) {
            if (!Keys(classes[i], {"label", "threshold", "enabled"}) || !classes[i].at("label").is_string() ||
                !classes[i].at("threshold").is_number() || !classes[i].at("enabled").is_boolean())
                return {Error::InvalidManifest, nullptr};
            deployment->contract.classes[i] = {classes[i].at("label").get<std::string>(),
                                               classes[i].at("threshold").get<float>(),
                                               classes[i].at("enabled").get<bool>()};
        }
        deployment->contract.atomic_code = deployment->atomic_code;
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
        if (isolated) {
            deployment->isolated_device_test_only = true;
            deployment->test_device_identity      = expected_device_identity;
            deployment->test_run_id               = j.at("test_scope").at("run_id").get<std::string>();
        }
#endif
        if (deployment->atomic_code != "2000001" || !SafeName(deployment->atomic_code) ||
            !SafeName(deployment->contract.scene_id) ||
            manifest_basename != deployment->atomic_code + "-" + deployment->contract.scene_id + ".json" ||
            !SmallString(deployment->contract.authority_revision) ||
            !SmallString(deployment->contract.scene_id) ||
            deployment->reference_weight_identity != "unverified")
            return {Error::InvalidManifest, nullptr};
        const auto& model = j.at("model");
        if (!Keys(model, {"directory", "file", "sha256", "source_onnx_sha256"}))
            return {Error::InvalidManifest, nullptr};
        const auto model_directory        = model.at("directory").get<std::string>();
        const auto model_name             = model.at("file").get<std::string>();
        deployment->contract.model_sha256 = model.at("sha256").get<std::string>();
        deployment->source_onnx_sha256    = model.at("source_onnx_sha256").get<std::string>();
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
            runtime.at("semantic_contract") != kSemantic || runtime.at("source_width") != 1920 ||
            runtime.at("source_height") != 1080 || runtime.at("plane_pitch_bytes") != 2048 ||
            runtime.at("source_format") != "NV12" || runtime.at("color_matrix") != "BT709" ||
            runtime.at("color_range") != "LIMITED" || runtime.at("capacity") != 64 ||
            runtime.at("max_batch_size") != 1 || runtime.at("dynamic_batch_size") != false)
            return {Error::InvalidManifest, nullptr};
        auto& c                       = deployment->contract;
        c.semantic_contract           = kSemantic;
        c.result_capacity             = runtime.at("capacity").get<std::size_t>();
        c.required_capacity           = runtime.at("required_capacity").get<std::size_t>();
        c.graph_confidence            = runtime.at("confidence").get<float>();
        c.graph_iou                   = runtime.at("iou").get<float>();
        c.expected_max_batch_size     = 1;
        c.expected_dynamic_batch_size = false;
        if (!c.required_capacity || c.required_capacity > c.result_capacity || c.graph_confidence != .1f ||
            c.graph_iou != .35f || !std::isfinite(c.graph_confidence) || c.graph_confidence <= 0.f ||
            c.graph_confidence > 1.f || !std::isfinite(c.graph_iou) || c.graph_iou < 0.f ||
            c.graph_iou >= 1.f || c.classes[0].label != "Person" || c.classes[1].label != "Nonvehicle" ||
            c.classes[2].label != "Vehicle" ||
            !(c.classes[0].enabled || c.classes[1].enabled || c.classes[2].enabled) ||
            !std::all_of(c.classes.begin(), c.classes.end(), [&](const auto& policy) {
                return std::isfinite(policy.threshold) && policy.threshold >= c.graph_confidence &&
                       policy.threshold <= 1.f;
            }))
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
        // The vehicle graph requires K64. Derive the output
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
        if (c.y_name == c.uv_name || c.y_name == c.output_name || c.uv_name == c.output_name)
            return {Error::InvalidManifest, nullptr};
        const auto& evidence = j.at("evidence");
        std::vector<const char*> roles(std::begin(kRoles), std::end(kRoles));
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
                proof.at("schema") != "cosmoedge.vehicle-evidence.v1" || proof.at("role") != role ||
                proof.at("status") != "PASS" || proof.at("compiled_sha256") != c.model_sha256 ||
                proof.at("source_onnx_sha256") != deployment->source_onnx_sha256 ||
                proof.at("authority_revision") != c.authority_revision)
                return {Error::InvalidEvidence, nullptr};
            const auto artifact      = proof.at("artifact_file").get<std::string>();
            const auto artifact_hash = proof.at("artifact_sha256").get<std::string>();
            std::vector<std::uint8_t> artifact_bytes;
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
#ifndef COSMO_AXERA_VEHICLE_ROUTE_CANDIDATE
    (void)pinned_contracts_fd;
    (void)pinned_models_root_fd;
    (void)manifest_basename;
    (void)expected_publisher_uid;
    (void)expected_service_id;
    return {Error::Unavailable, nullptr};
#else
    return LoadInternal(pinned_contracts_fd, pinned_models_root_fd, manifest_basename, expected_publisher_uid,
                        expected_service_id, false, {});
#endif
}
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
#ifndef COSMO_AXERA_VEHICLE_ROUTE_CANDIDATE
    (void)pinned_contracts_fd;
    (void)pinned_models_root_fd;
    (void)manifest_basename;
    (void)expected_publisher_uid;
    (void)expected_service_id;
    (void)expected_device_identity;
    return {Error::Unavailable, nullptr};
#else
    return LoadInternal(pinned_contracts_fd, pinned_models_root_fd, manifest_basename, expected_publisher_uid,
                        expected_service_id, true, expected_device_identity);
#endif
}
#endif
}  // namespace cosmo::nn::axera::vehicle_deployment
