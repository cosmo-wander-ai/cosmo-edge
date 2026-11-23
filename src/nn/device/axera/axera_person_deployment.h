#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "nn/device/axera/axera_person_model_binding.h"

namespace cosmo::nn::axera::person_deployment {
// Internal result, not an HTTP/configuration API. The caller must hand the
// returned bytes directly to Binding::Bind, then verify the frame witness and
// task/queue authority before considering execution. A valid manifest is a
// publisher assertion and does not establish reference-device equivalence.
struct EvidenceRecord {
    std::string role;
    std::string sha256;
};
struct Deployment {
    person_binding::Contract contract;
    std::string service_id;
    std::string atomic_code;
    std::string source_onnx_sha256;
    std::string reference_weight_identity;
    float scene_threshold{0.f};
    std::vector<EvidenceRecord> evidence;
    std::shared_ptr<const std::vector<std::uint8_t>> model_bytes;
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
    // Present only in isolated test builds; never a production admission.
    bool isolated_device_test_only{false};
    std::string test_device_identity;
    std::string test_run_id;
#endif
};
enum class Error { None, InvalidAuthority, InvalidManifest, InvalidEvidence, InvalidModel, Unavailable };
struct Result {
    Error error{Error::Unavailable};
    std::unique_ptr<const Deployment> deployment;
};

#if defined(COSMO_AXERA_HS_ROUTE_CANDIDATE) && defined(COSMO_AXERA_HS_ROUTE_PRODUCTION)
// Production Hs is bound to this exact model, graph, device-smoke record,
// publisher-owned evidence and scene. Unverified reference/accuracy limits are explicit.
Result LoadHsProductionFromDirectory(int pinned_contracts_fd, int pinned_models_root_fd,
                                     const std::string& manifest_basename,
                                     std::uint32_t expected_publisher_uid,
                                     const std::string& expected_service_id);
#endif

#if defined(COSMO_AXERA_MOUSE_ROUTE_CANDIDATE) && defined(COSMO_AXERA_MOUSE_ROUTE_PRODUCTION)
Result LoadMouseProductionFromDirectory(int, int, const std::string&, std::uint32_t, const std::string&);
#endif
#if defined(COSMO_AXERA_RUBBISH_ROUTE_CANDIDATE) && defined(COSMO_AXERA_RUBBISH_ROUTE_PRODUCTION)
Result LoadRubbishProductionFromDirectory(int, int, const std::string&, std::uint32_t, const std::string&);
#endif
#if defined(COSMO_AXERA_MOUSE_ROUTE_CANDIDATE) && defined(COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST)
Result LoadMouseIsolatedDeviceTestFromDirectory(int, int, const std::string&, std::uint32_t,
                                                const std::string&, const std::string&);
#endif
#if defined(COSMO_AXERA_RUBBISH_ROUTE_CANDIDATE) && defined(COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST)
Result LoadRubbishIsolatedDeviceTestFromDirectory(int, int, const std::string&, std::uint32_t,
                                                  const std::string&, const std::string&);
#endif

// Both directory fds MUST be supplied by trusted process startup, not from a
// business request. The caller must open fixed roots without symlink traversal.
// This checks publisher ownership and denies group/other writes, resolves a
// manifest-selected single model-directory component using openat/O_NOFOLLOW,
// validates evidence digests and reads model bytes once. Evidence hashes bind
// identity, not the factual content of the publisher's evidence. Never reopen
// a model pathname or use a request-supplied hash as authority.
Result LoadFromDirectory(int pinned_contracts_fd, int pinned_models_root_fd,
                         const std::string& manifest_basename, std::uint32_t expected_publisher_uid,
                         const std::string& expected_service_id);
#ifdef COSMO_AXERA_FACE_ISOLATED_DEVICE_TEST
// SHA-256 hex of the exact bytes in /etc/ssh/ssh_host_ed25519_key.pub.
// Reads only the root-owned local path, with every component non-symlink and
// non-group/other-writable; empty indicates an untrusted or unavailable key.
std::string ReadLocalSshHostKeySha256() noexcept;
#ifdef COSMO_AXERA_FACE_HOSTKEY_TEST_SEAM
// Isolated unit test only: production and device test binaries omit this seam.
std::string ReadSshHostKeyFromRootForTesting(int root_fd, std::uint32_t publisher_uid) noexcept;
#endif
// Test builds only. expected_device_identity must come from trusted local
// hardware-bound device identity, never user input or a mutable request.
// This scope permits only isolated-device validation and is not production
// authorization; an unverified hardware identity must fail before this call.
Result LoadIsolatedDeviceTestFromDirectory(int pinned_contracts_fd, int pinned_models_root_fd,
                                           const std::string& manifest_basename,
                                           std::uint32_t expected_publisher_uid,
                                           const std::string& expected_service_id,
                                           const std::string& expected_device_identity);
#ifdef COSMO_AXERA_HS_ROUTE_CANDIDATE
// Hs has no production admission: the .26 template is not hash-bound to this model.
Result LoadHsIsolatedDeviceTestFromDirectory(int pinned_contracts_fd, int pinned_models_root_fd,
                                             const std::string& manifest_basename,
                                             std::uint32_t expected_publisher_uid,
                                             const std::string& expected_service_id,
                                             const std::string& expected_device_identity);
#endif
#endif
}  // namespace cosmo::nn::axera::person_deployment
