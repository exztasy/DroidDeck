#!/usr/bin/bash
# Runs INSIDE an Arch Linux ARM container (menci/archlinuxarm:base-devel) on an arm64 runner.
# Packs Arch's own Venus Vulkan driver (vulkan-virtio) and vulkaninfo as venus-guest.tzst, under
# usr/local/lib/droiddeck-venus, for the apk to stage into the runtime. On a Mali device the
# session points the Vulkan loader at virtio_icd.json there; the driver forwards every call over
# a socket to the app's Venus server (tools/venus/build-host.sh). Arch's own build, so it links
# what the runtime (also Arch) already has; only these files are added.
set -euxo pipefail
WORK=/work
cd "$WORK"
# Same container workarounds as tools/wlroots/build-in-arch.sh.
grep -q '^DisableSandbox' /etc/pacman.conf || sed -i 's/^\[options\]/[options]\nDisableSandbox/' /etc/pacman.conf
{ for m in https://ca.us.mirror.archlinuxarm.org https://fl.us.mirror.archlinuxarm.org https://de3.mirror.archlinuxarm.org https://nl.mirror.archlinuxarm.org; do echo "Server = $m/\$arch/\$repo"; done; cat /etc/pacman.d/mirrorlist; } > /etc/pacman.d/mirrorlist.new
mv /etc/pacman.d/mirrorlist.new /etc/pacman.d/mirrorlist
pacman -Syu --noconfirm --needed zstd binutils vulkan-virtio vulkan-tools
D=usr/local/lib/droiddeck-venus
rm -rf out && mkdir -p "out/$D"
cp -L /usr/lib/libvulkan_virtio.so "out/$D/libvulkan_virtio.so"
cp -L /usr/bin/vulkaninfo "out/$D/vulkaninfo"
strip --strip-unneeded "out/$D/libvulkan_virtio.so" "out/$D/vulkaninfo" || true
# An absolute library_path: the manifest is read where the app staged it, whatever the cwd.
API=$(sed -n 's/.*"api_version"[^"]*"\([^"]*\)".*/\1/p' /usr/share/vulkan/icd.d/virtio_icd*.json | head -1)
printf '{\n  "file_format_version": "1.0.1",\n  "ICD": {\n    "library_path": "/%s/libvulkan_virtio.so",\n    "api_version": "%s"\n  }\n}\n' "$D" "${API:-1.4.0}" > "out/$D/virtio_icd.json"
pacman -Q vulkan-virtio vulkan-tools | tee "out/$D/VERSION"
for f in libvulkan_virtio.so vulkaninfo; do
  echo "== $f"; readelf -d "out/$D/$f" | grep -E 'NEEDED|SONAME'
  # The newest glibc symbol it asks for; the runtime's glibc must be at least this.
  objdump -T "out/$D/$f" | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1
done
cat "out/$D/virtio_icd.json"
(cd out && tar --use-compress-program='zstd -19' -cf ../venus-guest.tzst usr)
sha256sum venus-guest.tzst | tee venus-guest.tzst.sha256
ls -l venus-guest.tzst "out/$D"
