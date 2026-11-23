#!/bin/sh
set -eu

# Use real target SDK headers only; all runtime entry points are mocked.
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
"$compiler" -std=c++17 -pthread -fvisibility=hidden -fvisibility-inlines-hidden \
    -I test/axera/fake-sdk -I src test/axera/AxeraSysLeaseTest.cc \
    test/axera/AxeraSysLeasePeer.cc -o "$build_dir/sys-lease-test"
"$build_dir/sys-lease-test"

for gate in production mock; do
  flag=
  if [ "$gate" = mock ]; then flag=-DCOSMO_AXERA_MOCK_VERIFIED_CONTRACT; fi
"$compiler" $flag -std=c++17 -pthread -ffunction-sections -fdata-sections "$dead_strip" \
    -DCOSMO_NN_USE_AXERA_BACKEND -I src -I "$AXERA_SDK_INCLUDE" \
    test/axera/AxeraNetNodeTest.cc src/nn/device/axera/axera_net_node.cc \
    src/nn/device/axera/axera_physical_lease.cc src/media/AxeraCompressedPacketLease.cc \
    src/nn/core/blob.cc src/nn/core/blob_impl.cc src/nn/core/abstract_device.cc \
    src/nn/core/status.cc src/nn/core/inference_pipeline_metrics.cc src/nn/node/node.cc \
    src/nn/node/net_node.cc src/nn/utils/timer.cc -o "$build_dir/net-node-test"
"$build_dir/net-node-test"
if [ "$gate" = mock ]; then AX_TEST_ENGINE_THROW=1 "$build_dir/net-node-test"; fi
done
# The AX import path must fail closed and not link the SDK metadata API.
"$compiler" -std=c++17 -ffunction-sections -fdata-sections "$dead_strip" \
    -DFMT_HEADER_ONLY -DCOSMO_NN_USE_AXERA_BACKEND -I src -I 3rd/include \
    -I 3rd/fmt-7.1.2/include test/axera/AxeraModelImportGateTest.cc \
    src/infer/BmodelTool.cc -o "$build_dir/model-import-gate-test"
"$build_dir/model-import-gate-test"
# Exercise the real IVPS wrapper and NN node with mocked SDK calls, including
# bidirectional conflict while the other component remains blocked in flight.
"$compiler" -std=c++17 -pthread -ffunction-sections -fdata-sections "$dead_strip" \
    -DFMT_HEADER_ONLY -DCOSMO_NN_USE_AXERA_BACKEND -DCOSMO_AXERA_MOCK_VERIFIED_CONTRACT \
    -I src -I test -I 3rd/fmt-7.1.2/include -I "$AXERA_SDK_INCLUDE" \
    test/axera/AxeraIvpsNetIntegrationTest.cc src/media/AxeraIvpsPreprocess.cc src/media/VideoDecoderAxera.cc \
    src/nn/device/axera/axera_net_node.cc src/nn/device/axera/axera_physical_lease.cc \
    src/media/AxeraCompressedPacketLease.cc src/nn/core/blob.cc src/nn/core/blob_impl.cc \
    src/nn/core/abstract_device.cc src/nn/core/status.cc src/nn/core/inference_pipeline_metrics.cc \
    src/nn/node/node.cc src/nn/node/net_node.cc src/nn/utils/timer.cc -o "$build_dir/ivps-net-test"
"$build_dir/ivps-net-test"
"$compiler" -std=c++17 -pthread -DFMT_HEADER_ONLY -I src -I test -I 3rd/fmt-7.1.2/include \
    -I "$AXERA_SDK_INCLUDE" test/axera/AxeraIvpsContractTest.cc \
    src/media/AxeraIvpsPreprocess.cc src/media/VideoDecoderAxera.cc \
    src/nn/device/axera/axera_physical_lease.cc src/media/AxeraCompressedPacketLease.cc \
    -o "$build_dir/ivps-contract-test"
"$build_dir/ivps-contract-test"
"$compiler" -std=c++17 -pthread -ffunction-sections -fdata-sections "$dead_strip" \
    -DCOSMO_NN_USE_AXERA_BACKEND -I src test/axera/StrictGraphEntryTest.cc \
    src/nn/core/graph.cc src/nn/core/blob.cc src/nn/core/blob_impl.cc \
    src/nn/core/blob_store.cc src/nn/core/shared_resource.cc src/nn/core/abstract_device.cc \
    src/nn/core/status.cc src/nn/node/node.cc src/nn/utils/timer.cc -o "$build_dir/strict-graph-test"
"$build_dir/strict-graph-test"
"$compiler" -std=c++17 -pthread -ffunction-sections -fdata-sections "$dead_strip" \
    -DCOSMO_NN_USE_AXERA_BACKEND -DCOSMO_NN_USE_HOST_BACKEND -DFMT_HEADER_ONLY \
    -I src -I 3rd/include -I 3rd/fmt-7.1.2/include test/axera/StrictNativeBlobTest.cc \
    src/infer/AiComponment.cc src/nn/core/blob.cc src/nn/core/blob_impl.cc \
    src/nn/core/abstract_device.cc src/nn/core/status.cc -o "$build_dir/strict-blob-test"
"$build_dir/strict-blob-test"
"$compiler" -std=c++17 -I src test/axera/StrictModePolicyTest.cc -o "$build_dir/policy-other"
"$build_dir/policy-other"
"$compiler" -std=c++17 -DCOSMO_NN_USE_AXERA_BACKEND -I src test/axera/StrictModePolicyTest.cc \
    -o "$build_dir/policy-axera"
"$build_dir/policy-axera"
"$compiler" -std=c++17 -pthread -Wall -Wextra -Werror -I src \
    test/axera/PhysicalLeaseReentryTest.cc src/nn/device/axera/axera_physical_lease.cc \
    -o "$build_dir/physical-lease-reentry"
"$build_dir/physical-lease-reentry"
"$compiler" -std=c++17 -pthread -Wall -Wextra -Werror -I src \
    test/axera/PhysicalLeaseConcurrencyTest.cc src/nn/device/axera/axera_physical_lease.cc \
    -o "$build_dir/physical-lease-concurrency"
"$build_dir/physical-lease-concurrency"
echo 'AXERA mock-SDK and strict graph tests passed (hardware execution not tested)'
