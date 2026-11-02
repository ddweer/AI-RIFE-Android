// Minimal stub for the RIFE build. The Lossless.dll PE parsing, the DXBC→
// SPIR-V translation and the three SPIR-V disk caches are gone; only the
// error-code contract above and this constant-false availability probe
// remain so callers elsewhere in the app keep compiling.

#include "android_shader_loader.hpp"

namespace lsfg_android {

bool fp16_shaders_available(const std::string & /*cacheDir*/) {
    return false;
}

} // namespace lsfg_android
