#pragma once

#include "vulkan/vulkan.h"

namespace vk::utils {

#define VK_GET_INSTANCE_PROC_ADDR(instance, function) \
    auto function = reinterpret_cast<PFN_##function>(vkGetInstanceProcAddr(instance, #function));

#define VK_GET_DEVICE_PROC_ADDR(device, function) \
    auto function = reinterpret_cast<PFN_##function>(vkGetDeviceProcAddr(device, #function));

#define VK_ON_FAIL_RETURN(condition, retval) ON_FAIL_EVAL_TRACE_RETURN((condition), _result_ == VK_SUCCESS, (retval), #condition " returned ", vk::utils::errorString(_result_))
#define VK_ON_FAIL_RETURN_VOID(condition) VK_ON_FAIL_RETURN((condition), (void)0)
#define VK_ON_FAIL_RELAY(condition) VK_ON_FAIL_RETURN((condition), _result_)

#define VK_ON_ERROR_RETURN(condition, retval) ON_FAIL_EVAL_TRACE_RETURN((condition), _result_ >= VK_SUCCESS, (retval), #condition " returned ", vk::utils::errorString(_result_))
#define VK_ON_ERROR_RETURN_VOID(condition) VK_ON_ERROR_RETURN((condition), (void)0)
#define VK_ON_ERROR_RELAY(condition) VK_ON_ERROR_RETURN((condition), _result_)

__declspec(noinline) const char* errorString(VkResult res) {
    switch (res) {
#define STR(r) case VK_ ##r: return #r
        STR(SUCCESS);
        STR(NOT_READY);
        STR(TIMEOUT);
        STR(EVENT_SET);
        STR(EVENT_RESET);
        STR(INCOMPLETE);
        STR(ERROR_OUT_OF_HOST_MEMORY);
        STR(ERROR_OUT_OF_DEVICE_MEMORY);
        STR(ERROR_INITIALIZATION_FAILED);
        STR(ERROR_DEVICE_LOST);
        STR(ERROR_MEMORY_MAP_FAILED);
        STR(ERROR_LAYER_NOT_PRESENT);
        STR(ERROR_EXTENSION_NOT_PRESENT);
        STR(ERROR_FEATURE_NOT_PRESENT);
        STR(ERROR_INCOMPATIBLE_DRIVER);
        STR(ERROR_TOO_MANY_OBJECTS);
        STR(ERROR_FORMAT_NOT_SUPPORTED);
        STR(ERROR_FRAGMENTED_POOL);
        STR(ERROR_OUT_OF_POOL_MEMORY);
        STR(ERROR_INVALID_EXTERNAL_HANDLE);
        STR(ERROR_SURFACE_LOST_KHR);
        STR(ERROR_NATIVE_WINDOW_IN_USE_KHR);
        STR(SUBOPTIMAL_KHR);
        STR(ERROR_OUT_OF_DATE_KHR);
        STR(ERROR_INCOMPATIBLE_DISPLAY_KHR);
        STR(ERROR_VALIDATION_FAILED_EXT);
        STR(ERROR_INVALID_SHADER_NV);
        STR(ERROR_FRAGMENTATION_EXT);
        STR(ERROR_NOT_PERMITTED_EXT);
#undef STR
    }

    return "UNKNOWN_ERROR";
}

} // namespace vk::utils
