# Mali GPUs (Google Tensor)

Mali support is experimental. The target device is the Pixel 8a (Tensor G3, Mali-G715).

## How the runtime draws on Mali

Everything in the runtime normally draws with Turnip, which needs Qualcomm's KGSL. Pixels run ARM's
`mali_kbase` rather than the DRM `panthor` driver, so Mesa's PanVK cannot run either. The only
Vulkan on the device is the vendor's Android driver, which the runtime's glibc programs cannot load.

So on Mali the runtime draws through **Venus**:

- The runtime's Vulkan loader is pointed at Mesa's Venus driver (`libvulkan_virtio.so`, staged at
  `/usr/local/lib/droiddeck-venus`). Venus serializes every Vulkan call over a unix socket
  (`VN_DEBUG=vtest`, `VTEST_SOCKET_NAME`).
- The app runs virglrenderer's vtest server (`libvirgl_test_server.so`, with its render server
  `libvirgl_render_server.so`) on the device's own Vulkan driver, and it executes those calls
  (`gpu/VenusServer`).
- Memory the runtime maps is shared as dma-bufs. Mali can import dma-bufs but cannot export any fd,
  and Android has neither gbm nor udmabuf, so the server allocates that memory as BLOB
  AHardwareBuffers, whose dma-bufs Mali imports and the runtime maps
  (`tools/venus/patches/0001-…`).

The app's own compositor uses the system Mali driver directly. It imports linear dma-bufs, which
the Pixel 8a confirmed.

## Pieces and how they are built

| Piece | Built by | Ends up |
|---|---|---|
| Venus server (virglrenderer 1.3.0 + patch, libepoxy) | `tools/venus/build-host.sh` (NDK) in Build APK | `lib/arm64-v8a/libvirgl_*.so` in the apk |
| Venus driver, manifest, `vulkaninfo` | `tools/venus/build-guest-in-arch.sh` in **Build Venus guest driver**, published as a release | staged into the runtime at each session start |

To update the runtime half, run **Build Venus guest driver** with a new tag, then put the tag and
the printed sha256 in `tools/venus/release.env`.

## Status

1. **The runtime has a Vulkan device on Mali.** Built, not yet confirmed on a device.
2. **gamescope presents.** Not done. Venus over vtest can export dma-bufs but not import them, and
   gamescope imports its clients' buffers. This needs a vtest extension that passes fds from the
   runtime to the server.
3. **Steam's UI.** Not done. Xwayland's GPU path needs a DRM node, which the Pixel does not give
   apps; on Adreno a KGSL stand-in fills that role (`tools/linuxfs/preload/drm.c`).

## Testing on a device

Start a Steam session, then open its folder in `Download/DroidDeck/`:

- `session.log`: the `== vulkan driver: Venus` line, then `== vulkaninfo:` lines. A working bridge
  shows a device named `Virtio-GPU Venus (Mali-G715)`.
- `venus.log`: the server's own output.
- `gpu.txt`: what the system driver offers (written on every non-Adreno session).
- `app.log`: the `VenusServer` and `venus:` lines say whether the server started and why not.
