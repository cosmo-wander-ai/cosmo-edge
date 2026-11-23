#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
sdk_root="${COSMO_AXERA_ROOT:?Set COSMO_AXERA_ROOT to a private AX SDK root containing include/}"
compiler="${CXX:-c++}"
scratch_dir="$(mktemp -d "${TMPDIR:-/tmp}/cosmo-axera-media.XXXXXX")"
trap 'rm -rf "$scratch_dir"' EXIT

common_args=(-std=c++17 -DFMT_HEADER_ONLY -I"$repo_root/src" -I"$repo_root/test"
    -I"$repo_root/3rd/fmt-7.1.2/include" -I"$sdk_root/include")

"$compiler" "${common_args[@]}" "$repo_root/test/catch_amalgamated.cpp" \
    "$repo_root/test/axera/test_axera_vdec_mock.cc" "$repo_root/src/media/VideoDecoderAxera.cc" \
    -o "$scratch_dir/vdec"
"$scratch_dir/vdec" '[media][axera][mock]'

"$compiler" "${common_args[@]}" "$repo_root/test/catch_amalgamated.cpp" \
    "$repo_root/test/axera/test_axera_venc_mock.cc" "$repo_root/src/media/VideoEncoderAxera.cc" \
    -o "$scratch_dir/venc"
"$scratch_dir/venc" '[media][axera][mock]'

"$compiler" "${common_args[@]}" "$repo_root/test/catch_amalgamated.cpp" \
    "$repo_root/test/test_axera_video_frame_copy.cc" "$repo_root/test/test_axera_encoder_pending.cc" \
    -o "$scratch_dir/frames"
"$scratch_dir/frames" '[media][axera]'
