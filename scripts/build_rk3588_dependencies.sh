#!/usr/bin/env bash
# Executed only inside Dockerfile.rockchip-rk3588. No host or device installation.
set -euo pipefail

builder_lock=/opt/cosmo/rockchip-builder-lock.json
if [ "${1:-}" = "--install-cmake" ]; then
    python3 - "$builder_lock" "${CMAKE_WHEEL_SOURCE:-pypi}" <<'PY'
import hashlib, json, pathlib, subprocess, sys

lock = json.loads(pathlib.Path(sys.argv[1]).read_text())['sources']
wheel = lock['cmake_wheel']
source = sys.argv[2]
if source not in wheel['sources']:
    raise SystemExit('CMAKE_WHEEL_SOURCE must be pypi or tuna')
root = pathlib.Path('/opt/cosmo')
(root/'sources').mkdir(parents=True, exist_ok=True)
(root/'evidence').mkdir(parents=True, exist_ok=True)
path = root/'sources'/wheel['filename']
url = wheel['sources'][source]
subprocess.run(['curl', '--fail', '--location', '--retry', '3', '--connect-timeout', '30',
                '--max-time', '600', url, '-o', str(path)], check=True)
actual_sha = hashlib.sha256(path.read_bytes()).hexdigest()
if path.stat().st_size != wheel['size_bytes'] or actual_sha != wheel['sha256']:
    raise SystemExit('CMake wheel size or SHA256 differs from the official artifact lock')
subprocess.run([sys.executable, '-m', 'pip', 'install', '--no-index', '--no-deps',
                '--no-cache-dir', str(path)], check=True)
version = subprocess.check_output(['cmake', '--version'], text=True).splitlines()[0]
if version != 'cmake version ' + lock['cmake']:
    raise SystemExit('installed CMake version differs from the builder lock: ' + version)
record = {'source': source, 'download_url': url, 'primary_url': wheel['sources']['pypi'],
          'filename': path.name, 'size_bytes': path.stat().st_size,
          'sha256': actual_sha, 'version': version}
(root/'evidence/cmake-wheel.json').write_text(json.dumps(record, indent=2)+'\n')
print(version)
PY
    exit 0
fi
if (($#)); then
    echo 'Unsupported dependency builder argument' >&2
    exit 2
fi
rknn_lock=/opt/cosmo/config/rknn/toolchain-lock.json
ffmpeg_root=/opt/ffmpeg-glibc231
media_root=/opt/rockchip-media/rk3588
jobs="${BUILD_JOBS:-8}"
ffmpeg_source="${FFMPEG_SOURCE:-official}"
mpp_source_mode="${MPP_SOURCE_MODE:-git}"
case "$jobs" in ''|*[!0-9]*|0) echo 'BUILD_JOBS must be positive' >&2; exit 2;; esac
case "${APT_SOURCE_MODE:-}" in snapshot|live|mirror-ustc) ;; *) echo 'APT_SOURCE_MODE must be snapshot, live or mirror-ustc' >&2; exit 2;; esac
case "$ffmpeg_source" in official|macports|macports-aliyun|macports-tuna) ;; *) echo 'FFMPEG_SOURCE must be official, macports, macports-aliyun or macports-tuna' >&2; exit 2;; esac
case "$mpp_source_mode" in git|archive) ;; *) echo 'MPP_SOURCE_MODE must be git or archive' >&2; exit 2;; esac

# The image identities are inputs to the evidence record, never mutable tags.
python3 - <<'PY'
import os, re
for name in ('BULLSEYE_BASE', 'ROCKCHIP_SDK_IMAGE'):
    if not re.fullmatch(r'[^\s@]+@sha256:[0-9a-f]{64}', os.environ.get(name, '')):
        raise SystemExit(name + ' requires a RepoDigest with a complete SHA256')
