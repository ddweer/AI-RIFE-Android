#pragma once

#include <string>

namespace lsfg_android {

// Error codes returned to Kotlin via JNI. Keep kOk == 0.
constexpr int kOk = 0;
constexpr int kErrDllUnreadable = -1;
constexpr int kErrMissingResource = -2;
constexpr int kErrTranslationFailed = -3;
constexpr int kErrWriteFailed = -4;

// RIFE build: the Lossless.dll FP16 SPIR-V cache no longer exists (no DLL,
// no shader extraction). Kept only so the JNI layer's
// isFramegenFp16Supported keeps compiling; always reports false so the UI
// hides the legacy FP16 shader toggle. ncnn handles fp16 internally.
bool fp16_shaders_available(const std::string &cacheDir);

} // namespace lsfg_android
