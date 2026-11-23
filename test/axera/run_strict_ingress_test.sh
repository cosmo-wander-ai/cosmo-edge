#!/bin/sh
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/cosmo-axera-ingress.XXXXXXXX")
trap 'rm -rf -- "$build_dir"' EXIT HUP INT TERM
cd "$root"
${CXX:-c++} -std=c++17 -I src -I prebuild/ffmpeg/x86_64/include \
    test/axera/StrictIngressTest.cc -o "$build_dir/strict-ingress-test"
"$build_dir/strict-ingress-test"
echo 'AX native demux metadata and frame rate admission tests passed'
