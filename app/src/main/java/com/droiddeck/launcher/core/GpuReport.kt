package com.droiddeck.launcher.core

import android.system.Os
import android.system.OsConstants
import android.util.Log
import com.droiddeck.launcher.wayland.WaylandCompositor
import java.io.File

/**
 * `gpu.txt`: what a GPU Turnip cannot drive offers instead, written beside `device.txt` on every
 * session that is not on an Adreno.
 *
 * On Mali (Google Tensor) the only Vulkan in reach is the vendor's Android driver, so a Linux-side
 * path has to forward to it, and whether that works is decided by facts no one here can see without
 * the device: which kernel node the app may open, which extensions the driver has, and which handle
 * types it can export memory as. This writes them down once per session so a report from a Pixel
 * answers those questions without a second round trip. Hardware facts only, nothing identifying.
 */
object GpuReport {
    private const val TAG = "GpuReport"

    /** Off the caller's thread: the probe creates a Vulkan instance, which can take a while. */
    fun writeAsync(target: File) {
        if (DeviceSupport.family() == DeviceSupport.Family.ADRENO) return
        Thread({
            try {
                target.writeText(build())
            } catch (e: Throwable) {
                Log.w(TAG, "could not write $target", e)
            }
        }, "gpu-report").start()
    }

    fun build(): String {
        val b = StringBuilder()
        fun h(title: String) {
            b.append('\n').append(title).append('\n').append("-".repeat(title.length)).append('\n')
        }
        fun k(key: String, value: Any?) {
            b.append(key.padEnd(24)).append(value ?: "unknown").append('\n')
        }

        b.append("DroidDeck GPU report\n")
        b.append("====================\n")
        k("Family", DeviceSupport.family())
        k("GPU", DeviceSupport.gpuName())

        h("Kernel driver")
        for (node in listOf("/dev/mali0", "/dev/dri/renderD128", "/dev/kgsl-3d0")) {
            if (File(node).exists()) k(node, access(node))
        }
        for (path in DeviceSupport.MALI_GPUINFO) {
            if (File(path).exists()) k(File(path).name, FileUtils.readString(File(path))?.trim() ?: "present, not readable")
        }

        h("Vendor driver files")
        // Whatever Vulkan HALs the vendor ships, by name: the board name in it says which one loads.
        val hals = File("/vendor/lib64/hw").listFiles { f -> f.name.startsWith("vulkan.") }?.map { it.path }.orEmpty()
        for (path in (DeviceSupport.MALI_DRIVERS + hals).distinct()) {
            if (File(path).exists()) k(File(path).name, path)
        }

        h("System Vulkan driver")
        b.append(runCatching { WaylandCompositor.nativeProbeSystemVulkan() }
            .getOrElse { "probe failed: $it\n" })
        return b.toString()
    }

    private fun access(path: String): String {
        val read = runCatching { Os.access(path, OsConstants.R_OK) }.getOrDefault(false)
        val write = runCatching { Os.access(path, OsConstants.R_OK or OsConstants.W_OK) }.getOrDefault(false)
        return when {
            write -> "present, the app may open it read-write"
            read -> "present, read-only for the app"
            else -> "present, not openable by the app"
        }
    }
}
