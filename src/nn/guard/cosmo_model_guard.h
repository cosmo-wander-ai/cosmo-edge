/// @file cosmo_model_guard.h
/// @brief Public interface for the model guard library.

#pragma once
#include <cstddef>
#include <cstdint>

#ifdef COSMO_NN_USE_SOPHON_BACKEND
#include "bmruntime_interface.h"
#endif

#ifndef GUARD_EXPORT
#define GUARD_EXPORT __attribute__((visibility("default")))
#endif

namespace cosmo::guard {

/// Magic number identifying an encrypted model file.
static constexpr uint32_t kEncryptedMagic = 0xEC010001;

/// Check whether the file-header magic indicates an encrypted model.
inline bool IsEncryptedModel(uint32_t magic) {
    return magic == kEncryptedMagic;
}

#ifdef COSMO_NN_USE_SOPHON_BACKEND

/// Error codes returned by guard functions.
enum class GuardError : int {
    kSuccess           = 0,
    kInvalidMagic      = -1,
    kDecryptFailed     = -2,
    kLoadFailed        = -3,
    kHandleNull        = -4,
    kSegmentOutOfRange = -5,
    kFileOpenFailed    = -6,
    kGuardNotAvailable = -10,
};

/// Decrypt and load a single segment from an encrypted model file.
///
/// @param enc_file_path   Path to the encrypted model file.
/// @param bm_handle       Sophon device handle.
/// @param segment_index   0-based segment index.
/// @param out_bmrt        [out] Created bmrt handle; caller manages lifetime via bmrt_destroy.
/// @return 0 on success, negative GuardError code on failure.
GUARD_EXPORT int DecryptAndLoadSegmentFromFile(const char* enc_file_path, bm_handle_t bm_handle,
                                               uint32_t segment_index, void** out_bmrt);

/// Query the number of encrypted segments in a model file.
///
/// @param enc_file_path   Path to the encrypted model file.
/// @return Segment count (>0) on success, negative GuardError code on failure.
GUARD_EXPORT int GetEncryptedSegmentCountFromFile(const char* enc_file_path);

#endif  // COSMO_NN_USE_SOPHON_BACKEND

}  // namespace cosmo::guard
