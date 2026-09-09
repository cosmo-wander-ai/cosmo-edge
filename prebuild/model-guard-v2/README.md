# Cosmo Model Guard v2 SDK

This directory exposes the public, consumer-facing portion of the formally
built Model Guard v2 SDK used by CosmoEdge:

- `include/cosmo_model_guard_v2.h`
- `lib/libcosmo_model_guard.so*`

The checked-in AArch64 shared library has the `v2-only` runtime compatibility
profile. It does not expose the legacy Model Guard ABI.

The default CosmoEdge build profile remains `public-runtime` for automation
compatibility; its user-facing artifact is the SOURCE package. The public SDK
and SOURCE package contain the runtime library and public header, but no
`bin/cosmo-model-provision` or private signing material. A configured device
needs only
`/data/cwaiuserdata/model-guard/device-certificate.bin` to authorize all
current and future preset models published under the product model key. There
are no per-model licenses. SOURCE cannot commission a blank device or construct
or sign a formal production release.

`bin/cosmo-model-provision` is an offline device-initialization tool and is not
part of the public runtime SDK. It remains ignored by Git and must not be
force-added. Selecting `production-release` does not create or recover any
signing key.

This public repository does not contain the private Model Guard source,
production signing keys, device secrets, or the complete controlled inputs
required to reconstruct, sign, or deploy a production package. This README
does not grant or alter artifact licensing or redistribution rights; those
require separately approved terms from the artifact owner.

## Distribution approval record

The separately approved CosmoEdge distribution record is public
[Issue #59](https://github.com/cosmo-wander-ai/cosmo-edge/issues/59), which
states that CosmoEdge may distribute the approved Model Guard runtime library
and public header. The implementation containing this exact runtime was then
approved and merged through
[PR #101](https://github.com/cosmo-wander-ai/cosmo-edge/pull/101).

The approved `libcosmo_model_guard.so.2.0.0` SHA-256 is
`74ff8b456548e615882e5c9ee6dd18a51a2caf8124d761d7243dad014310042c`.
This project-specific approval record is not a general relicensing of the
runtime, its private implementation, provisioning inputs, or signing material.

## Verification

The canonical public Sophon build invokes
`scripts/verify_model_guard_v2_sdk.py` for the checked-in runtime SDK:

```bash
/usr/bin/python3 -I -B scripts/verify_model_guard_v2_sdk.py \
  --admission-profile public-runtime \
  --sdk-root "$MODEL_GUARD_SDK_ROOT" \
  --readelf "$AARCH64_READELF" \
  --nm "$AARCH64_NM"
```
