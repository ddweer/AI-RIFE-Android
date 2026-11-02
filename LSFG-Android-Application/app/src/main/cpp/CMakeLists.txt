cmake_minimum_required(VERSION 3.22.1)
project(lsfg-android LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Reach into the Linux project tree for sources we want to reuse as-is.
set(LSFG_ROOT "${CMAKE_SOURCE_DIR}/../../../../../lsfg-vk-android")
if(NOT EXISTS "${LSFG_ROOT}/framegen/CMakeLists.txt")
    message(FATAL_ERROR "Can't find lsfg-vk sources at ${LSFG_ROOT}. Did you clone the submodules?")
endif()

# -----------------------------------------------------------------------------
# volk — Vulkan loader. On Android we enable the Android surface platform.
# We avoid add_subdirectory() because volk's default install/include rules get
# in the way; compiling the two-file library directly is trivial.
# -----------------------------------------------------------------------------
add_library(volk STATIC "${LSFG_ROOT}/thirdparty/volk/volk.c")
target_include_directories(volk PUBLIC "${LSFG_ROOT}/thirdparty/volk")
target_compile_definitions(volk PUBLIC VK_USE_PLATFORM_ANDROID_KHR=1)

# -----------------------------------------------------------------------------
# pe-parse and dxbc removed in 0.2.2: shaders are now bundled as precompiled
# SPIR-V under src/main/assets/shaders/. No PE parsing or DXBC→SPIR-V
# translation is needed on device.
# -----------------------------------------------------------------------------

# -----------------------------------------------------------------------------
# framegen — the Vulkan frame-generation pipeline. Reused verbatim.
#
# Tell Gradle's externalNativeBuild task that this project's "input set"
# extends into the submodule sources. Without this, AGP's incremental build
# only watches files under app/src/main/cpp/ and silently skips re-running
# CMake when files in ../../../../../lsfg-vk-android/framegen/src change —
# leaving the cached lsfg-vk-framegen.a in place even after a real source
# edit. CMAKE_CONFIGURE_DEPENDS makes the configure step depend on every
# .cpp/.hpp under the framegen tree, so any edit there triggers a full
# regeneration on the next build.
# -----------------------------------------------------------------------------
file(GLOB_RECURSE FRAMEGEN_WATCH_SOURCES CONFIGURE_DEPENDS
    "${LSFG_ROOT}/framegen/src/*.cpp"
    "${LSFG_ROOT}/framegen/src/*.hpp"
    "${LSFG_ROOT}/framegen/include/*.hpp"
    "${LSFG_ROOT}/framegen/public/*.hpp"
    "${LSFG_ROOT}/framegen/v3.1_src/*.cpp"
    "${LSFG_ROOT}/framegen/v3.1p_src/*.cpp"
)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${FRAMEGEN_WATCH_SOURCES}
)
add_subdirectory("${LSFG_ROOT}/framegen" "${CMAKE_BINARY_DIR}/framegen" EXCLUDE_FROM_ALL)

# -----------------------------------------------------------------------------
# lsfg-android — JNI shared library.
# Phase 2 only exposes nativeVersion() plus placeholder symbols; later phases
# add the real shader extraction and Vulkan context entry points.
# -----------------------------------------------------------------------------
find_library(log-lib log)
find_library(neuralnetworks-lib neuralnetworks)

add_library(lsfg-android SHARED
    lsfg_jni.cpp
    android_shader_loader.cpp
    android_vk_probe.cpp
    android_vk_session.cpp
    ahb_image_bridge.cpp
    gpu_postprocess.cpp
    lsfg_render_loop.cpp
    nnapi_npu.cpp
    nnapi_postprocess.cpp
    cpu_postprocess.cpp
    crash_reporter.cpp
)

target_link_libraries(lsfg-android
    PRIVATE
        lsfg-vk-framegen
        volk
        ${log-lib}
        ${neuralnetworks-lib}
        android
)
