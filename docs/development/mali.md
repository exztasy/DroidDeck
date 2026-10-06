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

1. **The runtime has a Vulkan device on Mali.** Confirmed on the Pixel 8a: `vulkaninfo` in the
   runtime reports `Virtio-GPU Venus (Mali-G715)`, Vulkan 1.4.334.
2. **gamescope presents.** In progress. Venus over vtest reports no DRM node, has no
   VK_KHR_external_semaphore_fd and cannot import dma-bufs (the import call is a null function
   in the vtest renderer). So:
   - gamescope runs without a DRM node (`tools/gamescope/patches/0120-…`): no explicit sync, its
     output exported LINEAR from gralloc-backed memory;
   - the server offers dma-buf export and allocates every exportable buffer through gralloc
     (`tools/venus/patches/0001-…`);
   - every program in the runtime presents through CPU images (`MESA_VK_WSI_DEBUG=sw`), so
     gamescope never has to import a dma-buf: wl_shm on Wayland, PutImage on X11.
3. **Steam's UI.** Follows from 2: CEF (ANGLE on Vulkan) and Zink present through the same CPU
   path, and Xwayland runs without glamor. Slow (a GPU-to-CPU copy per frame) until dma-buf
   import exists over the socket, which needs a vtest protocol extension and our own Mesa build.

## Testing on a device

Start a Steam session, then open its folder in `Download/DroidDeck/`:

- `session.log`: the `== vulkan driver: Venus` line, then `== vulkaninfo:` lines. A working bridge
  shows a device named `Virtio-GPU Venus (Mali-G715)`. gamescope's own lines follow; with no DRM
  node it says `physical device names no DRM node; running without one`.
- `venus-vulkaninfo.txt`: the full report, with the device's features.
- `venus.log`: the server's own output.
- `gpu.txt`: what the system driver offers (written on every non-Adreno session).
- `app.log`: the `VenusServer` and `venus:` lines say whether the server started and why not.
