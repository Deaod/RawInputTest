#include <iostream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <atomic>
#include <array>
#include <string>
#include <algorithm>

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "types.hpp"
#include "best_effort_logger.hpp"
#include "log_utils.hpp"
#include "spsc_ring_buffer.hpp"
#include "cpuid.hpp"
#include "simd_primitives.hpp"
#include <utility>

#include <vulkan/vulkan.h>
#include "vulkan_utils.hpp"

#pragma warning(push)
#pragma warning(disable : 4201)
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#pragma warning(pop)

LRESULT CALLBACK input_wndproc(HWND, UINT, WPARAM, LPARAM);

void input_thread_fn() {
    threads::current::assign_id();
    belog::enable_logging();

    VkInstance instance;
    {
        VkApplicationInfo appInfo = {};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "RawInputTest";
        appInfo.pEngineName = "RawInputTest";
        appInfo.apiVersion = VK_API_VERSION_1_1;

        const char* instanceExtensions[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME };

        VkInstanceCreateInfo instanceCreateInfo = {};
        instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        instanceCreateInfo.pNext = nullptr;
        instanceCreateInfo.pApplicationInfo = &appInfo;

        instanceCreateInfo.enabledExtensionCount = u32(sizeof(instanceExtensions) / sizeof(*instanceExtensions));
        instanceCreateInfo.ppEnabledExtensionNames = instanceExtensions;

        VK_ON_FAIL_RETURN_VOID(vkCreateInstance(&instanceCreateInfo, nullptr, &instance));
    }

    VkPhysicalDevice physicalDevice;
    {
        u32 gpuCount = 0;
        VK_ON_FAIL_RETURN_VOID(vkEnumeratePhysicalDevices(instance, &gpuCount, nullptr));

        std::vector<VkPhysicalDevice> physicalDevices(gpuCount);
        VK_ON_FAIL_RETURN_VOID(vkEnumeratePhysicalDevices(instance, &gpuCount, physicalDevices.data()));

        physicalDevice = physicalDevices[0];
    }

    VkPhysicalDeviceProperties deviceProperties;
    vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);
    VkPhysicalDeviceFeatures deviceFeatures;
    vkGetPhysicalDeviceFeatures(physicalDevice, &deviceFeatures);
    VkPhysicalDeviceMemoryProperties deviceMemoryProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &deviceMemoryProperties);

    auto getMemoryTypeIndex = [&deviceMemoryProperties](u32 typeBits, VkMemoryPropertyFlags properties) {
        for (u32 i = 0; i < deviceMemoryProperties.memoryTypeCount; i++) {
            if ((typeBits & 1) == 1) {
                if ((deviceMemoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                    return i;
                }
            }
            typeBits >>= 1;
        }

        return ~u32(0);
    };

    VkPhysicalDeviceFeatures enabledFeatures{};

    u32 queueFamilyCount;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilyProperties(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilyProperties.data());

    std::vector<std::string> supportedExtensions;
    {
        u32 extCount = 0;
        vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, nullptr);
        if (extCount > 0) {
            std::vector<VkExtensionProperties> extensions(extCount);
            if (vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount, &extensions.front()) == VK_SUCCESS) {
                for (auto ext : extensions) {
                    supportedExtensions.push_back(ext.extensionName);
                }
            }
        }
    }

    std::vector<VkDeviceQueueCreateInfo> queueCreateInfos{};
    const float defaultQueuePriority(0.0f);

    struct {
        u32 graphics;
        u32 compute;
        u32 transfer;
    } queueFamilyIndices = { ~u32(0), ~u32(0), ~u32(0) };
    VkDevice device;

    {
        for (u32 i = 0; i < u32(queueFamilyProperties.size()); i++) {
            if (queueFamilyProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                queueFamilyIndices.graphics = i;
                break;
            }
        }

        ON_FAIL_TRACE_RETURN_VOID(queueFamilyIndices.graphics != ~u32(0), "Failed to find graphics queue.");

        VkDeviceQueueCreateInfo queueInfo{};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = queueFamilyIndices.graphics;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &defaultQueuePriority;
        queueCreateInfos.push_back(queueInfo);

        for (u32 i = 0; i < u32(queueFamilyProperties.size()); i++) {
            if ((queueFamilyProperties[i].queueFlags & VK_QUEUE_COMPUTE_BIT) && ((queueFamilyProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0)) {
                queueFamilyIndices.compute = i;
                break;
            }
        }

        if (queueFamilyIndices.compute == ~u32(0)) {
            for (u32 i = 0; i < u32(queueFamilyProperties.size()); i++) {
                if (queueFamilyProperties[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                    queueFamilyIndices.compute = i;
                    break;
                }
            }
        }

        ON_FAIL_TRACE_RETURN_VOID(queueFamilyIndices.compute != ~u32(0), "Failed to find compute queue.");

        if (queueFamilyIndices.compute != queueFamilyIndices.graphics) {
            queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueInfo.queueFamilyIndex = queueFamilyIndices.compute;
            queueInfo.queueCount = 1;
            queueInfo.pQueuePriorities = &defaultQueuePriority;
            queueCreateInfos.push_back(queueInfo);
        }

        queueFamilyIndices.transfer = queueFamilyIndices.graphics;

        const char* enabledExtensions[] = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };

        VkDeviceCreateInfo deviceCreateInfo = {};
        deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceCreateInfo.queueCreateInfoCount = u32(queueCreateInfos.size());;
        deviceCreateInfo.pQueueCreateInfos = queueCreateInfos.data();
        deviceCreateInfo.pEnabledFeatures = &enabledFeatures;
        deviceCreateInfo.enabledExtensionCount = u32(sizeof(enabledExtensions) / sizeof(*enabledExtensions));
        deviceCreateInfo.ppEnabledExtensionNames = enabledExtensions;

        VK_ON_FAIL_RETURN_VOID(vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device));
    }

    VkFormat depthFormat = VK_FORMAT_MAX_ENUM;
    {
        VkFormat depthFormats[] = {
            VK_FORMAT_D32_SFLOAT_S8_UINT,
            VK_FORMAT_D32_SFLOAT,
            VK_FORMAT_D24_UNORM_S8_UINT,
            VK_FORMAT_D16_UNORM_S8_UINT,
            VK_FORMAT_D16_UNORM
        };

        for (auto& format : depthFormats) {
            VkFormatProperties formatProps;
            vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &formatProps);
            // Format must support depth stencil attachment for optimal tiling
            if (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
                depthFormat = format;
                break;
            }
        }
        ON_FAIL_TRACE_RETURN_VOID(
            depthFormat != VK_FORMAT_MAX_ENUM,
            "Failed to find appropriate depth format"
        );
    }

    [[maybe_unused]] auto vkGetPhysicalDeviceSurfaceSupportKHR = VK_GET_INSTANCE_PROC_ADDR(instance, GetPhysicalDeviceSurfaceSupportKHR);
    [[maybe_unused]] auto vkGetPhysicalDeviceSurfaceCapabilitiesKHR = VK_GET_INSTANCE_PROC_ADDR(instance, GetPhysicalDeviceSurfaceCapabilitiesKHR);
    [[maybe_unused]] auto vkGetPhysicalDeviceSurfaceFormatsKHR = VK_GET_INSTANCE_PROC_ADDR(instance, GetPhysicalDeviceSurfaceFormatsKHR);
    [[maybe_unused]] auto vkGetPhysicalDeviceSurfacePresentModesKHR = VK_GET_INSTANCE_PROC_ADDR(instance, GetPhysicalDeviceSurfacePresentModesKHR);

    [[maybe_unused]] auto vkCreateSwapchainKHR = VK_GET_DEVICE_PROC_ADDR(device, CreateSwapchainKHR);
    [[maybe_unused]] auto vkDestroySwapchainKHR = VK_GET_DEVICE_PROC_ADDR(device, DestroySwapchainKHR);
    [[maybe_unused]] auto vkGetSwapchainImagesKHR = VK_GET_DEVICE_PROC_ADDR(device, GetSwapchainImagesKHR);
    [[maybe_unused]] auto vkAcquireNextImageKHR = VK_GET_DEVICE_PROC_ADDR(device, AcquireNextImageKHR);
    [[maybe_unused]] auto vkQueuePresentKHR = VK_GET_DEVICE_PROC_ADDR(device, QueuePresentKHR);

    struct {
        // Swap chain image presentation
        VkSemaphore presentComplete;
        // Command buffer submission and execution
        VkSemaphore renderComplete;
        // UI overlay submission and execution
        VkSemaphore overlayComplete;
    } semaphores;

    {
        VkSemaphoreCreateInfo semaphoreCreateInfo{};
        semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        // Create a semaphore used to synchronize image presentation
        // Ensures that the image is displayed before we start submitting new commands to the queu
        VK_ON_FAIL_RETURN_VOID(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &semaphores.presentComplete));

        // Create a semaphore used to synchronize command submission
        // Ensures that the image is not presented until all commands have been sumbitted and executed
        VK_ON_FAIL_RETURN_VOID(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &semaphores.renderComplete));

        // Create a semaphore used to synchronize command submission
        // Ensures that the image is not presented until all commands for the UI overlay have been sumbitted and executed
        // Will be inserted after the render complete semaphore if the UI overlay is enabled
        VK_ON_FAIL_RETURN_VOID(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &semaphores.overlayComplete));
    }

    VkPipelineStageFlags submitPipelineStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.pWaitDstStageMask = &submitPipelineStages;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &semaphores.presentComplete;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &semaphores.renderComplete;

    HWND input_capture_window;
    WNDCLASSEX input_capture_class;

    auto module_handle = GetModuleHandle(nullptr);
    auto monitor_handle = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTONEAREST);
    auto background = CreateSolidBrush(RGB(0, 0, 0));

    input_capture_class.cbSize = sizeof(input_capture_class);
    input_capture_class.style = CS_HREDRAW | CS_VREDRAW;
    input_capture_class.lpfnWndProc = input_wndproc;
    input_capture_class.cbClsExtra = 0;
    input_capture_class.cbWndExtra = 0;
    input_capture_class.hInstance = module_handle;
    input_capture_class.hIcon = nullptr;
    input_capture_class.hCursor = nullptr;
    input_capture_class.hbrBackground = background;
    input_capture_class.lpszMenuName = nullptr;
    input_capture_class.lpszClassName = TEXT("InputCapture");
    input_capture_class.hIconSm = nullptr;
    auto input_capture_class_atom = RegisterClassEx(&input_capture_class);
    ON_FAIL_TRACE_RETURN_VOID(
        input_capture_class_atom,
        "failed to register class ", GetLastError()
    );

    MONITORINFOEX monitor_info = { sizeof(monitor_info) };
    ON_FAIL_TRACE_RETURN_VOID(
        GetMonitorInfo(monitor_handle, &monitor_info),
        "failed to query monitor info ", GetLastError()
    );

    input_capture_window = CreateWindow(
        TEXT("InputCapture"), // window class name
        TEXT("InputCapture"), // window caption
        WS_POPUP | WS_VISIBLE,// window style
        monitor_info.rcMonitor.left,                                // initial x position
        monitor_info.rcMonitor.top,                                 // initial y position
        monitor_info.rcMonitor.right - monitor_info.rcMonitor.left, // initial x size
        monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top, // initial y size
        nullptr,              // parent window handle
        nullptr,              // window menu handle
        module_handle,        // program instance handle
        nullptr               // creation parameters
    );
    ON_FAIL_TRACE_RETURN_VOID(
        input_capture_window != nullptr,
        "failed to create window ", GetLastError()
    );

    RAWINPUTDEVICE devices[2];

    devices[0].usUsagePage = 0x01;
    devices[0].usUsage = 0x02;
    devices[0].dwFlags = RIDEV_NOLEGACY;   // adds HID mouse and also ignores legacy mouse messages
    devices[0].hwndTarget = input_capture_window;

    devices[1].usUsagePage = 0x01;
    devices[1].usUsage = 0x06;
    devices[1].dwFlags = RIDEV_NOLEGACY;   // adds HID keyboard and also ignores legacy keyboard messages
    devices[1].hwndTarget = input_capture_window;

    ON_FAIL_TRACE_RETURN_VOID(
        RegisterRawInputDevices(devices, 2, sizeof(RAWINPUTDEVICE)),
        "failed to register for raw input ", GetLastError()
    );

    VkSurfaceKHR surface;

    VkWin32SurfaceCreateInfoKHR surfaceCreateInfo = {};
    surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surfaceCreateInfo.hinstance = module_handle;
    surfaceCreateInfo.hwnd = input_capture_window;
    VK_ON_FAIL_RETURN_VOID(vkCreateWin32SurfaceKHR(instance, &surfaceCreateInfo, nullptr, &surface));

    u32 queueNodeIndex = ~u32(0);

    for (u32 queueFamilyIndex = 0; queueFamilyIndex < queueFamilyCount; queueFamilyIndex++) {
        VkBool32 supportsPresent;
        vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, queueFamilyIndex, surface, &supportsPresent);
        if (((queueFamilyProperties[queueFamilyIndex].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) && supportsPresent) {
            queueNodeIndex = queueFamilyIndex;
        }
    }

    ON_FAIL_TRACE_RETURN_VOID(
        queueNodeIndex != ~u32(0),
        "Failed to find graphics/presenting queue"
    );

    uint32_t formatCount;
    VK_ON_FAIL_RETURN_VOID(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr));

    std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
    VK_ON_FAIL_RETURN_VOID(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, surfaceFormats.data()));

    VkFormat colorFormat = VK_FORMAT_MAX_ENUM;
    VkColorSpaceKHR colorSpace = VK_COLOR_SPACE_MAX_ENUM_KHR;
    {
        // If the surface format list only includes one entry with VK_FORMAT_UNDEFINED,
        // there is no preferered format, so we assume VK_FORMAT_B8G8R8A8_UNORM
        if ((formatCount == 1) && (surfaceFormats[0].format == VK_FORMAT_UNDEFINED)) {
            colorFormat = VK_FORMAT_B8G8R8A8_UNORM;
            colorSpace = surfaceFormats[0].colorSpace;
        } else {
            // iterate over the list of available surface format and
            // check for the presence of VK_FORMAT_B8G8R8A8_UNORM
            bool found_B8G8R8A8_UNORM = false;
            for (auto&& surfaceFormat : surfaceFormats) {
                if (surfaceFormat.format == VK_FORMAT_B8G8R8A8_UNORM) {
                    colorFormat = surfaceFormat.format;
                    colorSpace = surfaceFormat.colorSpace;
                    found_B8G8R8A8_UNORM = true;
                    break;
                }
            }

            // in case VK_FORMAT_B8G8R8A8_UNORM is not available
            // select the first available color format
            if (!found_B8G8R8A8_UNORM) {
                colorFormat = surfaceFormats[0].format;
                colorSpace = surfaceFormats[0].colorSpace;
            }
        }
    }

    VkQueue queue;
    vkGetDeviceQueue(device, queueNodeIndex, 0, &queue);

    VkCommandPoolCreateInfo cmdPoolInfo = {};
    cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cmdPoolInfo.queueFamilyIndex = queueNodeIndex;
    cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool commandPool;
    VK_ON_FAIL_RETURN_VOID(vkCreateCommandPool(device, &cmdPoolInfo, nullptr, &commandPool));

    auto getCommandBuffer = [&device, &commandPool](bool begin) -> VkCommandBuffer {
        VkCommandBuffer buffer;

        VkCommandBufferAllocateInfo cmdBufAllocateInfo = {};
        cmdBufAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdBufAllocateInfo.commandPool = commandPool;
        cmdBufAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdBufAllocateInfo.commandBufferCount = 1;

        VK_ON_FAIL_RETURN(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, &buffer), VK_NULL_HANDLE);

        if (begin) {
            VkCommandBufferBeginInfo cmdBufInfo{};
            cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            VK_ON_FAIL_RETURN(vkBeginCommandBuffer(buffer, &cmdBufInfo), VK_NULL_HANDLE);
        }

        return buffer;
    };

    auto flushCommandBuffer = [&device, &queue, &commandPool](VkCommandBuffer buffer) {
        VK_ON_FAIL_RELAY(vkEndCommandBuffer(buffer));

        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &buffer;

        // Create fence to ensure that the command buffer has finished executing
        VkFenceCreateInfo fenceCreateInfo = {};
        fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceCreateInfo.flags = 0;
        VkFence fence;
        VK_ON_FAIL_RELAY(vkCreateFence(device, &fenceCreateInfo, nullptr, &fence));

        // Submit to the queue
        VK_ON_FAIL_RELAY(vkQueueSubmit(queue, 1, &submitInfo, fence));
        // Wait for the fence to signal that command buffer has finished executing
        VK_ON_FAIL_RELAY(vkWaitForFences(device, 1, &fence, VK_TRUE, 100'000'000'000)); // 100 second timeout

        vkDestroyFence(device, fence, nullptr);
        vkFreeCommandBuffers(device, commandPool, 1, &buffer);

        return VK_SUCCESS;
    };

    RECT windowRect;
    ON_FAIL_TRACE_RETURN_VOID(GetWindowRect(input_capture_window, &windowRect), "Failed to get window rect: ", GetLastError());

    u32 width = windowRect.right - windowRect.left;
    u32 height = windowRect.bottom - windowRect.top;

    VkSurfaceCapabilitiesKHR surfCaps;
    VK_ON_FAIL_RETURN_VOID(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfCaps));

    u32 presentModeCount;
    VK_ON_FAIL_RETURN_VOID(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr));

    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    VK_ON_FAIL_RETURN_VOID(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, presentModes.data()));

    /** @brief Handle to the current swap chain, required for recreation */
    VkSwapchainKHR swapChain;
    {
        VkExtent2D swapchainExtent = {};
        // If width (and height) equals the special value 0xFFFFFFFF, the size of the surface will be set by the swapchain
        if (surfCaps.currentExtent.width == ~u32(0)) {
            // If the surface size is undefined, the size is set to
            // the size of the images requested.
            swapchainExtent.width = width;
            swapchainExtent.height = height;
        } else {
            // If the surface size is defined, the swap chain size must match
            swapchainExtent = surfCaps.currentExtent;
            width = surfCaps.currentExtent.width;
            height = surfCaps.currentExtent.height;
        }

        // The VK_PRESENT_MODE_FIFO_KHR mode must always be present as per spec
        // This mode waits for the vertical blank ("v-sync")
        VkPresentModeKHR swapchainPresentMode = VK_PRESENT_MODE_FIFO_KHR;
        // Try to find a immediate mode, use mailbox mode as fallback.
        // It's the lowest latency present mode available
        for (size_t i = 0; i < presentModeCount; i++) {
            if (presentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR) {
                swapchainPresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
            }
            if (presentModes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) {
                swapchainPresentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
                break;
            }
        }

        u32 desiredNumberOfSwapchainImages = surfCaps.minImageCount + 1;
        if (surfCaps.maxImageCount > 0) {
            desiredNumberOfSwapchainImages = std::min(desiredNumberOfSwapchainImages, surfCaps.maxImageCount);
        }

        // Find the transformation of the surface
        VkSurfaceTransformFlagsKHR preTransform;
        if (surfCaps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) {
            // We prefer a non-rotated transform
            preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        } else {
            preTransform = surfCaps.currentTransform;
        }

        // Find a supported composite alpha format (not all devices support alpha opaque)
        VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        // Simply select the first composite alpha format available
        VkCompositeAlphaFlagBitsKHR compositeAlphaFlags[] = {
            VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
        };
        for (auto& compositeAlphaFlag : compositeAlphaFlags) {
            if (surfCaps.supportedCompositeAlpha & compositeAlphaFlag) {
                compositeAlpha = compositeAlphaFlag;
                break;
            }
        }

        VkSwapchainCreateInfoKHR swapchainCI = {};
        swapchainCI.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapchainCI.pNext = nullptr;
        swapchainCI.surface = surface;
        swapchainCI.minImageCount = desiredNumberOfSwapchainImages;
        swapchainCI.imageFormat = colorFormat;
        swapchainCI.imageColorSpace = colorSpace;
        swapchainCI.imageExtent = swapchainExtent;
        swapchainCI.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        swapchainCI.preTransform = (VkSurfaceTransformFlagBitsKHR) preTransform;
        swapchainCI.imageArrayLayers = 1;
        swapchainCI.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        swapchainCI.queueFamilyIndexCount = 0;
        swapchainCI.pQueueFamilyIndices = nullptr;
        swapchainCI.presentMode = swapchainPresentMode;
        swapchainCI.oldSwapchain = nullptr;
        // Setting clipped to VK_TRUE allows the implementation to discard rendering outside of the surface area
        swapchainCI.clipped = VK_TRUE;
        swapchainCI.compositeAlpha = compositeAlpha;

        // Set additional usage flag for blitting from the swapchain images if supported
        VkFormatProperties formatProps;
        vkGetPhysicalDeviceFormatProperties(physicalDevice, colorFormat, &formatProps);
        if ((formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT_KHR) || (formatProps.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT)) {
            swapchainCI.imageUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        }

        VK_ON_FAIL_RETURN_VOID(vkCreateSwapchainKHR(device, &swapchainCI, nullptr, &swapChain));
    }

    u32 imageCount;
    VK_ON_FAIL_RETURN_VOID(vkGetSwapchainImagesKHR(device, swapChain, &imageCount, nullptr));
    std::vector<VkImage> images(imageCount);
    VK_ON_FAIL_RETURN_VOID(vkGetSwapchainImagesKHR(device, swapChain, &imageCount, images.data()));
    struct SwapChainBuffer {
        VkImage image;
        VkImageView view;
    };
    std::vector<SwapChainBuffer> buffers(imageCount);
    {
        for (uint32_t i = 0; i < imageCount; i++) {
            VkImageViewCreateInfo colorAttachmentView = {};
            colorAttachmentView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            colorAttachmentView.pNext = nullptr;
            colorAttachmentView.format = colorFormat;
            colorAttachmentView.components = {
                VK_COMPONENT_SWIZZLE_R,
                VK_COMPONENT_SWIZZLE_G,
                VK_COMPONENT_SWIZZLE_B,
                VK_COMPONENT_SWIZZLE_A
            };
            colorAttachmentView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            colorAttachmentView.subresourceRange.baseMipLevel = 0;
            colorAttachmentView.subresourceRange.levelCount = 1;
            colorAttachmentView.subresourceRange.baseArrayLayer = 0;
            colorAttachmentView.subresourceRange.layerCount = 1;
            colorAttachmentView.viewType = VK_IMAGE_VIEW_TYPE_2D;
            colorAttachmentView.flags = 0;

            buffers[i].image = images[i];

            colorAttachmentView.image = buffers[i].image;

            VK_ON_FAIL_RETURN_VOID(vkCreateImageView(device, &colorAttachmentView, nullptr, &buffers[i].view));
        }
    }

    std::vector<VkCommandBuffer> drawCmdBuffers(imageCount);
    {
        VkCommandBufferAllocateInfo cmdBufAllocateInfo = {};
        cmdBufAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdBufAllocateInfo.commandPool = commandPool;
        cmdBufAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdBufAllocateInfo.commandBufferCount = imageCount;
        VK_ON_FAIL_RETURN_VOID(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, drawCmdBuffers.data()));
    }

    struct {
        VkImage image;
        VkDeviceMemory mem;
        VkImageView view;
    } depthStencil;

    {
        VkImageCreateInfo image = {};
        image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image.pNext = nullptr;
        image.imageType = VK_IMAGE_TYPE_2D;
        image.format = depthFormat;
        image.extent = { width, height, 1 };
        image.mipLevels = 1;
        image.arrayLayers = 1;
        image.samples = VK_SAMPLE_COUNT_1_BIT;
        image.tiling = VK_IMAGE_TILING_OPTIMAL;
        image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        image.flags = 0;

        VkMemoryAllocateInfo mem_alloc = {};
        mem_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mem_alloc.pNext = nullptr;
        mem_alloc.allocationSize = 0;
        mem_alloc.memoryTypeIndex = 0;

        VkImageViewCreateInfo depthStencilView = {};
        depthStencilView.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        depthStencilView.pNext = nullptr;
        depthStencilView.viewType = VK_IMAGE_VIEW_TYPE_2D;
        depthStencilView.format = depthFormat;
        depthStencilView.flags = 0;
        depthStencilView.subresourceRange = {};
        depthStencilView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        depthStencilView.subresourceRange.baseMipLevel = 0;
        depthStencilView.subresourceRange.levelCount = 1;
        depthStencilView.subresourceRange.baseArrayLayer = 0;
        depthStencilView.subresourceRange.layerCount = 1;

        VkMemoryRequirements memReqs;

        VK_ON_FAIL_RETURN_VOID(vkCreateImage(device, &image, nullptr, &depthStencil.image));
        vkGetImageMemoryRequirements(device, depthStencil.image, &memReqs);
        mem_alloc.allocationSize = memReqs.size;
        mem_alloc.memoryTypeIndex = getMemoryTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        VK_ON_FAIL_RETURN_VOID(vkAllocateMemory(device, &mem_alloc, nullptr, &depthStencil.mem));
        VK_ON_FAIL_RETURN_VOID(vkBindImageMemory(device, depthStencil.image, depthStencil.mem, 0));

        depthStencilView.image = depthStencil.image;
        VK_ON_FAIL_RETURN_VOID(vkCreateImageView(device, &depthStencilView, nullptr, &depthStencil.view));
    }

    VkRenderPass renderPass;
    {
        std::array<VkAttachmentDescription, 2> attachments = {};
        // Color attachment
        attachments[0].format = colorFormat;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        // Depth attachment
        attachments[1].format = depthFormat;
        attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorReference = {};
        colorReference.attachment = 0;
        colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthReference = {};
        depthReference.attachment = 1;
        depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkSubpassDescription subpassDescription = {};
        subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpassDescription.colorAttachmentCount = 1;
        subpassDescription.pColorAttachments = &colorReference;
        subpassDescription.pDepthStencilAttachment = &depthReference;
        subpassDescription.inputAttachmentCount = 0;
        subpassDescription.pInputAttachments = nullptr;
        subpassDescription.preserveAttachmentCount = 0;
        subpassDescription.pPreserveAttachments = nullptr;
        subpassDescription.pResolveAttachments = nullptr;

        // Subpass dependencies for layout transitions
        std::array<VkSubpassDependency, 2> dependencies;

        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

        VkRenderPassCreateInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpassDescription;
        renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
        renderPassInfo.pDependencies = dependencies.data();

        VK_ON_FAIL_RETURN_VOID(vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass));
    }

    VkPipelineCache pipelineCache;
    {
        VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
        pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        VK_ON_FAIL_RETURN_VOID(vkCreatePipelineCache(device, &pipelineCacheCreateInfo, nullptr, &pipelineCache));
    }

    std::vector<VkFramebuffer> frameBuffers(imageCount);
    {
        VkImageView attachments[2];

        // Depth/Stencil attachment is the same for all frame buffers
        attachments[1] = depthStencil.view;

        VkFramebufferCreateInfo frameBufferCreateInfo = {};
        frameBufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        frameBufferCreateInfo.pNext = nullptr;
        frameBufferCreateInfo.renderPass = renderPass;
        frameBufferCreateInfo.attachmentCount = 2;
        frameBufferCreateInfo.pAttachments = attachments;
        frameBufferCreateInfo.width = width;
        frameBufferCreateInfo.height = height;
        frameBufferCreateInfo.layers = 1;

        // Create frame buffers for every swap chain image

        for (uint32_t i = 0; i < frameBuffers.size(); i++) {
            attachments[0] = buffers[i].view;
            VK_ON_FAIL_RETURN_VOID(vkCreateFramebuffer(device, &frameBufferCreateInfo, nullptr, &frameBuffers[i]));
        }
    }

    // Semaphores
    // Used to coordinate operations within the graphics queue and ensure correct command ordering
    VkSemaphore presentCompleteSemaphore;
    VkSemaphore renderCompleteSemaphore;
    // Fences
    // Used to check the completion of queue operations (e.g. command buffer execution)
    std::vector<VkFence> waitFences(imageCount);
    {
        // Semaphores (Used for correct command ordering)
        VkSemaphoreCreateInfo semaphoreCreateInfo = {};
        semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        semaphoreCreateInfo.pNext = nullptr;

        // Semaphore used to ensures that image presentation is complete before starting to submit again
        VK_ON_FAIL_RETURN_VOID(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &presentCompleteSemaphore));

        // Semaphore used to ensures that all commands submitted have been finished before submitting the image to the queue
        VK_ON_FAIL_RETURN_VOID(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &renderCompleteSemaphore));

        // Fences (Used to check draw command buffer completion)
        VkFenceCreateInfo fenceCreateInfo = {};
        fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        // Create in signaled state so we don't wait on first render of each command buffer
        fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for (auto& fence : waitFences) {
            VK_ON_FAIL_RETURN_VOID(vkCreateFence(device, &fenceCreateInfo, nullptr, &fence));
        }
    }

    struct Vertex {
        float position[3];
        float color[3];
    };

    struct {
        VkDeviceMemory memory; // Handle to the device memory for this buffer
        VkBuffer buffer;       // Handle to the Vulkan buffer object that the memory is bound to
    } vertices;

    struct {
        VkDeviceMemory memory;
        VkBuffer buffer;
        u32 count;
    } indices;

    // Static data like vertex and index buffer should be stored on the device memory 
    // for optimal (and fastest) access by the GPU
    //
    // To achieve this we use so-called "staging buffers" :
    // - Create a buffer that's visible to the host (and can be mapped)
    // - Copy the data to this buffer
    // - Create another buffer that's local on the device (VRAM) with the same size
    // - Copy the data from the host to the device using a command buffer
    // - Delete the host visible (staging) buffer
    // - Use the device local buffers for rendering

    struct StagingBuffer {
        VkDeviceMemory memory;
        VkBuffer buffer;
    };

    struct {
        StagingBuffer vertices;
        StagingBuffer indices;
    } stagingBuffers;

    {
        // Setup vertices
        std::vector<Vertex> vertexBuffer = {
            { {  1.0f,  1.0f, 0.0f }, { 1.0f, 0.0f, 0.0f } },
            { { -1.0f,  1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } },
            { {  0.0f, -1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f } }
        };
        u32 vertexBufferSize = u32(vertexBuffer.size() * sizeof(Vertex));

        // Setup indices
        std::vector<u32> indexBuffer = { 0, 1, 2 };
        indices.count = u32(indexBuffer.size());
        u32 indexBufferSize = u32(indices.count * sizeof(u32));

        VkMemoryAllocateInfo memAlloc = {};
        memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        VkMemoryRequirements memReqs;

        void *data;

        // Vertex buffer
        VkBufferCreateInfo vertexBufferInfo = {};
        vertexBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        vertexBufferInfo.size = vertexBufferSize;
        // Buffer is used as the copy source
        vertexBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        // Create a host-visible buffer to copy the vertex data to (staging buffer)
        VK_ON_FAIL_RETURN_VOID(vkCreateBuffer(device, &vertexBufferInfo, nullptr, &stagingBuffers.vertices.buffer));
        vkGetBufferMemoryRequirements(device, stagingBuffers.vertices.buffer, &memReqs);
        memAlloc.allocationSize = memReqs.size;
        // Request a host visible memory type that can be used to copy our data do
        // Also request it to be coherent, so that writes are visible to the GPU right after unmapping the buffer
        memAlloc.memoryTypeIndex = getMemoryTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VK_ON_FAIL_RETURN_VOID(vkAllocateMemory(device, &memAlloc, nullptr, &stagingBuffers.vertices.memory));
        // Map and copy
        VK_ON_FAIL_RETURN_VOID(vkMapMemory(device, stagingBuffers.vertices.memory, 0, memAlloc.allocationSize, 0, &data));
        memcpy(data, vertexBuffer.data(), vertexBufferSize);
        vkUnmapMemory(device, stagingBuffers.vertices.memory);
        VK_ON_FAIL_RETURN_VOID(vkBindBufferMemory(device, stagingBuffers.vertices.buffer, stagingBuffers.vertices.memory, 0));

        // Create a device local buffer to which the (host local) vertex data will be copied and which will be used for rendering
        vertexBufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VK_ON_FAIL_RETURN_VOID(vkCreateBuffer(device, &vertexBufferInfo, nullptr, &vertices.buffer));
        vkGetBufferMemoryRequirements(device, vertices.buffer, &memReqs);
        memAlloc.allocationSize = memReqs.size;
        memAlloc.memoryTypeIndex = getMemoryTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_ON_FAIL_RETURN_VOID(vkAllocateMemory(device, &memAlloc, nullptr, &vertices.memory));
        VK_ON_FAIL_RETURN_VOID(vkBindBufferMemory(device, vertices.buffer, vertices.memory, 0));

        // Index buffer
        VkBufferCreateInfo indexbufferInfo = {};
        indexbufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        indexbufferInfo.size = indexBufferSize;
        indexbufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        // Copy index data to a buffer visible to the host (staging buffer)
        VK_ON_FAIL_RETURN_VOID(vkCreateBuffer(device, &indexbufferInfo, nullptr, &stagingBuffers.indices.buffer));
        vkGetBufferMemoryRequirements(device, stagingBuffers.indices.buffer, &memReqs);
        memAlloc.allocationSize = memReqs.size;
        memAlloc.memoryTypeIndex = getMemoryTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VK_ON_FAIL_RETURN_VOID(vkAllocateMemory(device, &memAlloc, nullptr, &stagingBuffers.indices.memory));
        VK_ON_FAIL_RETURN_VOID(vkMapMemory(device, stagingBuffers.indices.memory, 0, indexBufferSize, 0, &data));
        memcpy(data, indexBuffer.data(), indexBufferSize);
        vkUnmapMemory(device, stagingBuffers.indices.memory);
        VK_ON_FAIL_RETURN_VOID(vkBindBufferMemory(device, stagingBuffers.indices.buffer, stagingBuffers.indices.memory, 0));

        // Create destination buffer with device only visibility
        indexbufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VK_ON_FAIL_RETURN_VOID(vkCreateBuffer(device, &indexbufferInfo, nullptr, &indices.buffer));
        vkGetBufferMemoryRequirements(device, indices.buffer, &memReqs);
        memAlloc.allocationSize = memReqs.size;
        memAlloc.memoryTypeIndex = getMemoryTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_ON_FAIL_RETURN_VOID(vkAllocateMemory(device, &memAlloc, nullptr, &indices.memory));
        VK_ON_FAIL_RETURN_VOID(vkBindBufferMemory(device, indices.buffer, indices.memory, 0));

        VkCommandBufferBeginInfo cmdBufferBeginInfo = {};
        cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        cmdBufferBeginInfo.pNext = nullptr;

        // Buffer copies have to be submitted to a queue, so we need a command buffer for them
        // Note: Some devices offer a dedicated transfer queue (with only the transfer bit set) that may be faster when doing lots of copies
        VkCommandBuffer copyCmd = getCommandBuffer(true);
        ON_FAIL_RETURN_VOID(copyCmd != VK_NULL_HANDLE);

        // Put buffer region copies into command buffer
        VkBufferCopy copyRegion = {};

        // Vertex buffer
        copyRegion.size = vertexBufferSize;
        vkCmdCopyBuffer(copyCmd, stagingBuffers.vertices.buffer, vertices.buffer, 1, &copyRegion);
        // Index buffer
        copyRegion.size = indexBufferSize;
        vkCmdCopyBuffer(copyCmd, stagingBuffers.indices.buffer, indices.buffer, 1, &copyRegion);

        // Flushing the command buffer will also submit it to the queue and uses a fence to ensure that all commands have been executed before returning
        VK_ON_FAIL_RETURN_VOID(flushCommandBuffer(copyCmd));

        // Destroy staging buffers
        // Note: Staging buffer must not be deleted before the copies have been submitted and executed
        vkDestroyBuffer(device, stagingBuffers.vertices.buffer, nullptr);
        vkFreeMemory(device, stagingBuffers.vertices.memory, nullptr);
        vkDestroyBuffer(device, stagingBuffers.indices.buffer, nullptr);
        vkFreeMemory(device, stagingBuffers.indices.memory, nullptr);
        }
    // Uniform buffer block object
    struct {
        VkDeviceMemory memory;
        VkBuffer buffer;
        VkDescriptorBufferInfo descriptor;
    }  uniformBufferVS;

    // For simplicity we use the same uniform block layout as in the shader:
    //
    //	layout(set = 0, binding = 0) uniform UBO
    //	{
    //		mat4 projectionMatrix;
    //		mat4 modelMatrix;
    //		mat4 viewMatrix;
    //	} ubo;
    //
    // This way we can just memcopy the ubo data to the ubo
    // Note: You should use data types that align with the GPU in order to avoid manual padding (vec4, mat4)
    struct {
        glm::mat4 projectionMatrix;
        glm::mat4 modelMatrix;
        glm::mat4 viewMatrix;
    } uboVS;

    glm::vec3 rotation = glm::vec3();
    float zoom = -2.5f;

    {
        // Prepare and initialize a uniform buffer block containing shader uniforms
        // Single uniforms like in OpenGL are no longer present in Vulkan. All Shader uniforms are passed via uniform buffer blocks
        VkMemoryRequirements memReqs;

        // Vertex shader uniform buffer block
        VkBufferCreateInfo bufferInfo = {};
        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.pNext = nullptr;
        allocInfo.allocationSize = 0;
        allocInfo.memoryTypeIndex = 0;

        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(uboVS);
        // This buffer will be used as a uniform buffer
        bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;

        // Create a new buffer
        VK_ON_FAIL_RETURN_VOID(vkCreateBuffer(device, &bufferInfo, nullptr, &uniformBufferVS.buffer));
        // Get memory requirements including size, alignment and memory type 
        vkGetBufferMemoryRequirements(device, uniformBufferVS.buffer, &memReqs);
        allocInfo.allocationSize = memReqs.size;
        // Get the memory type index that supports host visibile memory access
        // Most implementations offer multiple memory types and selecting the correct one to allocate memory from is crucial
        // We also want the buffer to be host coherent so we don't have to flush (or sync after every update.
        // Note: This may affect performance so you might not want to do this in a real world application that updates buffers on a regular base
        allocInfo.memoryTypeIndex = getMemoryTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        // Allocate memory for the uniform buffer
        VK_ON_FAIL_RETURN_VOID(vkAllocateMemory(device, &allocInfo, nullptr, &(uniformBufferVS.memory)));
        // Bind memory to buffer
        VK_ON_FAIL_RETURN_VOID(vkBindBufferMemory(device, uniformBufferVS.buffer, uniformBufferVS.memory, 0));
    }

    // Store information in the uniform's descriptor that is used by the descriptor set
    uniformBufferVS.descriptor.buffer = uniformBufferVS.buffer;
    uniformBufferVS.descriptor.offset = 0;
    uniformBufferVS.descriptor.range = sizeof(uboVS);

    // Update matrices
    uboVS.projectionMatrix = glm::perspective(glm::radians(60.0f), (float) width / (float) height, 0.1f, 256.0f);

    uboVS.viewMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, zoom));

    uboVS.modelMatrix = glm::mat4(1.0f);
    uboVS.modelMatrix = glm::rotate(uboVS.modelMatrix, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
    uboVS.modelMatrix = glm::rotate(uboVS.modelMatrix, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
    uboVS.modelMatrix = glm::rotate(uboVS.modelMatrix, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

    // Map uniform buffer and update it
    uint8_t *pData;
    VK_ON_FAIL_RETURN_VOID(vkMapMemory(device, uniformBufferVS.memory, 0, sizeof(uboVS), 0, (void **) &pData));
    memcpy(pData, &uboVS, sizeof(uboVS));
    // Unmap after data has been copied
    // Note: Since we requested a host coherent memory type for the uniform buffer, the write is instantly visible to the GPU
    vkUnmapMemory(device, uniformBufferVS.memory);

    //////////////////////////////////////////////////////////////////////////
    //////////////////////////////////////////////////////////////////////////
    //////////////////////////////////////////////////////////////////////////

    MSG msg;
    BOOL bRet;

    while ((bRet = GetMessage(&msg, nullptr, 0, 0)) != 0) {
        ON_FAIL_RETURN_VOID(bRet != -1);

        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}

LRESULT CALLBACK input_wndproc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_INPUT: {
            RAWINPUT raw;
            UINT size = sizeof(raw);

            GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam), RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER));

            switch (raw.header.dwType) {
                case RIM_TYPEKEYBOARD: {
                    auto& kb = raw.data.keyboard;
                    LOG_INFO(
                        "  Kbd: make=", belog::fmt(kb.MakeCode, belog::hex{}, belog::padding{ 4, '0' }),
                        " Flags=", belog::fmt(kb.Flags, belog::hex{}, belog::padding{ 4, '0' }),
                        " msg=", kb.Message
                    );
                    if (kb.MakeCode == 0x01 && (kb.Flags & RI_KEY_BREAK)) {
                        PostMessage(window, WM_QUIT, 0, 0);
                    }
                    break;
                }

                case RIM_TYPEMOUSE: {
                    static f32 yaw = 0.0f;
                    static f32 pitch = 0.0f;
                    auto& m = raw.data.mouse;

                    constexpr f32 sensitivityX = 0.005f;
                    constexpr f32 sensitivityY = 0.005f;

                    yaw += float(m.lLastX) * sensitivityX;
                    pitch = ctu::clamp(pitch + float(m.lLastY) * sensitivityY, -90.0f, 90.0f);

                    if (yaw > 360.0f) yaw -= 360.0f;
                    if (yaw < -360.0f) yaw += 360.0f;

                    f32 yaw_rad = ctu::deg_to_rad(yaw);
                    f32 pitch_rad = ctu::deg_to_rad(pitch);
                    
                    auto view = vector3{ 1.0f, 0.0f, 0.0f }.rotate(
                        quaternion{ cos(yaw_rad / 2.0f), 0.0f, 0.0f, sin(yaw_rad / 2.0f) } *
                        quaternion{ cos(pitch_rad / 2.0f), 0.0f, sin(pitch_rad / 2.0f), 0.0f }
                    ).unit();

                    LOG_INFO(" View: (", view.x(), ", ", view.y(), ", ", view.z(), ")");
                    break;
                }
            }

            return 0;
        }

        case WM_QUIT: {
            break;
        }
    }

    return DefWindowProc(window, message, wparam, lparam);
}

int APIENTRY WinMain(HINSTANCE /*hInstance*/, HINSTANCE /*hPrevInstance*/, LPSTR /*pCmdLine*/, int /*nCmdShow*/) {
    AllocConsole();
    AttachConsole(GetCurrentProcessId());
    FILE* stream;
    freopen_s(&stream, "CONOUT$", "w+", stdout);
    freopen_s(&stream, "CONOUT$", "w+", stderr);
    SetConsoleTitle(TEXT("RawInputTest"));

    analyze();
    std::thread logging_thread{ belog::do_logging };
    belog::enable_logging();
    std::thread input_thread{ input_thread_fn };
    input_thread.join();
    belog::shutdown();
    logging_thread.join();
}