PY
python3 - "$rknn_lock" <<'PY'
import hashlib, json, pathlib, sys
lock = json.loads(pathlib.Path(sys.argv[1]).read_text())['runtime']
for name, key in [('lib/librknnrt.so','librknnrt_sha256'), ('include/rknn_api.h','rknn_api_header_sha256')]:
    path = pathlib.Path('/opt/rknn') / name
    if hashlib.sha256(path.read_bytes()).hexdigest() != lock[key]:
        raise SystemExit('admitted RKNN asset differs from the repository lock: ' + name)
PY
# The package uses licenses/third-party/rknn-runtime/LICENSE from the source checkout.

read_lock() {
    python3 - "$builder_lock" "$@" <<'PY'
import json, pathlib, sys
value = json.loads(pathlib.Path(sys.argv[1]).read_text())
for key in sys.argv[2:]:
    value = value[key]
print(value)
PY
}
fetch_rockchip_source() {
    local name="$1" destination="$2" repository revision archive archive_url=""
    repository="$(read_lock sources "$name" repository)"
    revision="$(read_lock sources "$name" revision)"
    if [ "$mpp_source_mode" = git ]; then
        git init "$destination"
        git -C "$destination" remote add origin "$repository"
        git -C "$destination" -c http.version=HTTP/1.1 fetch --depth 1 origin "$revision"
        git -C "$destination" checkout --detach FETCH_HEAD
        test "$(git -C "$destination" rev-parse HEAD)" = "$revision"
    else
        archive="/opt/cosmo/sources/$name-$revision.tar.gz"
        archive_url="${MPP_ARCHIVE_URL:-$(read_lock sources "$name" archive url)}"
        curl --fail --location --retry 3 --connect-timeout 30 --max-time 600 \
            "$archive_url" -o "$archive"
        printf '%s  %s\n' "$(read_lock sources "$name" archive sha256)" "$archive" | sha256sum -c -
        mkdir -p "$destination"
        tar -xzf "$archive" -C "$destination" --strip-components=1
        # Upstream supports a source release without VCS metadata. Do not create
        # synthetic Git history or claim that an archive was a verified checkout.
        test ! -e "$destination/.git"
    fi
    python3 - "$builder_lock" "$name" "$mpp_source_mode" "$destination" "$archive_url" <<'PY'
import hashlib, json, pathlib, subprocess, sys
lock = json.loads(pathlib.Path(sys.argv[1]).read_text())['sources'][sys.argv[2]]
mode, destination = sys.argv[3:5]
record = {'mode': mode, 'repository': lock['repository'], 'revision': lock['revision'],
          'git_checkout_verified': mode == 'git'}
if mode == 'git':
    resolved = subprocess.check_output(['git', '-C', destination, 'rev-parse', 'HEAD'], text=True).strip()
    if resolved != lock['revision']:
        raise SystemExit('Rockchip Git revision differs from the builder lock')
    record.update(verification='git-HEAD', resolved_revision=resolved)
    if 'source_date_epoch' in lock:
        epoch = int(subprocess.check_output(['git', '-C', destination, 'show', '-s', '--format=%ct', 'HEAD'], text=True))
        if epoch != lock['source_date_epoch']:
            raise SystemExit('Rockchip Git commit epoch differs from the builder lock')
else:
    archive = pathlib.Path('/opt/cosmo/sources') / (sys.argv[2]+'-'+lock['revision']+'.tar.gz')
    actual_sha = hashlib.sha256(archive.read_bytes()).hexdigest()
    if actual_sha != lock['archive']['sha256'] or archive.stat().st_size != lock['archive']['size_bytes']:
        raise SystemExit('Rockchip source archive differs from the builder lock')
    record.update(verification='locked-archive-sha256', download_url=sys.argv[5], primary_url=lock['archive']['url'],
                  sha256=actual_sha, size_bytes=archive.stat().st_size)
if 'source_date_epoch' in lock:
    record.update(source_date_epoch=lock['source_date_epoch'], commit_metadata=lock['commit_metadata'])
path = pathlib.Path('/opt/cosmo/evidence') / ('rockchip-source-'+sys.argv[2]+'.json')
path.write_text(json.dumps(record, indent=2)+'\n')
PY
}

