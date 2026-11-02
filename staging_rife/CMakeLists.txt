cmake_minimum_required(VERSION 3.22.1)
project(lsfg-android LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Reach into the Linux project tree for volk (Vulkan loader). The framegen
# pipeline itself now lives in rife_framegen/ (ncnn + RIFE v4) instead of
# being reused from lsfg-vk-android.
set(LSFG_ROOT "${CMAKE_SOURCE_DIR}/../../../../../lsfg-vk-android")
if(NOT EXISTS "${LSFG_ROOT}/thirdparty/volk/volk.c")
    message(FATAL_ERROR "Can't find volk sources at ${LSFG_ROOT}/thirdparty/volk.")
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
# ncnn — prebuilt android-vulkan release (20250503). Layout:
#   ${NCNN_ROOT}/include/ncnn/*.h
#   ${NCNN_ROOT}/lib/${ANDROID_ABI}/libncnn.a
# The prebuilt was compiled with -fno-rtti, OpenMP enabled, and its Vulkan
# backend references glslang for runtime GLSL compilation (see the zip's
# lib/cmake/ncnn/ncnn.cmake). Consumers must therefore also link the glslang
# archives shipped in the same zip, a static OpenMP runtime, dl and
# jnigraphics, and must not emit RTTI for classes deriving from ncnn::Layer
# (libncnn.a contains no typeinfo for it).
# -----------------------------------------------------------------------------
set(NCNN_ROOT "C:/Users/dd841/.cache/android/ncnn-android-vulkan")
if(NOT EXISTS "${NCNN_ROOT}/include/ncnn/net.h")
    message(FATAL_ERROR "ncnn headers not found at ${NCNN_ROOT}/include/ncnn — unpack ncnn-android-vulkan.zip first")
endif()
if(NOT EXISTS "${NCNN_ROOT}/lib/${ANDROID_ABI}/libncnn.a")
    message(FATAL_ERROR "libncnn.a for ABI ${ANDROID_ABI} not found under ${NCNN_ROOT}/lib — unpack ncnn-android-vulkan.zip first")
endif()
add_library(ncnn STATIC IMPORTED)
set_target_properties(ncnn PROPERTIES
    IMPORTED_LOCATION "${NCNN_ROOT}/lib/${ANDROID_ABI}/libncnn.a"
)
target_include_directories(ncnn INTERFACE "${NCNN_ROOT}/include/ncnn")

# glslang archives from the same prebuilt zip. All glslang code is merged
# into libglslang.a; libSPIRV.a is a near-empty stub linked only for
# interface parity with the official ncnn.cmake.
add_library(glslang STATIC IMPORTED)
set_target_properties(glslang PROPERTIES
    IMPORTED_LOCATION "${NCNN_ROOT}/lib/${ANDROID_ABI}/libglslang.a"
)
add_library(SPIRV STATIC IMPORTED)
set_target_properties(SPIRV PROPERTIES
    IMPORTED_LOCATION "${NCNN_ROOT}/lib/${ANDROID_ABI}/libSPIRV.a"
)

find_library(log-lib log)
find_library(jnigraphics-lib jnigraphics)
find_library(dl-lib dl)
find_library(neuralnetworks-lib neuralnetworks)

# -----------------------------------------------------------------------------
# rife-framegen — the LSFG_3_1 / LSFG_3_1P implementation backed by ncnn and
# the RIFE v4 flownet. lsfg_api.cpp includes the public headers from the
# lsfg-vk-android tree to inherit their visibility("default") attributes.
#
# -fno-rtti matches the ncnn prebuilt build: the custom Warp layer derives
# from ncnn::Layer and must not reference its (nonexistent) typeinfo.
# Exceptions stay enabled — std::runtime_error is part of the API contract.
# -----------------------------------------------------------------------------
add_library(rife-framegen STATIC
    rife_framegen/lsfg_api.cpp
    rife_framegen/framegen_impl.cpp
    rife_framegen/rife_net.cpp
    rife_framegen/warp.cpp
)
target_include_directories(rife-framegen
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/rife_framegen"
        "${LSFG_ROOT}/framegen/public"
)
target_compile_options(rife-framegen PRIVATE -fno-rtti)
target_link_libraries(rife-framegen
    PUBLIC
        volk
        ncnn
        glslang
        SPIRV
        ${log-lib}
)

# -----------------------------------------------------------------------------
# lsfg-android — JNI shared library.
# -----------------------------------------------------------------------------
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

target_include_directories(lsfg-android
    PRIVATE
        "${LSFG_ROOT}/framegen/public"
)

target_link_libraries(lsfg-android
    PRIVATE
        rife-framegen
        volk
        ncnn
        glslang
        SPIRV
        ${log-lib}
        ${neuralnetworks-lib}
        ${jnigraphics-lib}
        ${dl-lib}
        android
)

# libncnn.a was built with OpenMP; satisfy its __kmpc_* references with the
# NDK's static OpenMP runtime (the same flags the official ncnn.cmake uses).
target_link_options(lsfg-android PRIVATE -fopenmp -static-openmp)
