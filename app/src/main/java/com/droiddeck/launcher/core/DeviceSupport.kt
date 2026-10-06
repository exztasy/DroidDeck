package com.droiddeck.launcher.core

import android.os.Build
import java.io.File

/**
 * Whether this device can draw a Linux session at all, and which GPU family it has.
 *
 * The runtime draws with Turnip, an Adreno driver. On Mali, Xclipse and PowerVR the compositor
 * gets no usable Vulkan device and a session comes up as sound over a black screen - a failure
 * with nothing in it to read. The app cannot fix that, so it says so before the download rather
 * than after it. Adreno is recognised by what only Qualcomm's stack has: the KGSL node, or the
 * vendor's own Vulkan driver at its usual path.
 *
 * Mali (Google Tensor G1-G4, most Exynos and MediaTek parts) is the first family past Adreno that
 * the app works towards: the compositor runs there on the system Vulkan driver, and every session
 * records what that driver offers (`gpu.txt`), which is what a Linux-side Mali path depends on.
 * It is recognised by ARM's kbase node or the vendor's Mali driver files.
 */
object DeviceSupport {
    enum class Family { ADRENO, MALI, OTHER }

    fun adreno(): Boolean =
        File("/sys/class/kgsl/kgsl-3d0").exists() || File("/vendor/lib64/hw/vulkan.adreno.so").exists()

    fun mali(): Boolean =
        File("/dev/mali0").exists() || File("/sys/class/misc/mali0").exists() || MALI_DRIVERS.any { File(it).exists() }

    fun family(): Family = when {
        adreno() -> Family.ADRENO
        mali() -> Family.MALI
        else -> Family.OTHER
    }

    /** The chip as the device names it, for the card that explains the refusal. */
    fun gpuName(): String {
        val soc = socModel()
        val gpu = gpuModel()
        return when {
            soc != null && gpu != null -> "$soc ($gpu)"
            soc != null -> "$soc (${Build.HARDWARE})"
            gpu != null -> gpu
            else -> Build.HARDWARE.ifBlank { "this GPU" }
        }
    }

    /**
     * The GPU's own name where the device gives it out: kbase's gpuinfo ("Mali-G715 7 cores r0p1
     * 0xA867") when SELinux lets the app read it, else what each Tensor ships with.
     */
    fun gpuModel(): String? {
        val info = MALI_GPUINFO.firstNotNullOfOrNull { FileUtils.readString(File(it))?.trim()?.ifEmpty { null } }
        if (info != null) return info.substringBefore(" r").substringBefore(" 0x").trim()
        return TENSOR_GPUS[socModel()?.lowercase()]
    }

    private fun socModel(): String? =
        if (Build.VERSION.SDK_INT >= 31) Build.SOC_MODEL.takeIf { it.isNotBlank() && it != Build.UNKNOWN } else null

    /** The vendor Mali drivers' usual paths (Pixel, Exynos, MediaTek). */
    val MALI_DRIVERS = listOf(
        "/vendor/lib64/hw/vulkan.mali.so",
        "/vendor/lib64/egl/libGLES_mali.so",
        "/vendor/lib64/egl/mali.so",
    )

    val MALI_GPUINFO = listOf(
        "/sys/class/misc/mali0/device/gpuinfo",
        "/sys/devices/platform/1f000000.mali/gpuinfo",
    )

    /** Tensor G5 moved to PowerVR, so a Tensor is not always a Mali. */
    private val TENSOR_GPUS = mapOf(
        "tensor" to "Mali-G78 MP20",
        "tensor g2" to "Mali-G710 MP7",
        "tensor g3" to "Mali-G715 MC7",
        "tensor g4" to "Mali-G715 MC7",
        "tensor g5" to "PowerVR DXT-48-1536",
    )
}
