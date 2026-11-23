#!/bin/sh
set -eu
# Linux/x86-only entry test, using the repository FFmpeg bundle and no camera.
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/cosmo-axera-ingress.XXXXXXXX")
trap 'rm -rf -- "$build_dir"' EXIT HUP INT TERM
cd "$root"
${CXX:-c++} -std=c++17 -pthread -ffunction-sections -fdata-sections \
    -Wl,--gc-sections -Wl,--wrap=avformat_open_input -Wno-deprecated-declarations \
    -DCOSMO_NN_USE_AXERA_BACKEND -DFMT_HEADER_ONLY -I src -I 3rd/include \
    -I 3rd/fmt-7.1.2/include -I prebuild/ffmpeg/x86_64/include \
    test/axera/StrictIngressTest.cc src/media/VideoDemuxer.cc src/media/VideoDemuxerStream.cc \
    src/media/FileDemuxStrategy.cc src/media/NetworkDemuxStrategy.cc \
    src/media/RtspDemuxStrategy.cc src/media/UsbDemuxStrategy.cc src/util/RtspUrlUtil.cc \
    -L prebuild/ffmpeg/x86_64/lib -lavdevice -lavformat -lavcodec -lavutil -lopenh264 \
    -o "$build_dir/strict-ingress-test"
LD_LIBRARY_PATH="$root/prebuild/ffmpeg/x86_64/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
    "$build_dir/strict-ingress-test"
echo 'AX strict ingress entry test passed (no network or hardware access)'
