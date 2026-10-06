#!/usr/bin/env bash
# Builds the Venus server for Android: virglrenderer's vtest server and its render server, with
# tools/venus/patches applied, linked statically against virglrenderer and libepoxy. The two
# programs are installed into $1 as libvirgl_test_server.so and libvirgl_render_server.so - named
# as libraries so the installer places them where Android lets the app execute them.
#
# On a GPU Turnip cannot drive (Mali), the session runs the server out here, on the device's own
# Vulkan driver, and the runtime's Venus driver forwards every Vulkan call to it over a socket.
set -euo pipefail
OUTDIR=$(mkdir -p "$1" && cd "$1" && pwd)
: "${NDK:?set NDK to the Android NDK root}"
# memfd_create (vtest's shared memory) is API 30; the server only runs on devices far newer.
API=31
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/source.env"
case "$(uname -s):$(uname -m)" in
  Linux:x86_64) NDK_HOST=linux-x86_64 ;;
  Darwin:*) NDK_HOST=darwin-x86_64 ;;
  *) echo "Unsupported build host: $(uname -s) $(uname -m)" >&2; exit 1 ;;
esac
TOOLCHAIN="$NDK/toolchains/llvm/prebuilt/$NDK_HOST/bin"
CC="$TOOLCHAIN/aarch64-linux-android${API}-clang"
[[ -x "$CC" ]] || { echo "Android NDK toolchain not found at $TOOLCHAIN" >&2; exit 1; }
command -v meson >/dev/null || { echo "meson is required" >&2; exit 1; }
command -v ninja >/dev/null || { echo "ninja is required" >&2; exit 1; }

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
fetch() {
  curl -fsSL --retry 3 -o "$WORK/$1" "$2"
  echo "$3  $WORK/$1" | sha256sum -c -
  mkdir -p "$WORK/${1%%.*}"
  tar -xf "$WORK/$1" -C "$WORK/${1%%.*}" --strip-components=1
}
fetch epoxy.tar.gz "$EPOXY_URL" "$EPOXY_SHA256"
fetch virgl.tar.bz2 "$VIRGL_URL" "$VIRGL_SHA256"
for patch in "$HERE"/patches/*.patch; do
  echo "applying $(basename "$patch")"
  patch -d "$WORK/virgl" -p1 --forward < "$patch"
done

cat > "$WORK/cross.ini" <<EOF
[binaries]
c = '$CC'
cpp = '$TOOLCHAIN/aarch64-linux-android${API}-clang++'
ar = '$TOOLCHAIN/llvm-ar'
strip = '$TOOLCHAIN/llvm-strip'
pkg-config = 'pkg-config'

[built-in options]
c_args = ['-ffile-prefix-map=$WORK=.', '-I$HERE/compat']
c_link_args = ['-Wl,-z,noexecstack']

[properties]
needs_exe_wrapper = true

[host_machine]
system = 'android'
cpu_family = 'aarch64'
cpu = 'armv8'
endian = 'little'
EOF

PREFIX="$WORK/prefix"
export PKG_CONFIG_LIBDIR="$PREFIX/lib/pkgconfig"
export PKG_CONFIG_PATH=
meson setup "$WORK/epoxy-build" "$WORK/epoxy" --cross-file "$WORK/cross.ini" --prefix "$PREFIX" \
  --libdir lib --buildtype release -Ddefault_library=static \
  -Degl=yes -Dglx=no -Dx11=false -Dtests=false
ninja -C "$WORK/epoxy-build" install

# Venus only: --no-virgl at run time, so no GL platform is needed (gbm does not exist here).
# compat/ stands in for the two AOSP headers (log/log.h, cutils/properties.h) the bundled Mesa util
# code includes when it sees Android, on the NDK's liblog and system properties.
# The device's Vulkan is dlopen'ed (vulkan-dload); the render server runs each context on a thread.
meson setup "$WORK/virgl-build" "$WORK/virgl" --cross-file "$WORK/cross.ini" --prefix "$PREFIX" \
  --libdir lib --buildtype release -Ddefault_library=static \
  -Dvenus=true -Dvulkan-dload=true -Drender-server-worker=thread -Dplatforms=auto \
  -Dtests=false -Dc_link_args="['-Wl,-z,noexecstack','-lnativewindow','-llog']"
ninja -C "$WORK/virgl-build" vtest/virgl_test_server server/virgl_render_server

install -m755 "$WORK/virgl-build/vtest/virgl_test_server" "$OUTDIR/libvirgl_test_server.so"
install -m755 "$WORK/virgl-build/server/virgl_render_server" "$OUTDIR/libvirgl_render_server.so"
for exe in libvirgl_test_server.so libvirgl_render_server.so; do
  "$TOOLCHAIN/llvm-strip" --strip-unneeded "$OUTDIR/$exe"
  NEEDED=$("$TOOLCHAIN/llvm-readelf" -d "$OUTDIR/$exe" | sed -n 's/.*NEEDED.*\[\(.*\)\]/\1/p' | tr '\n' ' ')
  echo "$exe NEEDED: $NEEDED"
  for lib in $NEEDED; do
    case $lib in libc.so|libdl.so|libm.so|liblog.so|libnativewindow.so|libandroid.so) ;;
    *) echo "ERROR: $exe has an unexpected dependency $lib"; exit 1 ;;
    esac
  done
done
# The allocator patch is in: its log line is in the server's strings.
grep -q 'AHardwareBuffer_getNativeHandle unavailable' "$OUTDIR/libvirgl_render_server.so" ||
  grep -q 'AHardwareBuffer_getNativeHandle unavailable' "$OUTDIR/libvirgl_test_server.so" ||
  { echo "ERROR: the AHardwareBuffer allocator is not in the build"; exit 1; }
ls -l "$OUTDIR"/libvirgl_*.so
