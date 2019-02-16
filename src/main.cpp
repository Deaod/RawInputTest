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
#include "spsc_queue.hpp"
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

union VkVersion {
    u32 all_bits;
    bitfield<u32, 0, 11> patch;
    bitfield<u32, 12, 21> minor;
    bitfield<u32, 22, 31> major;
};

enum class scancode {
    escape = 0x01,
    dig1,
    dig2,
    dig3,
    dig4,
    dig5,
    dig6,
    dig7,
    dig8,
    dig9,
    dig0,
    minus,
    plus,
    backspace,
    tab,
    q,
    w,
    e,
    r,
    t,
    y,
    u,
    i,
    o,
    p,
    open_bracket,
    close_bracket,
    enter,
    ctrl_left,
    a,
    s,
    d,
    f,
    g,
    h,
    j,
    k,
    l,
    semicolon,
    apostrophe,
    back_tick,
    shift_left,
    back_slash,
    z,
    x,
    c,
    v,
    b,
    n,
    m,
    comma,
    dot,
    slash,
    shift_right,
    num_mul,
    alt_left,
    space,
    caps_lock,
    f1,
    f2,
    f3,
    f4,
    f5,
    f6,
    f7,
    f8,
    f9,
    f10,
    num_lock,
    scroll_lock,
    num7,
    num8,
    num9,
    num_minus,
    num4,
    num5,
    num6,
    num_plus,
    num1,
    num2,
    num3,
    num0,
    num_dot,
    sys_rq,
    _unmapped1,
    int_1,
    f11,
    f12,
    _unmapped2,
    oem_1,
    oem_2,
    oem_3,
};

enum input_type : u64 {
    MOUSE = 0,
    KEYBOARD = 1,
    RESERVED0 = 2,
    RESERVED1 = 3
};

struct input_mouse {
    i16 x;
    i16 y;
    i16 scroll;
    union {
        u16 flags;

        bitfield<u16, 0> btn_left_down;
        bitfield<u16, 1> btn_left_up;
        bitfield<u16, 2> btn_right_down;
        bitfield<u16, 3> btn_right_up;
        bitfield<u16, 4> btn_middle_down;
        bitfield<u16, 5> btn_middle_up;

        bitfield<u16, 0> btn_1_down;
        bitfield<u16, 1> btn_1_up;
        bitfield<u16, 2> btn_2_down;
        bitfield<u16, 3> btn_2_up;
        bitfield<u16, 4> btn_3_down;
        bitfield<u16, 5> btn_3_up;
        bitfield<u16, 6> btn_4_down;
        bitfield<u16, 7> btn_4_up;
        bitfield<u16, 8> btn_5_down;
        bitfield<u16, 9> btn_5_up;

        bitfield<u16, 10> wheel;
        bitfield<u16, 11> hwheel;
    };
};

struct input_keyboard {
    u8 scancode;
    union {
        u8 flags;

        bitfield<u8, 0> up;
        bitfield<u8, 1> e0;
        bitfield<u8, 2> e1;
    };
};

struct input {
    union {
        bitfield<u64, 0, 1> type;
        bitfield<u64, 2, 63> tsc;
    };
    union {
        input_mouse m;
        input_keyboard k;
    };
};