mkdir -p /opt/cosmo/sources /opt/cosmo/evidence
ffmpeg_url="$(read_lock sources ffmpeg sources "$ffmpeg_source")"
curl --fail --location --retry 3 --connect-timeout 30 --max-time 600 \
    "$ffmpeg_url" -o /opt/cosmo/sources/ffmpeg.tar.xz
printf '%s  %s\n' "$(read_lock sources ffmpeg sha256)" /opt/cosmo/sources/ffmpeg.tar.xz | sha256sum -c -
python3 - "$builder_lock" "$ffmpeg_source" "$ffmpeg_url" <<'PY'
import hashlib, json, pathlib, sys
lock = json.loads(pathlib.Path(sys.argv[1]).read_text())['sources']['ffmpeg']
archive = pathlib.Path('/opt/cosmo/sources/ffmpeg.tar.xz')
record = {'source': sys.argv[2], 'download_url': sys.argv[3], 'primary_url': lock['url'],
          'size_bytes': archive.stat().st_size, 'sha256': hashlib.sha256(archive.read_bytes()).hexdigest()}
if record['sha256'] != lock['sha256'] or record['download_url'] != lock['sources'][record['source']]:
    raise SystemExit('FFmpeg download identity differs from the builder lock')
pathlib.Path('/opt/cosmo/evidence/ffmpeg-source.json').write_text(json.dumps(record, indent=2)+'\n')
PY
printf '%s  %s\n' "$(read_lock sources ffmpeg patch_sha256)" /opt/cosmo/patch_ffmpeg_flv_hevc.cmake | sha256sum -c -
mkdir -p /opt/cosmo/sources/ffmpeg
# Retain the exact patched source and configure log for dependency maintenance.
tar -xJf /opt/cosmo/sources/ffmpeg.tar.xz -C /opt/cosmo/sources/ffmpeg --strip-components=1
cmake -DFFMPEG_SOURCE_DIR=/opt/cosmo/sources/ffmpeg -P /opt/cosmo/patch_ffmpeg_flv_hevc.cmake
cd /opt/cosmo/sources/ffmpeg
PKG_CONFIG_LIBDIR=/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig \
    ./configure --prefix="$ffmpeg_root" --enable-cross-compile \
    --cross-prefix=aarch64-linux-gnu- --arch=aarch64 --target-os=linux \
    --enable-shared --disable-static --enable-pic \
    --disable-gpl --disable-nonfree --disable-autodetect \
    --disable-doc --disable-programs --disable-debug \
    --enable-openssl --enable-zlib
make -j"$jobs"
make install
mkdir -p "$ffmpeg_root/share/licenses/ffmpeg" "$ffmpeg_root/share/cosmo"
cp COPYING.LGPLv2.1 LICENSE.md "$ffmpeg_root/share/licenses/ffmpeg/"
cp ffbuild/config.log /opt/cosmo/evidence/ffmpeg-config.log
cp config.h /opt/cosmo/evidence/ffmpeg-config.h
# External dependency closure is deliberately small and built for Bullseye arm64.
# The ABI gate checks the complete final package separately.
for library in libssl.so.1.1 libcrypto.so.1.1 libz.so.1; do
    library_source="/usr/lib/aarch64-linux-gnu/$library"
    if [ ! -f "$library_source" ]; then
        library_source="/lib/aarch64-linux-gnu/$library"
    fi
    test -f "$library_source"
    cp -L "$library_source" "$ffmpeg_root/lib/$library"
done
for dependency in openssl zlib1g; do
    cp "/usr/share/doc/$dependency/copyright" "$ffmpeg_root/share/licenses/ffmpeg/$dependency-copyright"
done

