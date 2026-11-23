#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/cosmo-ax-replay.XXXXXXXX")
trap 'rm -rf -- "$build_dir"' EXIT HUP INT TERM
cd "$root"
asset="$root/data/test-video/Safety Helmet.mp4"
asset_hash=$(sha256sum "$asset")
case "$asset_hash" in
    ec77182a264f3059a091b68c4973942dba3b80e93f20feaf4d7e146885caf9d2*) ;;
    *) echo 'Test video hash differs from inspected reference' >&2; exit 1 ;;
esac
# Offline ffprobe parses/copies data to make an index. It is not a production
# zero-copy demuxer and is never invoked by the packet lease implementation.
ffprobe -v error -select_streams v:0 -show_entries packet=pts,dts,size,pos,flags \
    -of csv=p=0 "$asset" > "$build_dir/packets.csv"
${CXX:-c++} -std=c++17 -O2 -pthread -Wall -Wextra -Werror -DCOSMO_AXERA_PACKET_MOCK -I src \
    test/axera/StrictPacketReplayTest.cc src/media/AxeraCompressedPacketLease.cc \
    src/nn/device/axera/axera_physical_lease.cc -o "$build_dir/replay"
"$build_dir/replay" "$asset" "$build_dir/packets.csv"
echo 'All 360 indexed MP4 packets replayed via host-mock pread (no hardware or zero-copy claim)'
