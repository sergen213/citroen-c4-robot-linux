#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QLibrary>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QScreen>
#include <QSurface>
#include <QThread>
#include <QStringList>

#if defined(C4_LAUNCHER_VULKAN_HEADERS) && C4_LAUNCHER_VULKAN_HEADERS
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>
#endif

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

#if defined(C4_LAUNCHER_VULKAN_HEADERS) && C4_LAUNCHER_VULKAN_HEADERS

QString apiVersion(uint32_t version)
{
    return QStringLiteral("%1.%2.%3")
        .arg(VK_API_VERSION_MAJOR(version))
        .arg(VK_API_VERSION_MINOR(version))
        .arg(VK_API_VERSION_PATCH(version));
}

QString vulkanError(const char *operation, VkResult result)
{
    return QStringLiteral("%1 failed (VkResult %2)")
        .arg(QString::fromLatin1(operation)).arg(static_cast<int>(result));
}

template<typename T, typename Enumerate>
VkResult enumerate(std::vector<T> &values, Enumerate call)
{
    // Bound retries if a driver keeps changing its enumeration results.
    for (int attempt = 0; attempt < 4; ++attempt) {
        uint32_t count = 0;
        VkResult result = call(&count, nullptr);
        if (result != VK_SUCCESS && result != VK_INCOMPLETE)
            return result;
        values.resize(count);
        if (count == 0)
            return result;
        result = call(&count, values.data());
        if (result == VK_SUCCESS) {
            values.resize(count);
            return result;
        }
        if (result != VK_INCOMPLETE)
            return result;
    }
    values.clear();
    return VK_INCOMPLETE;
}

bool hasExtension(const std::vector<VkExtensionProperties> &extensions, const char *name)
{
    return std::any_of(extensions.begin(), extensions.end(), [name](const auto &extension) {
        return std::strcmp(extension.extensionName, name) == 0;
    });
}

struct Instance {
    QLibrary loader;
    VkInstance handle = VK_NULL_HANDLE;
    PFN_vkDestroyInstance destroy = nullptr;
    ~Instance()
    {
        if (handle != VK_NULL_HANDLE)
            destroy(handle, nullptr);
        loader.unload();
    }
};