mpp_revision="$(read_lock sources mpp revision)"
rga_revision="$(read_lock sources rga revision)"
fetch_rockchip_source mpp /opt/cosmo/sources/mpp
export SOURCE_DATE_EPOCH
# Frozen from the official commit API; available without .git in archive mode.
SOURCE_DATE_EPOCH="$(read_lock sources mpp source_date_epoch)"
cmake -S /opt/cosmo/sources/mpp -B /opt/cosmo/build/mpp \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=Linux \
    -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
    -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ \
    -DCMAKE_C_FLAGS='-ffile-prefix-map=/opt/cosmo/sources/mpp=/usr/src/rockchip/mpp-1.1.0 -ffile-prefix-map=/opt/cosmo/build/mpp=/usr/src/rockchip/mpp-1.1.0-build' \
    -DCMAKE_CXX_FLAGS='-ffile-prefix-map=/opt/cosmo/sources/mpp=/usr/src/rockchip/mpp-1.1.0 -ffile-prefix-map=/opt/cosmo/build/mpp=/usr/src/rockchip/mpp-1.1.0-build' \
    -DCMAKE_INSTALL_PREFIX="$media_root" -DBUILD_TEST=OFF
cmake --build /opt/cosmo/build/mpp --parallel "$jobs"
cmake --install /opt/cosmo/build/mpp
mkdir -p "$media_root/share/licenses/rockchip-media/mpp"
cp /opt/cosmo/sources/mpp/LICENSES/Apache-2.0 /opt/cosmo/sources/mpp/LICENSES/MIT "$media_root/share/licenses/rockchip-media/mpp/"
python3 /usr/local/bin/cosmo-rockchip-media-lock seal \
    --platform-profile /opt/cosmo/config/rknn/platforms/rk3588.json \
    --root "$media_root" --source "mpp=$mpp_revision" --source "rga=$rga_revision"
python3 /usr/local/bin/cosmo-rockchip-media-lock verify \
    --platform-profile /opt/cosmo/config/rknn/platforms/rk3588.json --root "$media_root"

dpkg-query -W -f='${Package}\t${Version}\t${Architecture}\n' > /opt/cosmo/evidence/dpkg-packages.tsv
cp /etc/apt/sources.list /opt/cosmo/evidence/apt-sources.list
python3 -m pip list --format=json > /opt/cosmo/evidence/python-packages.json
python3 - <<'PY'
import hashlib, json, os, pathlib, subprocess

def command(*args):
    return subprocess.check_output(args, text=True).strip()
def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
root = pathlib.Path('/opt/ffmpeg-glibc231')
lock = json.loads(pathlib.Path('/opt/cosmo/rockchip-builder-lock.json').read_text())
cmake_wheel = json.loads(pathlib.Path('/opt/cosmo/evidence/cmake-wheel.json').read_text())
if cmake_wheel['sha256'] != lock['sources']['cmake_wheel']['sha256']:
    raise SystemExit('CMake wheel evidence differs from the builder lock')
ffmpeg_source = json.loads(pathlib.Path('/opt/cosmo/evidence/ffmpeg-source.json').read_text())
if ffmpeg_source['sha256'] != lock['sources']['ffmpeg']['sha256']:
    raise SystemExit('FFmpeg source evidence differs from the builder lock')
# RGA is copied from the digest-bound SDK; the completed seal above verifies
# the selected runtime's header, binary, ELF and license constraints.
media_root = pathlib.Path('/opt/rockchip-media/rk3588')
media_seal = json.loads((media_root/'.cosmo-rockchip-media.json').read_text())
rga_lock = lock['sources']['rga']
if any(media_seal['sources']['rga'][key] != rga_lock[key] for key in ('repository', 'revision')):
    raise SystemExit('admitted RGA source identity differs from the media runtime lock')
