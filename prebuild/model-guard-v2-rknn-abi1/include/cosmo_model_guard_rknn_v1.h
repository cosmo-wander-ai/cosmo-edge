#ifndef COSMO_MODEL_GUARD_RKNN_V1_H_
#define COSMO_MODEL_GUARD_RKNN_V1_H_

#include <stddef.h>
#include <stdint.h>

#ifndef CMG_RKNN_V1_API
#if defined(__GNUC__) || defined(__clang__)
#define CMG_RKNN_V1_API __attribute__((visibility("default")))
#else
#define CMG_RKNN_V1_API
#endif
#endif

#define CMG_RKNN_V1_ABI_MAJOR UINT32_C(1)

typedef struct CmgRknnV1Artifact CmgRknnV1Artifact;
typedef int32_t CmgRknnV1Status;

#define CMG_RKNN_V1_OK ((CmgRknnV1Status)0)
#define CMG_RKNN_V1_FORMAT_INVALID ((CmgRknnV1Status) - 1001)
#define CMG_RKNN_V1_FORMAT_UNSUPPORTED ((CmgRknnV1Status) - 1002)
#define CMG_RKNN_V1_FORMAT_SOURCE_MISMATCH ((CmgRknnV1Status) - 1003)
#define CMG_RKNN_V1_FORMAT_LIMIT ((CmgRknnV1Status) - 1004)
#define CMG_RKNN_V1_CERTIFICATE_UNAVAILABLE ((CmgRknnV1Status) - 2001)
#define CMG_RKNN_V1_CERTIFICATE_REJECTED ((CmgRknnV1Status) - 2002)
#define CMG_RKNN_V1_CRYPTO_FAILED ((CmgRknnV1Status) - 3001)
#define CMG_RKNN_V1_RESOURCE_INVALID_ARGUMENT ((CmgRknnV1Status) - 4001)
#define CMG_RKNN_V1_RESOURCE_INVALID_STATE ((CmgRknnV1Status) - 4002)
#define CMG_RKNN_V1_RESOURCE_IO ((CmgRknnV1Status) - 4003)
#define CMG_RKNN_V1_RESOURCE_NO_MEMORY ((CmgRknnV1Status) - 4004)
#define CMG_RKNN_V1_RESOURCE_BUSY ((CmgRknnV1Status) - 4005)
#define CMG_RKNN_V1_RESOURCE_INTERNAL ((CmgRknnV1Status) - 4006)
#define CMG_RKNN_V1_RESOURCE_ABI_MISMATCH ((CmgRknnV1Status) - 4007)
#define CMG_RKNN_V1_BACKEND_FAILED ((CmgRknnV1Status) - 5001)

#define CMG_RKNN_V1_SOURCE_RAW_RKNN UINT32_C(3)
#define CMG_RKNN_V1_DEVICE_CERTIFICATE_SIZE UINT32_C(236)
#define CMG_RKNN_V1_OPEN_OPTIONS_SIZE UINT32_C(32)
#define CMG_RKNN_V1_ARTIFACT_INFO_SIZE UINT32_C(72)

typedef struct CmgRknnV1OpenOptions {
  uint32_t struct_size;
  uint32_t reserved;
  const char *installed_model_path;
  const uint8_t *device_certificate;
  uint64_t device_certificate_size;
} CmgRknnV1OpenOptions;

typedef struct CmgRknnV1ArtifactInfo {
  uint32_t struct_size;
  uint32_t source_format;
  uint32_t segment_count;
  uint32_t reserved;
  uint8_t artifact_id[16];
  uint64_t generation;
  uint8_t model_identity_sha256[32];
} CmgRknnV1ArtifactInfo;

#ifdef __cplusplus
extern "C" {
#endif

CMG_RKNN_V1_API CmgRknnV1Status CmgRknnV1OpenArtifact(
    const CmgRknnV1OpenOptions *options, CmgRknnV1Artifact **out_artifact);
CMG_RKNN_V1_API CmgRknnV1Status CmgRknnV1GetArtifactInfo(
    const CmgRknnV1Artifact *artifact, CmgRknnV1ArtifactInfo *out_info);
/* On success ownership of the RKNN context transfers to the caller, which
 * destroys it with rknn_destroy(). No plaintext model bytes cross this ABI. */
CMG_RKNN_V1_API CmgRknnV1Status
CmgRknnV1LoadSegment(CmgRknnV1Artifact *artifact, uint32_t segment_index,
                     uint64_t *out_rknn_context);
CMG_RKNN_V1_API void CmgRknnV1CloseArtifact(CmgRknnV1Artifact *artifact);

#ifdef __cplusplus
}
#endif

#if defined(__cplusplus)
static_assert(sizeof(CmgRknnV1OpenOptions) == CMG_RKNN_V1_OPEN_OPTIONS_SIZE,
              "CmgRknnV1OpenOptions ABI size mismatch");
static_assert(alignof(CmgRknnV1OpenOptions) == 8,
              "CmgRknnV1OpenOptions ABI alignment mismatch");
static_assert(sizeof(CmgRknnV1ArtifactInfo) == CMG_RKNN_V1_ARTIFACT_INFO_SIZE,
              "CmgRknnV1ArtifactInfo ABI size mismatch");
static_assert(alignof(CmgRknnV1ArtifactInfo) == 8,
              "CmgRknnV1ArtifactInfo ABI alignment mismatch");
#else
_Static_assert(sizeof(CmgRknnV1OpenOptions) == CMG_RKNN_V1_OPEN_OPTIONS_SIZE,
               "CmgRknnV1OpenOptions ABI size mismatch");
_Static_assert(_Alignof(CmgRknnV1OpenOptions) == 8,
               "CmgRknnV1OpenOptions ABI alignment mismatch");
_Static_assert(sizeof(CmgRknnV1ArtifactInfo) == CMG_RKNN_V1_ARTIFACT_INFO_SIZE,
               "CmgRknnV1ArtifactInfo ABI size mismatch");
_Static_assert(_Alignof(CmgRknnV1ArtifactInfo) == 8,
               "CmgRknnV1ArtifactInfo ABI alignment mismatch");
#endif

#endif