QJsonObject probeVulkan()
{
    QJsonArray errors;
    QJsonObject report{{"available", false}, {"devices", QJsonArray{}}};
    const auto finish = [&]() {
        report.insert("errors", errors);
        return report;
    };

    // Absolute distribution paths avoid loading a library from the working directory.
    Instance instance;
    QLibrary &loader = instance.loader;
    QStringList loaderPaths;
#if defined(Q_OS_LINUX)
#if defined(__x86_64__)
    loaderPaths << QStringLiteral("/usr/lib/x86_64-linux-gnu/libvulkan.so.1")
                << QStringLiteral("/lib/x86_64-linux-gnu/libvulkan.so.1");
#elif defined(__aarch64__)
    loaderPaths << QStringLiteral("/usr/lib/aarch64-linux-gnu/libvulkan.so.1")
                << QStringLiteral("/lib/aarch64-linux-gnu/libvulkan.so.1");
#elif defined(__i386__)
    loaderPaths << QStringLiteral("/usr/lib/i386-linux-gnu/libvulkan.so.1")
                << QStringLiteral("/lib/i386-linux-gnu/libvulkan.so.1");
#elif defined(__arm__)
    loaderPaths << QStringLiteral("/usr/lib/arm-linux-gnueabihf/libvulkan.so.1")
                << QStringLiteral("/lib/arm-linux-gnueabihf/libvulkan.so.1")
                << QStringLiteral("/usr/lib/arm-linux-gnueabi/libvulkan.so.1")
                << QStringLiteral("/lib/arm-linux-gnueabi/libvulkan.so.1");
#endif
    loaderPaths << QStringLiteral("/usr/lib64/libvulkan.so.1")
                << QStringLiteral("/lib64/libvulkan.so.1")
                << QStringLiteral("/usr/lib/libvulkan.so.1")
                << QStringLiteral("/lib/libvulkan.so.1");
#else
    errors.append(QStringLiteral("System Vulkan loader discovery is unavailable on this platform."));
    return finish();
#endif
    for (const QString &path : loaderPaths) {
        loader.setFileName(path);
        if (loader.load())
            break;
    }
    if (!loader.isLoaded()) {
        errors.append(QStringLiteral("System Vulkan loader libvulkan.so.1 is unavailable; install the distribution Vulkan loader and GPU Vulkan driver. %1")
                          .arg(loader.errorString()));
        return finish();
    }
    const auto getInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
        loader.resolve("vkGetInstanceProcAddr"));
    const auto cleanupInstance = reinterpret_cast<PFN_vkDestroyInstance>(
        loader.resolve("vkDestroyInstance"));
    if (!getInstanceProcAddr || !cleanupInstance) {
        errors.append(QStringLiteral("System Vulkan loader lacks vkGetInstanceProcAddr or vkDestroyInstance; repair the distribution Vulkan loader."));
        return finish();
    }
    const auto createInstance = reinterpret_cast<PFN_vkCreateInstance>(
        getInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance"));
    const auto enumerateInstanceExtensions = reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
        getInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties"));
    if (!createInstance || !enumerateInstanceExtensions) {
        errors.append(QStringLiteral("System Vulkan loader lacks vkCreateInstance or vkEnumerateInstanceExtensionProperties; repair the distribution Vulkan loader."));
        return finish();
    }

    uint32_t loaderVersion = VK_API_VERSION_1_0;
    const auto enumerateVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
        getInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"));
    if (enumerateVersion) {
        const VkResult result = enumerateVersion(&loaderVersion);
        if (result != VK_SUCCESS) {
            errors.append(vulkanError("vkEnumerateInstanceVersion", result));
            return finish();
        }
    }
    const uint32_t instanceVersion = std::min(loaderVersion, VK_API_VERSION_1_1);
    report.insert("loaderApiVersion", apiVersion(loaderVersion));
    report.insert("instanceApiVersion", apiVersion(instanceVersion));

    std::vector<VkExtensionProperties> instanceExtensions;
    VkResult result = enumerate(instanceExtensions, [&](uint32_t *count, VkExtensionProperties *data) {
        return enumerateInstanceExtensions(nullptr, count, data);
    });
    if (result != VK_SUCCESS)
        errors.append(vulkanError("vkEnumerateInstanceExtensionProperties", result));

    const char *propertiesExtension = VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME;
    const bool enablePropertiesExtension = instanceVersion < VK_API_VERSION_1_1
        && result == VK_SUCCESS && hasExtension(instanceExtensions, propertiesExtension);
    VkApplicationInfo application{};
    application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application.pApplicationName = "Citroen C4 Robot hardware probe";
    application.apiVersion = instanceVersion;
    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &application;
    if (enablePropertiesExtension) {
        createInfo.enabledExtensionCount = 1;
        createInfo.ppEnabledExtensionNames = &propertiesExtension;
    }
    instance.destroy = cleanupInstance;
    result = createInstance(&createInfo, nullptr, &instance.handle);
    if (result != VK_SUCCESS) {
        errors.append(vulkanError("vkCreateInstance", result));
        return finish();
    }
    const auto destroyInstance = reinterpret_cast<PFN_vkDestroyInstance>(
        getInstanceProcAddr(instance.handle, "vkDestroyInstance"));
    if (!destroyInstance) {
        errors.append(QStringLiteral("Vulkan instance lacks vkDestroyInstance; repair the distribution Vulkan loader."));
        return finish();
    }
    instance.destroy = destroyInstance;
    const auto enumeratePhysicalDevices = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
        getInstanceProcAddr(instance.handle, "vkEnumeratePhysicalDevices"));
    const auto getProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
        getInstanceProcAddr(instance.handle, "vkGetPhysicalDeviceProperties"));
    const auto getMemoryProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(
        getInstanceProcAddr(instance.handle, "vkGetPhysicalDeviceMemoryProperties"));
    const auto enumerateDeviceExtensions = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
        getInstanceProcAddr(instance.handle, "vkEnumerateDeviceExtensionProperties"));
    if (!enumeratePhysicalDevices || !getProperties || !getMemoryProperties || !enumerateDeviceExtensions) {
        errors.append(QStringLiteral("Vulkan instance lacks required physical-device query entry points; repair the Vulkan loader and GPU Vulkan driver."));
        return finish();
    }

    PFN_vkGetPhysicalDeviceFeatures2 getFeatures = nullptr;
    if (instanceVersion >= VK_API_VERSION_1_1) {
        getFeatures = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
            getInstanceProcAddr(instance.handle, "vkGetPhysicalDeviceFeatures2"));
    } else if (enablePropertiesExtension) {
        getFeatures = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
            getInstanceProcAddr(instance.handle, "vkGetPhysicalDeviceFeatures2KHR"));
    }

    std::vector<VkPhysicalDevice> devices;
    result = enumerate(devices, [&](uint32_t *count, VkPhysicalDevice *data) {
        return enumeratePhysicalDevices(instance.handle, count, data);
    });
    if (result != VK_SUCCESS) {
        errors.append(vulkanError("vkEnumeratePhysicalDevices", result));
        return finish();
    }
    if (devices.empty())
        errors.append(QStringLiteral("No Vulkan physical devices were found; install or enable the GPU Vulkan driver."));
    report.insert("available", !devices.empty());

    QJsonArray deviceReports;
    for (VkPhysicalDevice device : devices) {
        VkPhysicalDeviceProperties properties{};
        getProperties(device, &properties);
        VkPhysicalDeviceMemoryProperties memory{};
        getMemoryProperties(device, &memory);
        uint64_t localMemory = 0;
        for (uint32_t index = 0; index < memory.memoryHeapCount; ++index) {
            if ((memory.memoryHeaps[index].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0)
                localMemory += memory.memoryHeaps[index].size;
        }
        QJsonArray deviceErrors;
        std::vector<VkExtensionProperties> extensions;
        result = enumerate(extensions, [&](uint32_t *count, VkExtensionProperties *data) {
            return enumerateDeviceExtensions(device, nullptr, count, data);
        });
        QJsonArray extensionNames;
        if (result != VK_SUCCESS) {
            deviceErrors.append(vulkanError("vkEnumerateDeviceExtensionProperties", result));
            extensions.clear();
        } else {
            for (const auto &extension : extensions)
                extensionNames.append(QString::fromLatin1(extension.extensionName));
        }
        const bool accelerationExtension = hasExtension(extensions, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
        const bool rayTracingExtension = hasExtension(extensions, VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
        QJsonObject features{
            {"accelerationStructureExtension", result == VK_SUCCESS ? QJsonValue(accelerationExtension) : QJsonValue{}},
            {"rayTracingPipelineExtension", result == VK_SUCCESS ? QJsonValue(rayTracingExtension) : QJsonValue{}},
            {"queryAvailable", getFeatures != nullptr},
            {"accelerationStructure", result == VK_SUCCESS && !accelerationExtension ? QJsonValue(false) : QJsonValue{}},
            {"rayTracingPipeline", result == VK_SUCCESS && !rayTracingExtension ? QJsonValue(false) : QJsonValue{}}
        };
        if (getFeatures && (accelerationExtension || rayTracingExtension)) {
            VkPhysicalDeviceFeatures2 queried{};
            queried.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
            VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration{};
            acceleration.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
            VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracing{};
            rayTracing.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR;
            if (accelerationExtension) {
                acceleration.pNext = queried.pNext;
                queried.pNext = &acceleration;
            }
            if (rayTracingExtension) {
                rayTracing.pNext = queried.pNext;
                queried.pNext = &rayTracing;
            }
            getFeatures(device, &queried);
            if (accelerationExtension)
                features.insert("accelerationStructure", acceleration.accelerationStructure == VK_TRUE);
            if (rayTracingExtension)
                features.insert("rayTracingPipeline", rayTracing.rayTracingPipeline == VK_TRUE);
        } else if (!getFeatures && (accelerationExtension || rayTracingExtension)) {
            deviceErrors.append(QStringLiteral("Extended Vulkan feature queries are unavailable; ray tracing features are unknown."));
        }
        deviceReports.append(QJsonObject{
            {"name", QString::fromUtf8(properties.deviceName)},
            {"apiVersion", apiVersion(properties.apiVersion)},
            {"apiVersionRaw", static_cast<qint64>(properties.apiVersion)},
            {"driverVersionRaw", static_cast<qint64>(properties.driverVersion)},
            {"vendorId", static_cast<qint64>(properties.vendorID)},
            {"deviceId", static_cast<qint64>(properties.deviceID)},
            {"deviceType", static_cast<int>(properties.deviceType)},
            {"deviceLocalMemoryBytes", static_cast<qint64>(localMemory)},
            {"extensions", extensionNames}, {"features", features}, {"errors", deviceErrors}
        });
    }
    report.insert("devices", deviceReports);
    return finish();
}

#else

QJsonObject probeVulkan()
{
    return QJsonObject{
        {"available", false},
        {"devices", QJsonArray{}},
        {"errors", QJsonArray{QStringLiteral("Vulkan hardware queries are unavailable: this launcher was built without Vulkan SDK headers. Rebuild with Vulkan headers to enable optional Vulkan probing.")}}
    };
}

#endif

QJsonObject probeOpenGL()
{
    QJsonArray errors;
    QJsonObject report{{"available", false}};
    const auto finish = [&]() {
        report.insert("errors", errors);
        return report;
    };
    auto *application = qobject_cast<QGuiApplication *>(QGuiApplication::instance());
    if (!application || QThread::currentThread() != application->thread()) {
        errors.append(QStringLiteral("OpenGL probing requires a QGuiApplication on its GUI thread."));
        return finish();
    }
    QOpenGLContext context;
    if (!context.create()) {
        errors.append(QStringLiteral("Could not create an OpenGL context."));
        return finish();
    }
    QOffscreenSurface surface;
    surface.setFormat(context.format());
    surface.create();
    if (!surface.isValid()) {
        errors.append(QStringLiteral("Could not create an OpenGL offscreen surface."));
        return finish();
    }
    QOpenGLContext *previousContext = QOpenGLContext::currentContext();
    QSurface *previousSurface = previousContext ? previousContext->surface() : nullptr;
    if (!context.makeCurrent(&surface)) {
        errors.append(QStringLiteral("Could not make the offscreen OpenGL context current."));
        if (previousContext && previousSurface && !previousContext->makeCurrent(previousSurface))
            errors.append(QStringLiteral("Could not restore the previous OpenGL context."));
        return finish();
    }
    QOpenGLFunctions *functions = context.functions();
    if (functions) {
        const auto addString = [&](const char *key, GLenum name) {
            const auto *value = functions->glGetString(name);
            if (value)
                report.insert(key, QString::fromLatin1(reinterpret_cast<const char *>(value)));
            else
                errors.append(QStringLiteral("OpenGL did not provide %1.").arg(QString::fromLatin1(key)));
        };
        addString("vendor", GL_VENDOR);
        addString("renderer", GL_RENDERER);
        addString("version", GL_VERSION);
        report.insert("available", report.contains("version"));
        report.insert("openGLES", context.isOpenGLES());
    } else {
        errors.append(QStringLiteral("OpenGL function access is unavailable."));
    }
    context.doneCurrent();
    if (previousContext && previousSurface && !previousContext->makeCurrent(previousSurface))
        errors.append(QStringLiteral("Could not restore the previous OpenGL context."));
    return finish();
}

} // namespace

QJsonObject probeHardware()
{
    QJsonArray errors;
    QJsonArray displays;
    auto *application = qobject_cast<QGuiApplication *>(QGuiApplication::instance());
    const QString platform = application ? QGuiApplication::platformName() : QString{};
    const bool virtualPlatform = platform == QStringLiteral("offscreen")
        || platform.startsWith(QStringLiteral("minimal"));
    if (application && QThread::currentThread() == application->thread()) {
        for (QScreen *screen : QGuiApplication::screens()) {
            const QRect geometry = screen->geometry();
            displays.append(QJsonObject{
                {"name", screen->name()}, {"x", geometry.x()}, {"y", geometry.y()},
                {"width", geometry.width()}, {"height", geometry.height()},
                {"refreshRateHz", screen->refreshRate()}, {"devicePixelRatio", screen->devicePixelRatio()},
                {"virtual", virtualPlatform ? QJsonValue(true) : QJsonValue{}},
                {"physicalDisplayEvidence", false}
            });
        }
        if (displays.isEmpty())
            errors.append(QStringLiteral("Qt exposes no connected screens."));
        if (virtualPlatform)
            errors.append(QStringLiteral("Qt uses a virtual platform; exposed screen geometry is not physical display or mode evidence."));
    } else {
        errors.append(QStringLiteral("Display probing requires a QGuiApplication on its GUI thread."));
    }
    return QJsonObject{
        {"qtPlatform", platform},
        {"vulkan", probeVulkan()}, {"opengl", probeOpenGL()}, {"displays", displays},
        {"runtimeImplementation", QJsonObject{
            {"probed", false},
            {"authority", "Selected native ELF --graphics-capabilities; hardware extensions are not implementation evidence"}
        }},
        {"errors", errors}
    };
}
