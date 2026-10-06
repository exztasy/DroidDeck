/*
 * What the device's own (system) Vulkan driver offers, as text for the session's gpu.txt.
 *
 * Written for the GPUs Turnip cannot drive - Mali on Google Tensor first. A Linux-side path for
 * them has to go through this driver (the guest cannot reach mali_kbase any other way), and
 * whether it can turns on what is printed here: the device extensions, and above all which
 * handle types the driver can EXPORT host-visible memory as. Nothing is kept: the instance is
 * created, read and destroyed, and the library is closed again. Independent of the compositor's
 * own loader (vk_loader.c), which may hold a Turnip instead.
 */
#define VK_USE_PLATFORM_ANDROID_KHR
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <jni.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct out { char *buf; size_t len, cap; };

static void put(struct out *o, const char *fmt, ...) {
    va_list ap;
    for (;;) {
        va_start(ap, fmt);
        int n = vsnprintf(o->buf ? o->buf + o->len : NULL, o->buf ? o->cap - o->len : 0, fmt, ap);
        va_end(ap);
        if (n < 0) return;
        if (o->buf && o->len + (size_t)n < o->cap) { o->len += (size_t)n; return; }
        size_t cap = (o->cap ? o->cap * 2 : 4096) + (size_t)n;
        char *b = realloc(o->buf, cap);
        if (!b) return;
        o->buf = b;
        o->cap = cap;
    }
}

static int has_ext(const VkExtensionProperties *e, uint32_t n, const char *name) {
    for (uint32_t i = 0; i < n; i++)
        if (!strcmp(e[i].extensionName, name)) return 1;
    return 0;
}

/* The extensions each way of getting frames and memory across matter for; "-" marks a missing one. */
static const struct { const char *name, *why; } KEY_EXTS[] = {
    {"VK_KHR_external_memory_fd", "compositor dma-buf import; fd memory export"},
    {"VK_EXT_external_memory_dma_buf", "compositor dma-buf import; dma-buf memory export"},
    {"VK_EXT_image_drm_format_modifier", "compositor dma-buf import (gamescope's frames)"},
    {"VK_KHR_image_format_list", "compositor dma-buf import"},
    {"VK_ANDROID_external_memory_android_hardware_buffer", "memory shared as AHardwareBuffers"},
    {"VK_EXT_external_memory_host", "importing guest-allocated memory"},
    {"VK_EXT_queue_family_foreign", "handing images to another device or process"},
    {"VK_KHR_external_semaphore_fd", "GPU-side waits on sync_file fences"},
    {"VK_KHR_external_fence_fd", "sync_file fences across processes"},
    {"VK_KHR_timeline_semaphore", "timeline sync (core in 1.2)"},
    {"VK_EXT_descriptor_indexing", "DXVK/VKD3D (core in 1.2)"},
    {"VK_EXT_robustness2", "DXVK/VKD3D"},
    {"VK_EXT_transform_feedback", "DXVK (D3D11 stream output)"},
    {"VK_KHR_dynamic_rendering", "DXVK 2.x (core in 1.3)"},
};

static const struct { VkExternalMemoryHandleTypeFlagBits bit; const char *name; } HANDLE_TYPES[] = {
    {VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT, "opaque fd"},
    {VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, "dma-buf"},
    {VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID, "AHardwareBuffer"},
    {VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT, "host allocation"},
};

