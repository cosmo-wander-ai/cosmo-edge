#!/bin/sh
set -e

pcre_src="$1"
pcre_dst="$2"
zlib_src="$3"
zlib_dst="$4"
openssl_src="$5"
openssl_dst="$6"
openssl_cleaner="$7"

if [ -z "${openssl_cleaner}" ]; then
    echo "Usage: $0 <pcre-src> <pcre-dst> <zlib-src> <zlib-dst> <openssl-src> <openssl-dst> <openssl-cleaner>" >&2
    exit 1
fi

rm -rf "${pcre_dst}" "${zlib_dst}" "${openssl_dst}"
cp -R "${pcre_src}" "${pcre_dst}"
cp -R "${zlib_src}" "${zlib_dst}"
cp -R "${openssl_src}" "${openssl_dst}"

if [ ! -f "${pcre_dst}/configure" ]; then
    (cd "${pcre_dst}" && sh ./autogen.sh)
fi

sh "${openssl_cleaner}" "${openssl_dst}"

if [ -f "${zlib_dst}/Makefile" ]; then
    make -C "${zlib_dst}" distclean || true
fi
if [ ! -f "${zlib_dst}/Makefile" ] && [ -f "${zlib_dst}/Makefile.in" ]; then
    {
        printf 'all:\n\t-@echo "Please use ./configure first.  Thank you."\n'
        printf '\ndistclean:\n\t$(MAKE) -f Makefile.in distclean\n'
    } > "${zlib_dst}/Makefile"
fi