LRESULT CALLBACK input_wndproc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_INPUT: {
            RAWINPUT raw;
            UINT size = sizeof(raw);

            GetRawInputData(reinterpret_cast<HRAWINPUT>(lparam), RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER));

            auto input_queue = (spsc_queue<input, 8>*) GetWindowLongPtr(window, 0);
            if (input_queue == nullptr) {
                LOG_ERR("input_queue attached to window is null, last error = ", GetLastError());
                break;
            }

            
            input_queue->produce([&](void* storage) {
                auto in = new(storage) input();
                in->tsc = tsc() >> in->tsc.first_bit;
                switch (raw.header.dwType) {
                    case RIM_TYPEKEYBOARD:
                    {
                        auto& kb = raw.data.keyboard;
                        in->type = KEYBOARD;
                        in->k.scancode = u8(kb.MakeCode);
                        in->k.flags = u8(kb.Flags);
                        break;
                    }

                    case RIM_TYPEMOUSE:
                    {
                        auto& m = raw.data.mouse;
                        // ignore absolute positioning of mouse, only use relative positioning
                        if ((m.usFlags & MOUSE_MOVE_ABSOLUTE) != 0) {
                            return false;
                        }
                        in->type = MOUSE;
                        in->m.x = i16(m.lLastX);
                        in->m.y = i16(m.lLastY);
                        in->m.scroll = i16(m.usButtonData);
                        in->m.flags = u16(m.usButtonFlags);
                        break;
                    }
                }

                return true;
            });

            return 0;
        }

        case WM_QUIT: {
            break;
        }
    }

    return DefWindowProc(window, message, wparam, lparam);
}

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPSTR /*pCmdLine*/, int /*nCmdShow*/) {
    AllocConsole();
    AttachConsole(GetCurrentProcessId());
    FILE* stream;
    freopen_s(&stream, "CONOUT$", "w+", stdout);
    freopen_s(&stream, "CONOUT$", "w+", stderr);
    SetConsoleTitle(TEXT("RawInputTest"));

    HWND window = nullptr;
    std::atomic_bool window_created = false;
    auto input_queue = std::make_unique<spsc_queue<input, 8>>();

    static auto input_thread_func = [&window, &window_created, &input_queue]() {
        threads::current::assign_id();
        belog::enable_logging();

        auto module_handle = GetModuleHandle(nullptr);
        auto monitor_handle = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTONEAREST);
        auto background = CreateSolidBrush(RGB(0, 0, 0));

        WNDCLASSEX input_capture_class;
        input_capture_class.cbSize = sizeof(input_capture_class);
        input_capture_class.style = CS_HREDRAW | CS_VREDRAW;
        input_capture_class.lpfnWndProc = input_wndproc;
        input_capture_class.cbClsExtra = 0;
        input_capture_class.cbWndExtra = sizeof(void*);
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

        window = CreateWindow(
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
            window != nullptr,
            "failed to create window ", GetLastError()
        );

        SetLastError(0);
        SetWindowLongPtr(window, 0, LONG_PTR(input_queue.get()));
        ON_FAIL_RETURN_VOID(GetLastError() == 0);

        RAWINPUTDEVICE devices[2];

        devices[0].usUsagePage = 0x01;
        devices[0].usUsage = 0x02;
        devices[0].dwFlags = RIDEV_NOLEGACY;   // adds HID mouse and also ignores legacy mouse messages
        devices[0].hwndTarget = window;

        devices[1].usUsagePage = 0x01;
        devices[1].usUsage = 0x06;
        devices[1].dwFlags = RIDEV_NOLEGACY;   // adds HID keyboard and also ignores legacy keyboard messages
        devices[1].hwndTarget = window;

        ON_FAIL_TRACE_RETURN_VOID(
            RegisterRawInputDevices(devices, 2, sizeof(RAWINPUTDEVICE)),
            "failed to register for raw input ", GetLastError()
        );

        window_created.store(true, std::memory_order_release);

        MSG msg;
        BOOL bRet;

        while ((bRet = GetMessage(&msg, nullptr, 0, 0)) != 0) {
            ON_FAIL_RETURN_VOID(bRet != -1);

            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        threads::current::release_id();
    };

    std::thread tsc_calibration_thread{ measure_tsc_frequency };

    analyze();

    tsc_calibration_thread.join();

    std::thread logging_thread{ belog::do_logging };
    belog::enable_logging();
    std::thread input_thread{ input_thread_func };

    // busy wait until window has been created.
    while (window_created.load(std::memory_order_acquire) == false) {}

    //start rendering
    {
        VkInstance                           instance;
        VkPhysicalDevice                     physicalDevice;
        VkPhysicalDeviceProperties           deviceProperties;
        VkPhysicalDeviceFeatures             deviceFeatures;
        VkPhysicalDeviceMemoryProperties     deviceMemoryProperties;
        VkPhysicalDeviceFeatures             enabledFeatures{};
        std::vector<VkQueueFamilyProperties> queueFamilyProperties;
        std::vector<std::string>             supportedExtensions;
        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos{};
        struct {
            u32 graphics;
            u32 compute;
            u32 transfer;
        }                                    queueFamilyIndices = { ~u32(0), ~u32(0), ~u32(0) };
        VkDevice                             device;
        VkFormat                             depthFormat = VK_FORMAT_MAX_ENUM;
        struct {
            // Swap chain image presentation
            VkSemaphore presentComplete;
            // Command buffer submission and execution
            VkSemaphore renderComplete;
            // UI overlay submission and execution
            VkSemaphore overlayComplete;
        }                                    semaphores;
        VkSurfaceKHR                         surface;
        VkFormat                             colorFormat = VK_FORMAT_MAX_ENUM;
        VkColorSpaceKHR                      colorSpace = VK_COLOR_SPACE_MAX_ENUM_KHR;
        VkQueue                              queue;
        std::vector<VkPresentModeKHR>        presentModes;
        /** @brief Handle to the current swap chain, required for recreation */
        VkSwapchainKHR                       swapChain;
        struct SwapChainBuffer {
            VkImage image;
            VkImageView view;
        };
        std::vector<SwapChainBuffer>         buffers;
        u32                                  imageCount;
        std::vector<VkCommandBuffer>         drawCmdBuffers;
        struct {
            VkImage image;
            VkDeviceMemory mem;
            VkImageView view;
        }                                    depthStencil;
        VkRenderPass                         renderPass;
        VkPipelineCache                      pipelineCache;
        std::vector<VkFramebuffer>           frameBuffers;
        // Semaphores
        // Used to coordinate operations within the graphics queue and ensure correct command ordering
        VkSemaphore                          presentCompleteSemaphore;
        VkSemaphore                          renderCompleteSemaphore;
        // Fences
        // Used to check the completion of queue operations (e.g. command buffer execution)
        std::vector<VkFence>                 waitFences;

        struct Vertex {
            float position[3];
            float color[3];
        };

        struct {
            VkDeviceMemory memory; // Handle to the device memory for this buffer
            VkBuffer buffer;       // Handle to the Vulkan buffer object that the memory is bound to
        }                                    vertices;

        struct {
            VkDeviceMemory memory;
            VkBuffer buffer;
            u32 count;
        }                                    indices;

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

        // The descriptor set layout describes the shader binding layout (without actually referencing descriptor)
        // Like the pipeline layout it's pretty much a blueprint and can be used with different descriptor sets as long as their layout matches
        VkDescriptorSetLayout descriptorSetLayout;

        // The pipeline layout is used by a pipline to access the descriptor sets 
        // It defines interface (without binding any actual data) between the shader stages used by the pipeline and the shader resources
        // A pipeline layout can be shared among multiple pipelines as long as their interfaces match
        VkPipelineLayout pipelineLayout;

        // Pipelines (often called "pipeline state objects") are used to bake all states that affect a pipeline
        // While in OpenGL every state can be changed at (almost) any time, Vulkan requires to layout the graphics (and compute) pipeline states upfront
        // So for each combination of non-dynamic pipeline states you need a new pipeline (there are a few exceptions to this not discussed here)
        // Even though this adds a new dimension of planing ahead, it's a great opportunity for performance optimizations by the driver
        VkPipeline pipeline;

        // Descriptor set pool
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;

        // The descriptor set stores the resources bound to the binding points in a shader
        // It connects the binding points of the different shaders with the buffers and images used for those bindings
        VkDescriptorSet descriptorSet;

        {
            VkApplicationInfo appInfo = {};
            appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
            appInfo.pApplicationName = "RawInputTest";
            appInfo.pEngineName = "RawInputTest";

            VK_GET_INSTANCE_PROC_ADDR(nullptr, vkEnumerateInstanceVersion);
            if (vkEnumerateInstanceVersion != nullptr) {
                u32 maxVersionSupported = VK_API_VERSION_1_1;
                u32 versionSupported;
                VK_ON_FAIL_RETURN(vkEnumerateInstanceVersion(&versionSupported), EXIT_FAILURE);

                appInfo.apiVersion = std::min(maxVersionSupported, versionSupported);
            } else {
                appInfo.apiVersion = VK_API_VERSION_1_0;
            }

            VkVersion ver{ appInfo.apiVersion };
            LOG_INFO("Using Vulkan Version ", u32(ver.major), ".", u32(ver.minor), ".", u32(ver.patch));

            const char* instanceExtensions[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME };

            VkInstanceCreateInfo instanceCreateInfo = {};
            instanceCreateInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
            instanceCreateInfo.pNext = nullptr;
            instanceCreateInfo.pApplicationInfo = &appInfo;

            instanceCreateInfo.enabledExtensionCount = u32(sizeof(instanceExtensions) / sizeof(*instanceExtensions));
            instanceCreateInfo.ppEnabledExtensionNames = instanceExtensions;

            VK_ON_FAIL_RETURN(vkCreateInstance(&instanceCreateInfo, nullptr, &instance), EXIT_FAILURE);
        }

        {
            u32 gpuCount = 1;
            VK_ON_ERROR_RETURN(vkEnumeratePhysicalDevices(instance, &gpuCount, &physicalDevice), EXIT_FAILURE);
        }


        vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);
        vkGetPhysicalDeviceFeatures(physicalDevice, &deviceFeatures);
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &deviceMemoryProperties);

        auto getMemoryTypeIndex = [&deviceMemoryProperties](u32 typeBits, VkMemoryPropertyFlags properties) {
            for (u32 i = 0; i < deviceMemoryProperties.memoryTypeCount; i++, typeBits >>= 1) {
                if ((typeBits & 1) == 1) {
                    if ((deviceMemoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
                        return i;
                    }
                }
            }

            return ~u32(0);
        };

        {
            u32 queueFamilyCount;
            vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
            queueFamilyProperties.resize(queueFamilyCount);
            vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilyProperties.data());
        }

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

        const float defaultQueuePriority(0.0f);

        {
            for (u32 i = 0; i < u32(queueFamilyProperties.size()); i++) {
                if (queueFamilyProperties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                    queueFamilyIndices.graphics = i;
                    break;
                }
            }

            ON_FAIL_TRACE_RETURN(queueFamilyIndices.graphics != ~u32(0), EXIT_FAILURE, "Failed to find graphics queue.");

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

            ON_FAIL_TRACE_RETURN(queueFamilyIndices.compute != ~u32(0), EXIT_FAILURE, "Failed to find compute queue.");

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

            VK_ON_FAIL_RETURN(vkCreateDevice(physicalDevice, &deviceCreateInfo, nullptr, &device), EXIT_FAILURE);
        }

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
            ON_FAIL_TRACE_RETURN(
                depthFormat != VK_FORMAT_MAX_ENUM,
                EXIT_FAILURE,
                "Failed to find appropriate depth format"
            );
        }

        [[maybe_unused]] VK_GET_INSTANCE_PROC_ADDR(instance, vkGetPhysicalDeviceSurfaceSupportKHR);
        [[maybe_unused]] VK_GET_INSTANCE_PROC_ADDR(instance, vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
        [[maybe_unused]] VK_GET_INSTANCE_PROC_ADDR(instance, vkGetPhysicalDeviceSurfaceFormatsKHR);
        [[maybe_unused]] VK_GET_INSTANCE_PROC_ADDR(instance, vkGetPhysicalDeviceSurfacePresentModesKHR);

        [[maybe_unused]] VK_GET_DEVICE_PROC_ADDR(device, vkCreateSwapchainKHR);
        [[maybe_unused]] VK_GET_DEVICE_PROC_ADDR(device, vkDestroySwapchainKHR);
        [[maybe_unused]] VK_GET_DEVICE_PROC_ADDR(device, vkGetSwapchainImagesKHR);
        [[maybe_unused]] VK_GET_DEVICE_PROC_ADDR(device, vkAcquireNextImageKHR);
        [[maybe_unused]] VK_GET_DEVICE_PROC_ADDR(device, vkQueuePresentKHR);

        {
            VkSemaphoreCreateInfo semaphoreCreateInfo{};
            semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

            // Create a semaphore used to synchronize image presentation
            // Ensures that the image is displayed before we start submitting new commands to the queue
            VK_ON_FAIL_RETURN(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &semaphores.presentComplete), EXIT_FAILURE);

            // Create a semaphore used to synchronize command submission
            // Ensures that the image is not presented until all commands have been submitted and executed
            VK_ON_FAIL_RETURN(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &semaphores.renderComplete), EXIT_FAILURE);

            // Create a semaphore used to synchronize command submission
            // Ensures that the image is not presented until all commands for the UI overlay have been submitted and executed
            // Will be inserted after the render complete semaphore if the UI overlay is enabled
            VK_ON_FAIL_RETURN(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &semaphores.overlayComplete), EXIT_FAILURE);
        }

        {
            VkPipelineStageFlags submitPipelineStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            VkSubmitInfo submitInfo{};
            submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            submitInfo.pWaitDstStageMask = &submitPipelineStages;
            submitInfo.waitSemaphoreCount = 1;
            submitInfo.pWaitSemaphores = &semaphores.presentComplete;
            submitInfo.signalSemaphoreCount = 1;
            submitInfo.pSignalSemaphores = &semaphores.renderComplete;
        }

        {
            VkWin32SurfaceCreateInfoKHR surfaceCreateInfo = {};
            surfaceCreateInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
            surfaceCreateInfo.hinstance = hInstance;
            surfaceCreateInfo.hwnd = window;
            VK_ON_FAIL_RETURN(vkCreateWin32SurfaceKHR(instance, &surfaceCreateInfo, nullptr, &surface), EXIT_FAILURE);
        }

        u32 queueNodeIndex = ~u32(0);

        for (u32 queueFamilyIndex = 0; queueFamilyIndex < queueFamilyProperties.size(); queueFamilyIndex++) {
            VkBool32 supportsPresent;
            vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, queueFamilyIndex, surface, &supportsPresent);
            if (((queueFamilyProperties[queueFamilyIndex].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) && supportsPresent) {
                queueNodeIndex = queueFamilyIndex;
            }
        }

        ON_FAIL_TRACE_RETURN(
            queueNodeIndex != ~u32(0),
            EXIT_FAILURE,
            "Failed to find graphics/presenting queue"
        );

        uint32_t formatCount;
        VK_ON_FAIL_RETURN(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr), EXIT_FAILURE);

        std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
        VK_ON_FAIL_RETURN(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, surfaceFormats.data()), EXIT_FAILURE);

        {
            // If the surface format list only includes one entry with VK_FORMAT_UNDEFINED,
            // there is no preferred format, so we assume VK_FORMAT_B8G8R8A8_UNORM
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

        vkGetDeviceQueue(device, queueNodeIndex, 0, &queue);

        VkCommandPoolCreateInfo cmdPoolInfo = {};
        cmdPoolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        cmdPoolInfo.queueFamilyIndex = queueNodeIndex;
        cmdPoolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        VkCommandPool commandPool;
        VK_ON_FAIL_RETURN(vkCreateCommandPool(device, &cmdPoolInfo, nullptr, &commandPool), EXIT_FAILURE);

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
        ON_FAIL_TRACE_RETURN(GetWindowRect(window, &windowRect), EXIT_FAILURE, "Failed to get window rect: ", GetLastError());

        u32 width = windowRect.right - windowRect.left;
        u32 height = windowRect.bottom - windowRect.top;

        {
            u32 presentModeCount;
            VK_ON_FAIL_RETURN(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr), EXIT_FAILURE);

            presentModes.resize(presentModeCount);
            VK_ON_FAIL_RETURN(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, presentModes.data()), EXIT_FAILURE);
        }

        {
            VkSurfaceCapabilitiesKHR surfCaps;
            VK_ON_FAIL_RETURN(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &surfCaps), EXIT_FAILURE);

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
            for (auto mode : presentModes) {
                if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
                    swapchainPresentMode = VK_PRESENT_MODE_MAILBOX_KHR;
                }
                if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
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

            VK_ON_FAIL_RETURN(vkCreateSwapchainKHR(device, &swapchainCI, nullptr, &swapChain), EXIT_FAILURE);
        }

        {
            VK_ON_FAIL_RETURN(vkGetSwapchainImagesKHR(device, swapChain, &imageCount, nullptr), EXIT_FAILURE);
            std::vector<VkImage> images(imageCount);
            VK_ON_FAIL_RETURN(vkGetSwapchainImagesKHR(device, swapChain, &imageCount, images.data()), EXIT_FAILURE);

            buffers.resize(imageCount);

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

                VK_ON_FAIL_RETURN(vkCreateImageView(device, &colorAttachmentView, nullptr, &buffers[i].view), EXIT_FAILURE);
            }
        }

        drawCmdBuffers.resize(imageCount);
        {
            VkCommandBufferAllocateInfo cmdBufAllocateInfo = {};
            cmdBufAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
            cmdBufAllocateInfo.commandPool = commandPool;
            cmdBufAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            cmdBufAllocateInfo.commandBufferCount = imageCount;
            VK_ON_FAIL_RETURN(vkAllocateCommandBuffers(device, &cmdBufAllocateInfo, drawCmdBuffers.data()), EXIT_FAILURE);
        }

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

            VK_ON_FAIL_RETURN(vkCreateImage(device, &image, nullptr, &depthStencil.image), EXIT_FAILURE);
            vkGetImageMemoryRequirements(device, depthStencil.image, &memReqs);
            mem_alloc.allocationSize = memReqs.size;
            mem_alloc.memoryTypeIndex = getMemoryTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

            VK_ON_FAIL_RETURN(vkAllocateMemory(device, &mem_alloc, nullptr, &depthStencil.mem), EXIT_FAILURE);
            VK_ON_FAIL_RETURN(vkBindImageMemory(device, depthStencil.image, depthStencil.mem, 0), EXIT_FAILURE);

            depthStencilView.image = depthStencil.image;
            VK_ON_FAIL_RETURN(vkCreateImageView(device, &depthStencilView, nullptr, &depthStencil.view), EXIT_FAILURE);
        }

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

            VK_ON_FAIL_RETURN(vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass), EXIT_FAILURE);
        }


        {
            VkPipelineCacheCreateInfo pipelineCacheCreateInfo = {};
            pipelineCacheCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
            VK_ON_FAIL_RETURN(vkCreatePipelineCache(device, &pipelineCacheCreateInfo, nullptr, &pipelineCache), EXIT_FAILURE);
        }

        frameBuffers.resize(imageCount);
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
                VK_ON_FAIL_RETURN(vkCreateFramebuffer(device, &frameBufferCreateInfo, nullptr, &frameBuffers[i]), EXIT_FAILURE);
            }
        }


        waitFences.resize(imageCount);
        {
            // Semaphores (Used for correct command ordering)
            VkSemaphoreCreateInfo semaphoreCreateInfo = {};
            semaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
            semaphoreCreateInfo.pNext = nullptr;

            // Semaphore used to ensures that image presentation is complete before starting to submit again
            VK_ON_FAIL_RETURN(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &presentCompleteSemaphore), EXIT_FAILURE);

            // Semaphore used to ensures that all commands submitted have been finished before submitting the image to the queue
            VK_ON_FAIL_RETURN(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &renderCompleteSemaphore), EXIT_FAILURE);

            // Fences (Used to check draw command buffer completion)
            VkFenceCreateInfo fenceCreateInfo = {};
            fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            // Create in signaled state so we don't wait on first render of each command buffer
            fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            for (auto& fence : waitFences) {
                VK_ON_FAIL_RETURN(vkCreateFence(device, &fenceCreateInfo, nullptr, &fence), EXIT_FAILURE);
            }
        }

        {
            // Setup vertices
            std::vector<Vertex> vertexBuffer = {
                { {  1.0f,  1.0f, 0.0f },{ 1.0f, 0.0f, 0.0f } },
                { { -1.0f,  1.0f, 0.0f },{ 0.0f, 1.0f, 0.0f } },
                { {  1.0f, -1.0f, 0.0f },{ 0.0f, 0.0f, 1.0f } },
                { { -1.0f, -1.0f, 0.0f },{ 1.0f, 0.0f, 0.0f } },
            };
            u32 vertexBufferSize = u32(vertexBuffer.size() * sizeof(Vertex));

            // Setup indices
            std::vector<u32> indexBuffer = { 0, 1, 2, 1, 2, 3 };
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
            VK_ON_FAIL_RETURN(vkCreateBuffer(device, &vertexBufferInfo, nullptr, &stagingBuffers.vertices.buffer), EXIT_FAILURE);
            vkGetBufferMemoryRequirements(device, stagingBuffers.vertices.buffer, &memReqs);
            memAlloc.allocationSize = memReqs.size;
            // Request a host visible memory type that can be used to copy our data do
            // Also request it to be coherent, so that writes are visible to the GPU right after unmapping the buffer
            memAlloc.memoryTypeIndex = getMemoryTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            VK_ON_FAIL_RETURN(vkAllocateMemory(device, &memAlloc, nullptr, &stagingBuffers.vertices.memory), EXIT_FAILURE);
            // Map and copy
            VK_ON_FAIL_RETURN(vkMapMemory(device, stagingBuffers.vertices.memory, 0, memAlloc.allocationSize, 0, &data), EXIT_FAILURE);
            memcpy(data, vertexBuffer.data(), vertexBufferSize);
            vkUnmapMemory(device, stagingBuffers.vertices.memory);
            VK_ON_FAIL_RETURN(vkBindBufferMemory(device, stagingBuffers.vertices.buffer, stagingBuffers.vertices.memory, 0), EXIT_FAILURE);

            // Create a device local buffer to which the (host local) vertex data will be copied and which will be used for rendering
            vertexBufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            VK_ON_FAIL_RETURN(vkCreateBuffer(device, &vertexBufferInfo, nullptr, &vertices.buffer), EXIT_FAILURE);
            vkGetBufferMemoryRequirements(device, vertices.buffer, &memReqs);
            memAlloc.allocationSize = memReqs.size;
            memAlloc.memoryTypeIndex = getMemoryTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            VK_ON_FAIL_RETURN(vkAllocateMemory(device, &memAlloc, nullptr, &vertices.memory), EXIT_FAILURE);
            VK_ON_FAIL_RETURN(vkBindBufferMemory(device, vertices.buffer, vertices.memory, 0), EXIT_FAILURE);

            // Index buffer
            VkBufferCreateInfo indexbufferInfo = {};
            indexbufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            indexbufferInfo.size = indexBufferSize;
            indexbufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            // Copy index data to a buffer visible to the host (staging buffer)
            VK_ON_FAIL_RETURN(vkCreateBuffer(device, &indexbufferInfo, nullptr, &stagingBuffers.indices.buffer), EXIT_FAILURE);
            vkGetBufferMemoryRequirements(device, stagingBuffers.indices.buffer, &memReqs);
            memAlloc.allocationSize = memReqs.size;
            memAlloc.memoryTypeIndex = getMemoryTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            VK_ON_FAIL_RETURN(vkAllocateMemory(device, &memAlloc, nullptr, &stagingBuffers.indices.memory), EXIT_FAILURE);
            VK_ON_FAIL_RETURN(vkMapMemory(device, stagingBuffers.indices.memory, 0, indexBufferSize, 0, &data), EXIT_FAILURE);
            memcpy(data, indexBuffer.data(), indexBufferSize);
            vkUnmapMemory(device, stagingBuffers.indices.memory);
            VK_ON_FAIL_RETURN(vkBindBufferMemory(device, stagingBuffers.indices.buffer, stagingBuffers.indices.memory, 0), EXIT_FAILURE);

            // Create destination buffer with device only visibility
            indexbufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            VK_ON_FAIL_RETURN(vkCreateBuffer(device, &indexbufferInfo, nullptr, &indices.buffer), EXIT_FAILURE);
            vkGetBufferMemoryRequirements(device, indices.buffer, &memReqs);
            memAlloc.allocationSize = memReqs.size;
            memAlloc.memoryTypeIndex = getMemoryTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            VK_ON_FAIL_RETURN(vkAllocateMemory(device, &memAlloc, nullptr, &indices.memory), EXIT_FAILURE);
            VK_ON_FAIL_RETURN(vkBindBufferMemory(device, indices.buffer, indices.memory, 0), EXIT_FAILURE);

            VkCommandBufferBeginInfo cmdBufferBeginInfo = {};
            cmdBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            cmdBufferBeginInfo.pNext = nullptr;

            // Buffer copies have to be submitted to a queue, so we need a command buffer for them
            // Note: Some devices offer a dedicated transfer queue (with only the transfer bit set) that may be faster when doing lots of copies
            VkCommandBuffer copyCmd = getCommandBuffer(true);
            ON_FAIL_RETURN(copyCmd != VK_NULL_HANDLE, EXIT_FAILURE);

            // Put buffer region copies into command buffer
            VkBufferCopy copyRegion = {};

            // Vertex buffer
            copyRegion.size = vertexBufferSize;
            vkCmdCopyBuffer(copyCmd, stagingBuffers.vertices.buffer, vertices.buffer, 1, &copyRegion);
            // Index buffer
            copyRegion.size = indexBufferSize;
            vkCmdCopyBuffer(copyCmd, stagingBuffers.indices.buffer, indices.buffer, 1, &copyRegion);

            // Flushing the command buffer will also submit it to the queue and uses a fence to ensure that all commands have been executed before returning
            VK_ON_FAIL_RETURN(flushCommandBuffer(copyCmd), EXIT_FAILURE);

            // Destroy staging buffers
            // Note: Staging buffer must not be deleted before the copies have been submitted and executed
            vkDestroyBuffer(device, stagingBuffers.vertices.buffer, nullptr);
            vkFreeMemory(device, stagingBuffers.vertices.memory, nullptr);
            vkDestroyBuffer(device, stagingBuffers.indices.buffer, nullptr);
            vkFreeMemory(device, stagingBuffers.indices.memory, nullptr);
        }

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
            VK_ON_FAIL_RETURN(vkCreateBuffer(device, &bufferInfo, nullptr, &uniformBufferVS.buffer), EXIT_FAILURE);
            // Get memory requirements including size, alignment and memory type 
            vkGetBufferMemoryRequirements(device, uniformBufferVS.buffer, &memReqs);
            allocInfo.allocationSize = memReqs.size;
            // Get the memory type index that supports host visibile memory access
            // Most implementations offer multiple memory types and selecting the correct one to allocate memory from is crucial
            // We also want the buffer to be host coherent so we don't have to flush (or sync after every update.
            // Note: This may affect performance so you might not want to do this in a real world application that updates buffers on a regular base
            allocInfo.memoryTypeIndex = getMemoryTypeIndex(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            // Allocate memory for the uniform buffer
            VK_ON_FAIL_RETURN(vkAllocateMemory(device, &allocInfo, nullptr, &(uniformBufferVS.memory)), EXIT_FAILURE);
            // Bind memory to buffer
            VK_ON_FAIL_RETURN(vkBindBufferMemory(device, uniformBufferVS.buffer, uniformBufferVS.memory, 0), EXIT_FAILURE);
        }

        // Store information in the uniform's descriptor that is used by the descriptor set
        uniformBufferVS.descriptor.buffer = uniformBufferVS.buffer;
        uniformBufferVS.descriptor.offset = 0;
        uniformBufferVS.descriptor.range = sizeof(uboVS);

        {
            // Update matrices
            uboVS.projectionMatrix = glm::perspective(glm::radians(60.0f), (float) width / (float) height, 0.1f, 256.0f);

            uboVS.viewMatrix = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, zoom));

            uboVS.modelMatrix = glm::mat4(1.0f);
            uboVS.modelMatrix = glm::rotate(uboVS.modelMatrix, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
            uboVS.modelMatrix = glm::rotate(uboVS.modelMatrix, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
            uboVS.modelMatrix = glm::rotate(uboVS.modelMatrix, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

            // Map uniform buffer and update it
            uint8_t *pData;
            VK_ON_FAIL_RETURN(vkMapMemory(device, uniformBufferVS.memory, 0, sizeof(uboVS), 0, (void **) &pData), EXIT_FAILURE);
            memcpy(pData, &uboVS, sizeof(uboVS));
            // Unmap after data has been copied
            // Note: Since we requested a host coherent memory type for the uniform buffer, the write is instantly visible to the GPU
            vkUnmapMemory(device, uniformBufferVS.memory);
        }

        {
            // Setup layout of descriptors used in this example
            // Basically connects the different shader stages to descriptors for binding uniform buffers, image samplers, etc.
            // So every shader binding should map to one descriptor set layout binding

            // Binding 0: Uniform buffer (Vertex shader)
            VkDescriptorSetLayoutBinding layoutBinding = {};
            layoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            layoutBinding.descriptorCount = 1;
            layoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
            layoutBinding.pImmutableSamplers = nullptr;

            VkDescriptorSetLayoutCreateInfo descriptorLayout = {};
            descriptorLayout.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            descriptorLayout.pNext = nullptr;
            descriptorLayout.bindingCount = 1;
            descriptorLayout.pBindings = &layoutBinding;

            VK_ON_FAIL_RETURN(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout), EXIT_FAILURE);

            // Create the pipeline layout that is used to generate the rendering pipelines that are based on this descriptor set layout
            // In a more complex scenario you would have different pipeline layouts for different descriptor set layouts that could be reused
            VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo = {};
            pPipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pPipelineLayoutCreateInfo.pNext = nullptr;
            pPipelineLayoutCreateInfo.setLayoutCount = 1;
            pPipelineLayoutCreateInfo.pSetLayouts = &descriptorSetLayout;

            VK_ON_FAIL_RETURN(vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pipelineLayout), EXIT_FAILURE);
        }

        auto createShaderModule = [&device](const auto& shaderCode) -> VkShaderModule {
            // Create a new shader module that will be used for pipeline creation
            VkShaderModuleCreateInfo moduleCreateInfo{};
            moduleCreateInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            moduleCreateInfo.codeSize = sizeof(shaderCode);
            moduleCreateInfo.pCode = (uint32_t*) shaderCode;

            VkShaderModule shaderModule;
            VK_ON_FAIL_RETURN(vkCreateShaderModule(device, &moduleCreateInfo, nullptr, &shaderModule), nullptr);

            return shaderModule;
        };

        {
            // Create the graphics pipeline used in this example
            // Vulkan uses the concept of rendering pipelines to encapsulate fixed states, replacing OpenGL's complex state machine
            // A pipeline is then stored and hashed on the GPU making pipeline changes very fast
            // Note: There are still a few dynamic states that are not directly part of the pipeline (but the info that they are used is)

            VkGraphicsPipelineCreateInfo pipelineCreateInfo = {};
            pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
            // The layout used for this pipeline (can be shared among multiple pipelines using the same layout)
            pipelineCreateInfo.layout = pipelineLayout;
            // Renderpass this pipeline is attached to
            pipelineCreateInfo.renderPass = renderPass;

            // Construct the differnent states making up the pipeline

            // Input assembly state describes how primitives are assembled
            // This pipeline will assemble vertex data as a triangle lists (though we only use one triangle)
            VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = {};
            inputAssemblyState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
            inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

            // Rasterization state
            VkPipelineRasterizationStateCreateInfo rasterizationState = {};
            rasterizationState.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
            rasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
            rasterizationState.cullMode = VK_CULL_MODE_NONE;
            rasterizationState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            rasterizationState.depthClampEnable = VK_FALSE;
            rasterizationState.rasterizerDiscardEnable = VK_FALSE;
            rasterizationState.depthBiasEnable = VK_FALSE;
            rasterizationState.lineWidth = 1.0f;

            // Color blend state describes how blend factors are calculated (if used)
            // We need one blend attachment state per color attachment (even if blending is not used
            VkPipelineColorBlendAttachmentState blendAttachmentState[1] = {};
            blendAttachmentState[0].colorWriteMask = 0xf;
            blendAttachmentState[0].blendEnable = VK_FALSE;
            VkPipelineColorBlendStateCreateInfo colorBlendState = {};
            colorBlendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
            colorBlendState.attachmentCount = 1;
            colorBlendState.pAttachments = blendAttachmentState;

            // Viewport state sets the number of viewports and scissor used in this pipeline
            // Note: This is actually overriden by the dynamic states (see below)
            VkPipelineViewportStateCreateInfo viewportState = {};
            viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
            viewportState.viewportCount = 1;
            viewportState.scissorCount = 1;

            // Enable dynamic states
            // Most states are baked into the pipeline, but there are still a few dynamic states that can be changed within a command buffer
            // To be able to change these we need do specify which dynamic states will be changed using this pipeline. Their actual states are set later on in the command buffer.
            // For this example we will set the viewport and scissor using dynamic states
            std::vector<VkDynamicState> dynamicStateEnables;
            dynamicStateEnables.push_back(VK_DYNAMIC_STATE_VIEWPORT);
            dynamicStateEnables.push_back(VK_DYNAMIC_STATE_SCISSOR);
            VkPipelineDynamicStateCreateInfo dynamicState = {};
            dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
            dynamicState.pDynamicStates = dynamicStateEnables.data();
            dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStateEnables.size());

            // Depth and stencil state containing depth and stencil compare and test operations
            // We only use depth tests and want depth tests and writes to be enabled and compare with less or equal
            VkPipelineDepthStencilStateCreateInfo depthStencilState = {};
            depthStencilState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
            depthStencilState.depthTestEnable = VK_TRUE;
            depthStencilState.depthWriteEnable = VK_TRUE;
            depthStencilState.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
            depthStencilState.depthBoundsTestEnable = VK_FALSE;
            depthStencilState.back.failOp = VK_STENCIL_OP_KEEP;
            depthStencilState.back.passOp = VK_STENCIL_OP_KEEP;
            depthStencilState.back.compareOp = VK_COMPARE_OP_ALWAYS;
            depthStencilState.stencilTestEnable = VK_FALSE;
            depthStencilState.front = depthStencilState.back;

            // Multi sampling state
            // This example does not make use fo multi sampling (for anti-aliasing), the state must still be set and passed to the pipeline
            VkPipelineMultisampleStateCreateInfo multisampleState = {};
            multisampleState.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
            multisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            multisampleState.pSampleMask = nullptr;

            // Vertex input descriptions 
            // Specifies the vertex input parameters for a pipeline

            // Vertex input binding
            // This example uses a single vertex input binding at binding point 0 (see vkCmdBindVertexBuffers)
            VkVertexInputBindingDescription vertexInputBinding = {};
            vertexInputBinding.binding = 0;
            vertexInputBinding.stride = sizeof(Vertex);
            vertexInputBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            // Inpute attribute bindings describe shader attribute locations and memory layouts
            std::array<VkVertexInputAttributeDescription, 2> vertexInputAttributs;
            // These match the following shader layout (see triangle.vert):
            //	layout (location = 0) in vec3 inPos;
            //	layout (location = 1) in vec3 inColor;
            // Attribute location 0: Position
            vertexInputAttributs[0].binding = 0;
            vertexInputAttributs[0].location = 0;
            // Position attribute is three 32 bit signed (SFLOAT) floats (R32 G32 B32)
            vertexInputAttributs[0].format = VK_FORMAT_R32G32B32_SFLOAT;
            vertexInputAttributs[0].offset = offsetof(Vertex, position);
            // Attribute location 1: Color
            vertexInputAttributs[1].binding = 0;
            vertexInputAttributs[1].location = 1;
            // Color attribute is three 32 bit signed (SFLOAT) floats (R32 G32 B32)
            vertexInputAttributs[1].format = VK_FORMAT_R32G32B32_SFLOAT;
            vertexInputAttributs[1].offset = offsetof(Vertex, color);

            // Vertex input state used for pipeline creation
            VkPipelineVertexInputStateCreateInfo vertexInputState = {};
            vertexInputState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
            vertexInputState.vertexBindingDescriptionCount = 1;
            vertexInputState.pVertexBindingDescriptions = &vertexInputBinding;
            vertexInputState.vertexAttributeDescriptionCount = 2;
            vertexInputState.pVertexAttributeDescriptions = vertexInputAttributs.data();

            // Shaders
            std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};

            auto vertShader = createShaderModule(R"glsl(
#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 inPos;
layout (location = 1) in vec3 inColor;

layout (binding = 0) uniform UBO 
{
    mat4 projectionMatrix;
    mat4 modelMatrix;
    mat4 viewMatrix;
} ubo;

layout (location = 0) out vec3 outColor;

out gl_PerVertex 
{
    vec4 gl_Position;   
};


void main() 
{
    outColor = inColor;
    gl_Position = ubo.projectionMatrix * ubo.viewMatrix * ubo.modelMatrix * vec4(inPos.xyz, 1.0);
}
)glsl");
            ON_FAIL_RETURN(vertShader != nullptr, EXIT_FAILURE);

            // Vertex shader
            shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            // Set pipeline stage for this shader
            shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
            // Load binary SPIR-V shader
            shaderStages[0].module = vertShader;
            // Main entry point for the shader
            shaderStages[0].pName = "main";
            assert(shaderStages[0].module != VK_NULL_HANDLE);

            auto fragShader = createShaderModule(R"glsl(
#version 450

#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout (location = 0) in vec3 inColor;

layout (location = 0) out vec4 outFragColor;

void main() 
{
    outFragColor = vec4(inColor, 1.0);
})glsl");
            ON_FAIL_RETURN(fragShader != nullptr, EXIT_FAILURE);

            // Fragment shader
            shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            // Set pipeline stage for this shader
            shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            // Load binary SPIR-V shader
            shaderStages[1].module = fragShader;
            // Main entry point for the shader
            shaderStages[1].pName = "main";
            assert(shaderStages[1].module != VK_NULL_HANDLE);

            // Set pipeline shader stage info
            pipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
            pipelineCreateInfo.pStages = shaderStages.data();

            // Assign the pipeline states to the pipeline creation info structure
            pipelineCreateInfo.pVertexInputState = &vertexInputState;
            pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
            pipelineCreateInfo.pRasterizationState = &rasterizationState;
            pipelineCreateInfo.pColorBlendState = &colorBlendState;
            pipelineCreateInfo.pMultisampleState = &multisampleState;
            pipelineCreateInfo.pViewportState = &viewportState;
            pipelineCreateInfo.pDepthStencilState = &depthStencilState;
            pipelineCreateInfo.renderPass = renderPass;
            pipelineCreateInfo.pDynamicState = &dynamicState;

            // Create rendering pipeline using the specified states
            VK_ON_FAIL_RETURN(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipeline), EXIT_FAILURE);

            // Shader modules are no longer needed once the graphics pipeline has been created
            vkDestroyShaderModule(device, shaderStages[0].module, nullptr);
            vkDestroyShaderModule(device, shaderStages[1].module, nullptr);
        }


        {
            // We need to tell the API the number of max. requested descriptors per type
            VkDescriptorPoolSize typeCounts[1];
            // This example only uses one descriptor type (uniform buffer) and only requests one descriptor of this type
            typeCounts[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            typeCounts[0].descriptorCount = 1;
            // For additional types you need to add new entries in the type count list
            // E.g. for two combined image samplers :
            // typeCounts[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            // typeCounts[1].descriptorCount = 2;

            // Create the global descriptor pool
            // All descriptors used in this example are allocated from this pool
            VkDescriptorPoolCreateInfo descriptorPoolInfo = {};
            descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            descriptorPoolInfo.pNext = nullptr;
            descriptorPoolInfo.poolSizeCount = 1;
            descriptorPoolInfo.pPoolSizes = typeCounts;
            // Set the max. number of descriptor sets that can be requested from this pool (requesting beyond this limit will result in an error)
            descriptorPoolInfo.maxSets = 1;

            VK_ON_FAIL_RETURN(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool), EXIT_FAILURE);
        }

        {
            // Allocate a new descriptor set from the global descriptor pool
            VkDescriptorSetAllocateInfo allocInfo = {};
            allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool = descriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &descriptorSetLayout;

            VK_ON_FAIL_RETURN(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet), EXIT_FAILURE);

            // Update the descriptor set determining the shader binding points
            // For every binding point used in a shader there needs to be one
            // descriptor set matching that binding point

            VkWriteDescriptorSet writeDescriptorSet = {};

            // Binding 0 : Uniform buffer
            writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writeDescriptorSet.dstSet = descriptorSet;
            writeDescriptorSet.descriptorCount = 1;
            writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writeDescriptorSet.pBufferInfo = &uniformBufferVS.descriptor;
            // Binds this uniform buffer to binding point 0
            writeDescriptorSet.dstBinding = 0;

            vkUpdateDescriptorSets(device, 1, &writeDescriptorSet, 0, nullptr);
        }

        {
            VkCommandBufferBeginInfo cmdBufInfo = {};
            cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            cmdBufInfo.pNext = nullptr;

            // Set clear values for all framebuffer attachments with loadOp set to clear
            // We use two attachments (color and depth) that are cleared at the start of the subpass and as such we need to set clear values for both
            VkClearValue clearValues[2];
            clearValues[0].color = { { 0.0f, 0.0f, 0.2f, 1.0f } };
            clearValues[1].depthStencil = { 1.0f, 0 };

            VkRenderPassBeginInfo renderPassBeginInfo = {};
            renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            renderPassBeginInfo.pNext = nullptr;
            renderPassBeginInfo.renderPass = renderPass;
            renderPassBeginInfo.renderArea.offset.x = 0;
            renderPassBeginInfo.renderArea.offset.y = 0;
            renderPassBeginInfo.renderArea.extent.width = width;
            renderPassBeginInfo.renderArea.extent.height = height;
            renderPassBeginInfo.clearValueCount = 2;
            renderPassBeginInfo.pClearValues = clearValues;

            for (size_t i = 0; i < drawCmdBuffers.size(); ++i) {
                // Set target frame buffer
                renderPassBeginInfo.framebuffer = frameBuffers[i];

                VK_ON_FAIL_RETURN(vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo), EXIT_FAILURE);

                // Start the first sub pass specified in our default render pass setup by the base class
                // This will clear the color and depth attachment
                vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

                // Update dynamic viewport state
                VkViewport viewport = {};
                viewport.height = (float) height;
                viewport.width = (float) width;
                viewport.minDepth = (float) 0.0f;
                viewport.maxDepth = (float) 1.0f;
                vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

                // Update dynamic scissor state
                VkRect2D scissor = {};
                scissor.extent.width = width;
                scissor.extent.height = height;
                scissor.offset.x = 0;
                scissor.offset.y = 0;
                vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

                // Bind descriptor sets describing shader binding points
                vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

                // Bind the rendering pipeline
                // The pipeline (state object) contains all states of the rendering pipeline, binding it will set all the states specified at pipeline creation time
                vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

                // Bind triangle vertex buffer (contains position and colors)
                VkDeviceSize offsets[1] = { 0 };
                vkCmdBindVertexBuffers(drawCmdBuffers[i], 0, 1, &vertices.buffer, offsets);

                // Bind triangle index buffer
                vkCmdBindIndexBuffer(drawCmdBuffers[i], indices.buffer, 0, VK_INDEX_TYPE_UINT32);

                // Draw indexed triangle
                vkCmdDrawIndexed(drawCmdBuffers[i], indices.count, 1, 0, 0, 1);

                vkCmdEndRenderPass(drawCmdBuffers[i]);

                // Ending the render pass will add an implicit barrier transitioning the frame buffer color attachment to 
                // VK_IMAGE_LAYOUT_PRESENT_SRC_KHR for presenting it to the windowing system

                VK_ON_FAIL_RETURN(vkEndCommandBuffer(drawCmdBuffers[i]), EXIT_FAILURE);
            }
        }

        bool shutdown_requested = false;
        bool KeyboardStateKeyDown[0x100];
        bool keyboard_ignore_next = false;
        
        glm::vec3 camera_position = glm::vec3(0.0f, 0.0f, zoom);
        auto freq = tsc_frequency();
        auto last_frame = tsc();
        u8 frame_counter = 0;
        auto last_display_tsc = last_frame;

        // Map uniform buffer and update it
        decltype(uboVS) *uniform_buffer;
        VK_ON_FAIL_RETURN(vkMapMemory(device, uniformBufferVS.memory, 0, sizeof(uboVS), 0, (void **)&uniform_buffer), EXIT_FAILURE);

        uniform_buffer->projectionMatrix = glm::perspective(glm::radians(60.0f), float(width) / float(height), 0.1f, 256.0f);
        uniform_buffer->viewMatrix = glm::mat4(1.0f);
        uniform_buffer->modelMatrix = glm::mat4(1.0f);

        while (shutdown_requested == false) {

            input_queue->consume_all([&](input* in) {
                switch (in->type) {
                    case input_type::KEYBOARD: {
                        if (keyboard_ignore_next) {
                            keyboard_ignore_next = false;
                            break;
                        }
                        if (in->k.e1) {
                            keyboard_ignore_next = true; // ignore the phantom Num after Break
                            break;
                        }
                        auto key = (in->k.e0 << 7) | in->k.scancode;
                        KeyboardStateKeyDown[key] = (in->k.up == false);
                        if ((in->k.scancode == 0x01) && (in->k.up == false)) {
                            shutdown_requested = true;
                        }
                        break;
                    }

                    case input_type::MOUSE: {
                        constexpr f32 sensitivityX = 0.05f;
                        constexpr f32 sensitivityY = 0.05f;

                        rotation.y += float(in->m.x) * sensitivityX;
                        rotation.x = std::clamp(rotation.x + float(in->m.y) * sensitivityY, -90.0f, 90.0f);

                        if (rotation.y > 180.0f) rotation.y += -360.0f;
                        else if (rotation.y < -180.0f) rotation.y += 360.0f;
                        break;
                    }
                }
                return true;
            });

            {
                glm::mat4 model{ 1.0f };
                model = glm::rotate(model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f)); // pitch
                model = glm::rotate(model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f)); // yaw

                // Update matrices
                // Note: Since we requested a host coherent memory type for the uniform buffer, the write is instantly visible to the GPU
                uniform_buffer->viewMatrix[3] = glm::vec4(camera_position, 1.0f);
                uniform_buffer->modelMatrix = model;
            }

            {
                u32 currentBuffer = 0;
                // Get next image in the swap chain (back/front buffer)
                VK_ON_FAIL_RETURN(
                    vkAcquireNextImageKHR(
                        device,
                        swapChain,
                        UINT64_MAX,
                        presentCompleteSemaphore,
                        (VkFence)nullptr,
                        &currentBuffer
                    ),
                    EXIT_FAILURE
                );

                // Use a fence to wait until the command buffer has finished execution before using it again
                VK_ON_FAIL_RETURN(vkWaitForFences(device, 1, &waitFences[currentBuffer], VK_TRUE, UINT64_MAX), EXIT_FAILURE);
                VK_ON_FAIL_RETURN(vkResetFences(device, 1, &waitFences[currentBuffer]), EXIT_FAILURE);

                // Pipeline stage at which the queue submission will wait (via pWaitSemaphores)
                VkPipelineStageFlags waitStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
                // The submit info structure specifices a command buffer queue submission batch
                VkSubmitInfo submitInfo = {};
                submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                submitInfo.pWaitDstStageMask = &waitStageMask;									// Pointer to the list of pipeline stages that the semaphore waits will occur at
                submitInfo.pWaitSemaphores = &presentCompleteSemaphore;							// Semaphore(s) to wait upon before the submitted command buffer starts executing
                submitInfo.waitSemaphoreCount = 1;												// One wait semaphore						
                submitInfo.pSignalSemaphores = &renderCompleteSemaphore;						// Semaphore(s) to be signaled when command buffers have completed
                submitInfo.signalSemaphoreCount = 1;											// One signal semaphore
                submitInfo.commandBufferCount = 1;												// One command buffer
                submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];					// Command buffers(s) to execute in this batch (submission)

                // Submit to the graphics queue passing a wait fence
                VK_ON_FAIL_RETURN(vkQueueSubmit(queue, 1, &submitInfo, waitFences[currentBuffer]), EXIT_FAILURE);

                // Present the current buffer to the swap chain
                // Pass the semaphore signaled by the command buffer submission from the submit info as the wait semaphore for swap chain presentation
                // This ensures that the image is not presented to the windowing system until all commands have been submitted
                VkPresentInfoKHR presentInfo = {};
                presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
                presentInfo.pNext = NULL;
                presentInfo.swapchainCount = 1;
                presentInfo.pSwapchains = &swapChain;
                presentInfo.pImageIndices = &currentBuffer;
                // Check if a wait semaphore has been specified to wait for before presenting the image
                if (renderCompleteSemaphore != VK_NULL_HANDLE) {
                    presentInfo.pWaitSemaphores = &renderCompleteSemaphore;
                    presentInfo.waitSemaphoreCount = 1;
                }
                VK_ON_FAIL_RETURN(vkQueuePresentKHR(queue, &presentInfo), EXIT_FAILURE);
            }

            last_frame = tsc();
            frame_counter += 1;
            if (frame_counter == 0) {
                LOG_INFO("Time for 256 frames: ", double(last_frame - last_display_tsc) / double(freq));
                last_display_tsc = last_frame;
            }
        }

        // Unmap
        vkUnmapMemory(device, uniformBufferVS.memory);
    }

    // shutdown sequence
    {
        auto tid = GetWindowThreadProcessId(window, nullptr);
        PostThreadMessage(tid, WM_QUIT, 0, 0);
        input_thread.join();
    }
    {
        belog::shutdown();
        logging_thread.join();
    }

    return EXIT_SUCCESS;
}