static void probe_device(struct out *o, VkInstance inst, PFN_vkGetInstanceProcAddr gipa, VkPhysicalDevice pd,
                         uint32_t inst_version) {
#define GET(n) PFN_vk##n n = (PFN_vk##n)gipa(inst, "vk" #n)
    GET(GetPhysicalDeviceProperties);
    GET(GetPhysicalDeviceProperties2);
    GET(GetPhysicalDeviceMemoryProperties);
    GET(EnumerateDeviceExtensionProperties);
    GET(GetPhysicalDeviceExternalBufferProperties);
#undef GET
    if (!GetPhysicalDeviceProperties || !EnumerateDeviceExtensionProperties) return;

    VkPhysicalDeviceProperties p;
    GetPhysicalDeviceProperties(pd, &p);
    put(o, "Device                  %s\n", p.deviceName);
    put(o, "Vendor / device id      0x%04x / 0x%08x\n", p.vendorID, p.deviceID);
    put(o, "Vulkan                  %u.%u.%u\n", VK_API_VERSION_MAJOR(p.apiVersion),
        VK_API_VERSION_MINOR(p.apiVersion), VK_API_VERSION_PATCH(p.apiVersion));
    put(o, "Driver version          0x%08x\n", p.driverVersion);
    if (GetPhysicalDeviceProperties2 && p.apiVersion >= VK_API_VERSION_1_2 && inst_version >= VK_API_VERSION_1_2) {
        VkPhysicalDeviceDriverProperties dp = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
        VkPhysicalDeviceProperties2 p2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &dp};
        GetPhysicalDeviceProperties2(pd, &p2);
        put(o, "Driver                  %s (%s), conformance %u.%u.%u.%u\n", dp.driverName, dp.driverInfo,
            dp.conformanceVersion.major, dp.conformanceVersion.minor, dp.conformanceVersion.subminor,
            dp.conformanceVersion.patch);
    }

    if (GetPhysicalDeviceMemoryProperties) {
        VkPhysicalDeviceMemoryProperties mp;
        GetPhysicalDeviceMemoryProperties(pd, &mp);
        for (uint32_t h = 0; h < mp.memoryHeapCount; h++)
            put(o, "Memory heap %u           %llu MB%s\n", h,
                (unsigned long long)(mp.memoryHeaps[h].size >> 20),
                (mp.memoryHeaps[h].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) ? ", device-local" : "");
        for (uint32_t t = 0; t < mp.memoryTypeCount; t++) {
            VkMemoryPropertyFlags f = mp.memoryTypes[t].propertyFlags;
            put(o, "Memory type %-2u          heap %u:%s%s%s%s\n", t, mp.memoryTypes[t].heapIndex,
                (f & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ? " device-local" : "",
                (f & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) ? " host-visible" : "",
                (f & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) ? " coherent" : "",
                (f & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) ? " cached" : "");
        }
    }

    /* Can a plain buffer's memory leave the process, and as what? The question a forwarding
     * driver (the guest renders, this driver executes) stands or falls on. */
    if (GetPhysicalDeviceExternalBufferProperties && inst_version >= VK_API_VERSION_1_1) {
        for (size_t i = 0; i < sizeof(HANDLE_TYPES) / sizeof(HANDLE_TYPES[0]); i++) {
            VkPhysicalDeviceExternalBufferInfo bi = {
                .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO,
                .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                .handleType = HANDLE_TYPES[i].bit};
            VkExternalBufferProperties bp = {.sType = VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES};
            GetPhysicalDeviceExternalBufferProperties(pd, &bi, &bp);
            VkExternalMemoryFeatureFlags f = bp.externalMemoryProperties.externalMemoryFeatures;
            put(o, "Buffer memory as %-16s %s%s%s\n", HANDLE_TYPES[i].name,
                (f & VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT) ? "export " : "",
                (f & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) ? "import " : "",
                (f & (VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT | VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT))
                    ? "" : "no");
        }
    }

    uint32_t ne = 0;
    EnumerateDeviceExtensionProperties(pd, NULL, &ne, NULL);
    VkExtensionProperties *exts = calloc(ne ? ne : 1, sizeof(*exts));
    if (!exts) return;
    EnumerateDeviceExtensionProperties(pd, NULL, &ne, exts);
    put(o, "\nKey extensions\n");
    for (size_t i = 0; i < sizeof(KEY_EXTS) / sizeof(KEY_EXTS[0]); i++)
        put(o, "  %c %-52s %s\n", has_ext(exts, ne, KEY_EXTS[i].name) ? '+' : '-', KEY_EXTS[i].name,
            KEY_EXTS[i].why);
    put(o, "\nAll %u device extensions\n", ne);
    for (uint32_t i = 0; i < ne; i++) put(o, "  %s (rev %u)\n", exts[i].extensionName, exts[i].specVersion);
    free(exts);
}

static char *probe(void) {
    struct out o = {0};
    void *lib = dlopen("libvulkan.so", RTLD_LOCAL | RTLD_NOW);
    if (!lib) { put(&o, "System libvulkan.so could not be loaded: %s\n", dlerror()); return o.buf; }
    PFN_vkGetInstanceProcAddr gipa = (PFN_vkGetInstanceProcAddr)dlsym(lib, "vkGetInstanceProcAddr");
    PFN_vkEnumerateInstanceVersion eiv =
        gipa ? (PFN_vkEnumerateInstanceVersion)gipa(NULL, "vkEnumerateInstanceVersion") : NULL;
    PFN_vkCreateInstance ci = gipa ? (PFN_vkCreateInstance)gipa(NULL, "vkCreateInstance") : NULL;
    if (!ci) { put(&o, "System libvulkan.so has no vkCreateInstance\n"); dlclose(lib); return o.buf; }

    uint32_t inst_version = VK_API_VERSION_1_0;
    if (eiv) eiv(&inst_version);
    put(&o, "Loader                  Vulkan %u.%u.%u\n", VK_API_VERSION_MAJOR(inst_version),
        VK_API_VERSION_MINOR(inst_version), VK_API_VERSION_PATCH(inst_version));
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "droiddeck-gpu-probe",
                             .apiVersion = inst_version >= VK_API_VERSION_1_3 ? VK_API_VERSION_1_3 : inst_version};
    VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
    VkInstance inst = VK_NULL_HANDLE;
    VkResult r = ci(&ici, NULL, &inst);
    if (r != VK_SUCCESS) { put(&o, "vkCreateInstance failed (%d)\n", r); dlclose(lib); return o.buf; }

    PFN_vkEnumeratePhysicalDevices epd = (PFN_vkEnumeratePhysicalDevices)gipa(inst, "vkEnumeratePhysicalDevices");
    PFN_vkDestroyInstance di = (PFN_vkDestroyInstance)gipa(inst, "vkDestroyInstance");
    uint32_t n = 0;
    if (epd) epd(inst, &n, NULL);
    VkPhysicalDevice pds[4];
    if (n > 4) n = 4;
    if (epd && n) epd(inst, &n, pds);
    if (!n) put(&o, "No physical devices\n");
    for (uint32_t i = 0; i < n; i++) {
        if (i) put(&o, "\n");
        probe_device(&o, inst, gipa, pds[i], app.apiVersion);
    }
    if (di) di(inst, NULL);
    dlclose(lib);
    return o.buf;
}

JNIEXPORT jstring JNICALL
Java_com_droiddeck_launcher_wayland_WaylandCompositor_nativeProbeSystemVulkan(JNIEnv *env, jclass clazz) {
    (void)clazz;
    char *text = probe();
    jstring s = (*env)->NewStringUTF(env, text ? text : "probe ran out of memory\n");
    free(text);
    return s;
}
