#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/cosmo-ax-packet.XXXXXXXX")
trap 'rm -rf -- "$build_dir"' EXIT HUP INT TERM
cd "$root"
asset="$root/data/test-video/Safety Helmet.mp4"
asset_hash=$(sha256sum "$asset")
case "$asset_hash" in
    ec77182a264f3059a091b68c4973942dba3b80e93f20feaf4d7e146885caf9d2*) ;;
    *) echo 'Test video hash differs from the inspected packet index' >&2; exit 1 ;;
esac
for mode in production mock; do
    flag=
    if [ "$mode" = mock ]; then flag=-DCOSMO_AXERA_PACKET_MOCK; fi
    ${CXX:-c++} -std=c++17 -pthread -Wall -Wextra -Werror $flag -I src \
        test/axera/StrictPacketLeaseTest.cc src/media/AxeraCompressedPacketLease.cc \
        src/nn/device/axera/axera_physical_lease.cc \
        -o "$build_dir/test-$mode"
    "$build_dir/test-$mode" "$asset"
done
echo 'AX packet lease tests passed (heap mock, no SDK/device or VDEC execution)'
