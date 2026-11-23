#!/bin/sh
set -eu

# Only SDK declarations are used. Every SDK runtime entry point is mocked.
: "${AXERA_SDK_INCLUDE:?Set AXERA_SDK_INCLUDE to the matching private AX SDK include directory}"
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/cosmo-axera-mock.XXXXXXXX")
trap 'rm -rf -- "$build_dir"' EXIT HUP INT TERM
compiler=${CXX:-c++}

case $(uname -s) in
    Darwin) dead_strip=-Wl,-dead_strip ;;
    Linux) dead_strip=-Wl,--gc-sections ;;
    *) echo "unsupported test host" >&2; exit 1 ;;
esac

cd "$root"
"$compiler" -std=c++17 -pthread -I test/axera/fake-sdk -I src \
    test/axera/AxeraSysLeaseTest.cc test/axera/AxeraSysLeasePeer.cc -o "$build_dir/sys-lease-test"
"$build_dir/sys-lease-test"

"$compiler" -std=c++17 -pthread -ffunction-sections -fdata-sections "$dead_strip" \
    -DFMT_HEADER_ONLY -DCOSMO_NN_USE_AXERA_BACKEND -I src -I "$AXERA_SDK_INCLUDE" \
    -I 3rd/include -I 3rd/fmt-7.1.2/include \
    test/axera/AxeraNetNodeTest.cc src/infer/BmodelTool.cc src/nn/device/axera/axera_net_node.cc \
    src/nn/core/blob.cc src/nn/core/blob_impl.cc src/nn/core/abstract_device.cc \
    src/nn/core/status.cc src/nn/core/inference_pipeline_metrics.cc src/nn/node/node.cc \
    src/nn/node/net_node.cc src/nn/utils/timer.cc -o "$build_dir/net-node-test"
"$build_dir/net-node-test" "$build_dir"

"$compiler" -std=c++17 -DCOSMO_NN_USE_AXERA_BACKEND -I src -I "$AXERA_SDK_INCLUDE" \
    test/axera/AxeraE24MetadataTest.cc -o "$build_dir/e24-metadata-test"
"$build_dir/e24-metadata-test"

echo 'AX SYS lifetime, SDK model import/inference, and E24 tensor metadata mock tests passed'