rga_paths = sorted((media_root/'include/rga').rglob('*'))
rga_paths += [media_root/'lib/librga.so', media_root/'share/licenses/rockchip-media/librga/COPYING']
rga_source = {
    'mode': 'admitted-sdk', 'repository': rga_lock['repository'], 'revision': rga_lock['revision'],
    'admitted_sdk_image': os.environ['ROCKCHIP_SDK_IMAGE'], 'sdk_path': rga_lock['sdk_path'],
    'git_checkout_verified': False, 'verification': 'runtime-lock-artifact-sha256-and-ELF',
    'artifacts': [{'path': str(p.relative_to(media_root)), 'sha256': digest(p)} for p in rga_paths if p.is_file()],
}
pathlib.Path('/opt/cosmo/evidence/rockchip-source-rga.json').write_text(json.dumps(rga_source, indent=2)+'\n')
rockchip_sources = {}
for name in ('mpp', 'rga'):
    evidence = json.loads(pathlib.Path('/opt/cosmo/evidence/rockchip-source-'+name+'.json').read_text())
    expected_mode = os.environ['MPP_SOURCE_MODE'] if name == 'mpp' else 'admitted-sdk'
    if evidence['revision'] != lock['sources'][name]['revision'] or evidence['mode'] != expected_mode:
        raise SystemExit('Rockchip source evidence differs from the builder lock or selected mode')
    if evidence['mode'] == 'archive' and evidence['sha256'] != lock['sources'][name]['archive']['sha256']:
        raise SystemExit('Rockchip archive evidence differs from the builder lock')
    rockchip_sources[name] = evidence
manifest = {
    'schemaVersion': 1, 'ffmpegVersion': '4.4.6',
    'source': lock['sources']['ffmpeg'],
    'download': ffmpeg_source,
    'integration': 'External complete FFmpeg prefix; legacy FLV HEVC CodecID 12 patch applied',
    'configuration': command('aarch64-linux-gnu-strings', str(root/'lib/libavcodec.so')).splitlines(),
    'artifacts': [{'path':str(p.relative_to(root)), 'sha256':digest(p)}
                  for p in sorted((root/'lib').glob('*.so*')) if p.is_file() and not p.is_symlink()],
}
# Store only the actual FFmpeg configure line, not arbitrary binary strings.
manifest['configuration'] = [line for line in manifest['configuration'] if '--prefix=' in line]
(root/'share/cosmo/ffmpeg-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
record = {
    'schema_version':1, 'base_image':os.environ['BULLSEYE_BASE'],
    'admitted_sdk_image':os.environ['ROCKCHIP_SDK_IMAGE'],
    'apt_source_mode':os.environ['APT_SOURCE_MODE'],
    'apt_snapshot':lock['sources']['apt_snapshot'] if os.environ['APT_SOURCE_MODE'] == 'snapshot' else None,
    'apt_resolution': 'fixed snapshot' if os.environ['APT_SOURCE_MODE'] == 'snapshot' else 'non-snapshot signed suites; resolved packages frozen only by this build record',
    'apt_sources':pathlib.Path('/etc/apt/sources.list').read_text().splitlines(),
    'dpkg_packages_sha256':digest(pathlib.Path('/opt/cosmo/evidence/dpkg-packages.tsv')),
    'glibc_target':'2.31', 'builder_lock_sha256':digest(pathlib.Path('/opt/cosmo/rockchip-builder-lock.json')),
    'cross_gcc':command('aarch64-linux-gnu-gcc','--version').splitlines()[0],
    'cmake':command('cmake','--version').splitlines()[0],
    'cmake_wheel':cmake_wheel,
    'ffmpeg_source':ffmpeg_source,
    'mpp_source_mode':os.environ['MPP_SOURCE_MODE'],
    'rockchip_sources':rockchip_sources,
    'node':command('node','--version'), 'rustc':command('rustc','--version'),
    'cargo':command('cargo','--version'),
    'host_tools':{p:digest(pathlib.Path(p)) for p in ['/usr/local/node/bin/node',command('rustup','which','rustc'),command('rustup','which','cargo')]},
    'rknn_runtime_sha256':digest(pathlib.Path('/opt/rknn/lib/librknnrt.so')),
}
pathlib.Path('/opt/cosmo/builder-versions.json').write_text(json.dumps(record,indent=2)+'\n')
PY
rm -rf /opt/cosmo/build
