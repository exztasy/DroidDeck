# Mali GPUs (Google Tensor)

Mali support is experimental. The target device is the Pixel 8a (Tensor G3, Mali-G715).

## What is in place (not yet confirmed on a device)

On a Mali GPU, Setup shows **Mali GPU · experimental** instead of refusing, and the runtime can be
installed after a warning. The app's compositor puts frames on the screen with Android's own Mali
Vulkan driver. It no longer loads Turnip, which finds no KGSL device there. The compositor requests
the dma-buf import extensions only when the driver reports them. Without them it still starts and
shows `wl_shm` clients, and it refuses dma-buf clients with a log line instead of failing
`vkCreateDevice`.

## What does not work yet

Everything inside the runtime still draws with Turnip: gamescope, the Steam client through Zink,
and games through DXVK/VKD3D. Turnip needs Qualcomm's KGSL. Pixels run ARM's `mali_kbase` rather
than the DRM `panthor` driver, so Mesa's PanVK cannot run either. The way forward is a forwarding
driver: a Venus-style ICD in the runtime that sends Vulkan calls to a server in the app, which
executes them on the Mali driver. Whether that can work depends on what the Mali driver can export,
and `gpu.txt` records exactly that.

## Testing on a device

Start any session (Steam or desktop), then open its folder in `Download/DroidDeck/`. On a
non-Adreno device it has a `gpu.txt` next to `device.txt` with:

- whether the app can open `/dev/mali0`, and kbase's `gpuinfo` when it is readable;
- the vendor Vulkan HAL files;
- the system driver's version, memory types, and which handle types (opaque fd, dma-buf,
  AHardwareBuffer, host allocation) it can export or import buffer memory as;
- the device extensions that matter for the compositor, for a forwarding driver and for
  DXVK/VKD3D (`+` present, `-` missing), followed by the full list.

Attach `gpu.txt`, `device.txt` and `app.log` to the report. In `app.log` or logcat, the `gpu`
lines say which driver the compositor runs on and whether it imports dma-bufs.
