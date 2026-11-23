#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
sdk_root="${COSMO_AXERA_ROOT:?Set COSMO_AXERA_ROOT to a private AX SDK root containing include/}"
compiler="${CXX:-c++}"
scratch_dir="$(mktemp -d "${TMPDIR:-/tmp}/cosmo-axera-media.XXXXXX")"
trap 'rm -rf "$scratch_dir"' EXIT

case "$(uname -s):$(uname -m)" in
    Linux:x86_64) arch=x86_64 ;;
    Linux:aarch64) arch=aarch64 ;;
    *) echo 'The AX VDEC mock uses the repository Linux FFmpeg bundle.' >&2; exit 1 ;;
esac
ffmpeg_root="$repo_root/prebuild/ffmpeg/$arch"

"$compiler" -std=c++17 -pthread -DFMT_HEADER_ONLY \
    -I"$repo_root/src" -I"$repo_root/test" -I"$repo_root/3rd/fmt-7.1.2/include" \
    -I"$sdk_root/include" -I"$ffmpeg_root/include" \
    "$repo_root/test/catch_amalgamated.cpp" "$repo_root/test/axera/test_axera_vdec_mock.cc" \
    "$repo_root/src/media/VideoDecoder.cc" "$repo_root/src/media/VideoDecoderAxera.cc" \
    "$repo_root/src/media/H264AnnexBColorParser.cc" "$repo_root/src/media/VideoPacketColorWitness.cc" \
    -L"$ffmpeg_root/lib" -lavutil -o "$scratch_dir/vdec"
LD_LIBRARY_PATH="$ffmpeg_root/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    "$scratch_dir/vdec" '[media][axera][mock]'
