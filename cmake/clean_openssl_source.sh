#!/bin/sh
set -e

src_dir="$1"

if [ -z "${src_dir}" ] || [ ! -d "${src_dir}" ]; then
    echo "Usage: $0 <openssl-source-dir>" >&2
    exit 1
fi

if [ -f "${src_dir}/Makefile" ]; then
    make -C "${src_dir}" distclean
fi

rm -f "${src_dir}/Makefile" \
      "${src_dir}/configdata.pm" \
      "${src_dir}/configdata.pm.new" \
      "${src_dir}/config.log" \
      "${src_dir}/makefile.one"
