cmake_minimum_required(VERSION 3.8 FATAL_ERROR)

project(RawInputTest)

set(CMAKE_CONFIGURATION_TYPES Debug Release)

if (MSVC)
    set(CMAKE_CXX_FLAGS         "/W4 /EHsc /GR- /permissive- /std:c++latest /fp:fast /Zi /MP /Zc:rvalueCast /arch:AVX2")
    set(CMAKE_CXX_FLAGS         "${CMAKE_CXX_FLAGS} /wd4324")
    set(CMAKE_CXX_FLAGS_RELEASE "/MT  /O2 /Ob2 /Oi /Ot /Oy /GL /FA /Gy /DNDEBUG")
    set(CMAKE_CXX_FLAGS_DEBUG   "/MTd /Od /Ob0 /D_DEBUG")

    set(CMAKE_EXE_LINKER_FLAGS         "/machine:x64 /map")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/debug:fastlink /INCREMENTAL:NO /LTCG /opt:ref /opt:icf")
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG   "/debug          /INCREMENTAL")
endif()

find_package(Vulkan 1.1 REQUIRED)
add_subdirectory(ext/glm EXCLUDE_FROM_ALL)
add_subdirectory(ext/benchmark bin)

set(headers
    src/aligned_alloc.hpp
    src/best_effort_logger.hpp
    src/bitfield.hpp
    src/compile_time_utilities.hpp
    src/cpuid.hpp
    src/log_utils.hpp
    src/scope_guard.hpp
    src/simd_primitives.hpp
    src/spsc_queue.hpp
    src/spsc_ring_buffer.hpp
    src/stack.hpp
    src/threads.hpp
    src/types.hpp
    src/vulkan_utils.hpp
    src/bitmanip.hpp
)

set(sources
    src/main.cpp
    src/best_effort_logger.cpp
    src/threads.cpp
    src/compile_time_utilities.cpp
    src/cpuid.cpp
)

if (MSVC)
    list(APPEND sources 
        src/msvc/bitmanip.cpp
    )
endif()

source_group("Header Files" FILES ${headers})
source_group("Source Files" FILES ${sources})

add_executable(RawInputTest WIN32
    ${headers}
    ${sources}
)

target_include_directories(RawInputTest PRIVATE Vulkan::Vulkan)
target_link_libraries(RawInputTest Vulkan::Vulkan glm)

if (WIN32)
    target_compile_definitions(RawInputTest PRIVATE VK_USE_PLATFORM_WIN32_KHR)
    target_link_libraries(RawInputTest PowrProf.lib)
endif()

add_executable(RingBufferBenchmark
    test/RingBufferBenchmark.cpp
)
target_link_libraries(RingBufferBenchmark benchmark)
target_include_directories(RingBufferBenchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
