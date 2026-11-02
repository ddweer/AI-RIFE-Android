#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace lsfg_android {

// Error codes returned to Kotlin via JNI. Keep kOk == 0.
constexpr int kOk = 0;
constexpr int kErrShaderMissing = -1;
constexpr int kErrShaderLoadFailed = -2;

// Source identifier for load_cached_spirv.
enum class ShaderCache {
    Dxbc,       // <cacheDir>/<id>.spv         (bundled SPIR-V, DXBC-equivalent)
    Fp16Spirv,  // <cacheDir>/fp16/<id>.spv    (bundled FP16 SPIR-V)
    Fp32Spirv,  // <cacheDir>/fp32/<id>.spv    (bundled FP32 SPIR-V)
};

// Reads a cached SPIR-V file back from disk. Returns an empty vector on
// missing/unreadable files — the caller decides whether that's a fatal error.
// The Kotlin side is responsible for copying bundled shaders from
// src/main/assets/shaders/ into <cacheDir> on first launch.
std::vector<uint8_t> load_cached_spirv(const std::string &cacheDir, uint32_t resId,
                                       ShaderCache source = ShaderCache::Dxbc);

// Maps a framegen shader name (e.g. "p_mipmaps", "p_alpha[2]", "generate")
// to the DXBC resource ID (255..302) that lives on disk as <cacheDir>/<id>.spv.
// Returns 0 if the name is unknown.
//
// Mirror of Extract::nameIdxTable in lsfg-vk-android/src/extract/extract.cpp.
uint32_t shader_name_to_resource_id(const std::string &name);

// Same lookup, but returning the SPIR-V FP16 resource ID (304..351).
uint32_t shader_name_to_resource_id_fp16(const std::string &name);

// Returns true when the FP16 cache directory contains every shader in the
// 304..351 range.
bool fp16_shaders_available(const std::string &cacheDir);

// Same lookup as shader_name_to_resource_id but returning the FP32 SPIR-V
// resource ID (353..400).
uint32_t shader_name_to_resource_id_fp32_spirv(const std::string &name);

// Returns true when the FP32 SPIR-V cache directory contains every shader in
// the 353..400 range.
bool fp32_spirv_shaders_available(const std::string &cacheDir);

} // namespace lsfg_android
