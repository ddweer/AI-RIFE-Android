// Bundled SPIR-V shader loader (replaces Lossless.dll-based extraction).
//
// The app now ships precompiled SPIR-V shaders under src/main/assets/shaders/.
// On first launch the Kotlin side copies them into <cacheDir>, and this file
// reads them back from disk via load_cached_spirv(). No PE parsing, no
// DXBC→SPIR-V translation, and no Lossless.dll dependency remain.

#include "android_shader_loader.hpp"

#include <android/log.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <sys/stat.h>
#include <unordered_map>
#include <vector>

#define LOG_TAG "lsfg-shaders"
#define LOGE(...) ::lsfg_android::ring_logf(LOG_TAG, ANDROID_LOG_ERROR, __VA_ARGS__)
#define LOGI(...) ::lsfg_android::ring_logf(LOG_TAG, ANDROID_LOG_INFO,  __VA_ARGS__)

#include "crash_reporter.hpp"

namespace lsfg_android {

std::vector<uint8_t> load_cached_spirv(const std::string &cacheDir, uint32_t resId,
                                       ShaderCache source) {
    char path[512];
    switch (source) {
        case ShaderCache::Fp16Spirv:
            std::snprintf(path, sizeof(path), "%s/fp16/%u.spv", cacheDir.c_str(), resId);
            break;
        case ShaderCache::Fp32Spirv:
            std::snprintf(path, sizeof(path), "%s/fp32/%u.spv", cacheDir.c_str(), resId);
            break;
        case ShaderCache::Dxbc:
        default:
            std::snprintf(path, sizeof(path), "%s/%u.spv", cacheDir.c_str(), resId);
            break;
    }
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        LOGE("Shader not found: %s", path);
        return {};
    }
    const auto size = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> out(static_cast<size_t>(size));
    f.read(reinterpret_cast<char *>(out.data()), static_cast<std::streamsize>(size));
    LOGI("Loaded bundled shader %s (%zu bytes)", path, out.size());
    return out;
}

// Mirror of Extract::nameIdxTable in lsfg-vk-android/src/extract/extract.cpp.
// (Steam Deck side). Framegen asks for shaders by symbolic name; on Android
// we cache them on disk by numeric resource ID, so we need to translate.
uint32_t shader_name_to_resource_id(const std::string &name) {
    static const std::unordered_map<std::string, uint32_t> kTable = {
        { "mipmaps",     255 },
        { "alpha[0]",    267 },
        { "alpha[1]",    268 },
        { "alpha[2]",    269 },
        { "alpha[3]",    270 },
        { "beta[0]",     275 },
        { "beta[1]",     276 },
        { "beta[2]",     277 },
        { "beta[3]",     278 },
        { "beta[4]",     279 },
        { "gamma[0]",    257 },
        { "gamma[1]",    259 },
        { "gamma[2]",    260 },
        { "gamma[3]",    261 },
        { "gamma[4]",    262 },
        { "delta[0]",    257 },
        { "delta[1]",    263 },
        { "delta[2]",    264 },
        { "delta[3]",    265 },
        { "delta[4]",    266 },
        { "delta[5]",    258 },
        { "delta[6]",    271 },
        { "delta[7]",    272 },
        { "delta[8]",    273 },
        { "delta[9]",    274 },
        { "generate",    256 },
        { "p_mipmaps",   255 },
        { "p_alpha[0]",  290 },
        { "p_alpha[1]",  291 },
        { "p_alpha[2]",  292 },
        { "p_alpha[3]",  293 },
        { "p_beta[0]",   298 },
        { "p_beta[1]",   299 },
        { "p_beta[2]",   300 },
        { "p_beta[3]",   301 },
        { "p_beta[4]",   302 },
        { "p_gamma[0]",  280 },
        { "p_gamma[1]",  282 },
        { "p_gamma[2]",  283 },
        { "p_gamma[3]",  284 },
        { "p_gamma[4]",  285 },
        { "p_delta[0]",  280 },
        { "p_delta[1]",  286 },
        { "p_delta[2]",  287 },
        { "p_delta[3]",  288 },
        { "p_delta[4]",  289 },
        { "p_delta[5]",  281 },
        { "p_delta[6]",  294 },
        { "p_delta[7]",  295 },
        { "p_delta[8]",  296 },
        { "p_delta[9]",  297 },
        { "p_generate",  256 },
    };
    auto it = kTable.find(name);
    return it == kTable.end() ? 0u : it->second;
}

uint32_t shader_name_to_resource_id_fp16(const std::string &name) {
    const uint32_t dxbcId = shader_name_to_resource_id(name);
    if (dxbcId == 0) {
        return 0;
    }
    const uint32_t fp16Id = dxbcId + 49u;
    if (fp16Id < 304 || fp16Id > 351) {
        return 0;
    }
    return fp16Id;
}

bool fp16_shaders_available(const std::string &cacheDir) {
    for (uint32_t dxbcId = 255u; dxbcId <= 302u; ++dxbcId) {
        const uint32_t fp16Id = dxbcId + 49u;
        if (fp16Id < 304 || fp16Id > 351) continue;
        char path[512];
        std::snprintf(path, sizeof(path), "%s/fp16/%u.spv", cacheDir.c_str(), fp16Id);
        struct stat st{};
        if (stat(path, &st) != 0 || st.st_size < 20) {
            return false;
        }
    }
    return true;
}

uint32_t shader_name_to_resource_id_fp32_spirv(const std::string &name) {
    const uint32_t dxbcId = shader_name_to_resource_id(name);
    if (dxbcId == 0) {
        return 0;
    }
    const uint32_t fp32Id = dxbcId + 98u;
    if (fp32Id < 353 || fp32Id > 400) {
        return 0;
    }
    return fp32Id;
}

bool fp32_spirv_shaders_available(const std::string &cacheDir) {
    for (uint32_t dxbcId = 255u; dxbcId <= 302u; ++dxbcId) {
        const uint32_t fp32Id = dxbcId + 98u;
        if (fp32Id < 353 || fp32Id > 400) continue;
        char path[512];
        std::snprintf(path, sizeof(path), "%s/fp32/%u.spv", cacheDir.c_str(), fp32Id);
        struct stat st{};
        if (stat(path, &st) != 0 || st.st_size < 20) {
            return false;
        }
    }
    return true;
}

} // namespace lsfg_android
