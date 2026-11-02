#!/usr/bin/env python3
"""Patch the LSFG-Android app sources for the ncnn+RIFE framegen swap."""
import io
import sys

APP = r"C:\Users\dd841\.cache\lsfg-build\LSFG-Android-Application\app\src\main\cpp"


def read(path):
    with io.open(path, "r", encoding="utf-8", newline=None) as f:
        return f.read()


def write(path, text):
    with io.open(path, "r", encoding="utf-8", newline="") as f:
        raw = f.read(4096)
    eol = "\r\n" if "\r\n" in raw else "\n"
    text = text.replace("\n", eol)
    with io.open(path, "w", encoding="utf-8", newline="") as f:
        f.write(text)


def replace_once(text, old, new, what):
    count = text.count(old)
    if count != 1:
        sys.exit("FAILED (%s): expected 1 occurrence, found %d" % (what, count))
    return text.replace(old, new, 1)


def replace_span(text, start_marker, end_marker, new, what):
    start = text.find(start_marker)
    if start < 0:
        sys.exit("FAILED (%s): start marker not found" % what)
    end = text.find(end_marker, start)
    if end < 0:
        sys.exit("FAILED (%s): end marker not found" % what)
    return text[:start] + new + text[end:]


# ---------------------------------------------------------------------------
# lsfg_render_loop.cpp
# ---------------------------------------------------------------------------
p = APP + r"\lsfg_render_loop.cpp"
t = read(p)

t = replace_once(
    t,
    '#include "ahb_image_bridge.hpp"\n#include "android_shader_loader.hpp"\n',
    '#include "ahb_image_bridge.hpp"\n',
    "render_loop: drop shader_loader include")

t = replace_once(
    t,
    "#include <cstring>\n",
    "#include <cstdio>\n#include <cstring>\n#include <fstream>\n",
    "render_loop: add cstdio/fstream includes")

new_init_framegen = '''bool initFramegen(const char *cacheDir) {
    const std::string cache(cacheDir ? cacheDir : "");

    // RIFE backend: the framegen library only ever requests the two model
    // files ("flownet.param" / "flownet.bin"), which the Application class
    // extracts from the APK assets into <cacheDir>/rife/ at startup. Any
    // other name means something is off — log it and return empty so the
    // model load fails loudly.
    auto loader = [cache](const std::string &name) -> std::vector<uint8_t> {
        if (name != "flownet.param" && name != "flownet.bin") {
            LOGW("framegen requested unexpected resource '%s' — not available in RIFE build",
                 name.c_str());
            return {};
        }
        const std::string path = cache + "/rife/" + name;
        std::ifstream f(path, std::ifstream::binary);
        if (!f) {
            LOGE("failed to open %s", path.c_str());
            return {};
        }
        f.seekg(0, std::ifstream::end);
        const std::streamoff size = f.tellg();
        f.seekg(0, std::ifstream::beg);
        if (size <= 0) {
            LOGE("empty model file %s", path.c_str());
            return {};
        }
        std::vector<uint8_t> data(static_cast<size_t>(size));
        f.read(reinterpret_cast<char *>(data.data()), size);
        if (!f) {
            LOGE("failed to read %s", path.c_str());
            return {};
        }
        return data;
    };

    LOGI("Initializing RIFE (ncnn) framegen, model dir: %s/rife", cache.c_str());

    try {
        if (g.performanceMode) {
            LSFG_3_1P::initialize(g.vk.deviceUuid, g.hdr, g.flowScale,
                                  static_cast<uint64_t>(g.multiplier), loader);
        } else {
            LSFG_3_1::initialize(g.vk.deviceUuid, g.hdr, g.flowScale,
                                 static_cast<uint64_t>(g.multiplier), loader);
        }
        return true;
    } catch (const std::exception &e) {
        LOGE("LSFG_3_1::initialize threw: %s — model missing or GPU init failed. Continuing in capture-only mode.", e.what());
        return false;
    } catch (...) {
        LOGE("LSFG_3_1::initialize threw unknown exception");
        return false;
    }
}

'''

t = replace_span(
    t,
    "bool initFramegen(const char *cacheDir) {",
    "bool createFramegenContext() {",
    new_init_framegen,
    "render_loop: replace initFramegen")

write(p, t)
print("patched lsfg_render_loop.cpp")

# ---------------------------------------------------------------------------
# lsfg_jni.cpp
# ---------------------------------------------------------------------------
p = APP + r"\lsfg_jni.cpp"
t = read(p)

t = replace_once(
    t,
    'constexpr const char *kVersion = "lsfg-android 0.1.2";',
    'constexpr const char *kVersion = "lsfg-android 0.2.0-rife";',
    "jni: kVersion")

t = replace_once(
    t,
    "#include <android/hardware_buffer_jni.h>\n#include <android/native_window_jni.h>\n#include <jni.h>\n\n#include <string>\n",
    "#include <android/hardware_buffer_jni.h>\n#include <android/log.h>\n#include <android/native_window_jni.h>\n#include <jni.h>\n\n#include <cstdio>\n#include <string>\n",
    "jni: includes")

t = replace_once(
    t,
    '''extern "C" JNIEXPORT jint JNICALL
Java_com_lsfg_android_session_NativeBridge_extractShaders(
        JNIEnv *env, jobject /*thiz*/,
        jstring dllPath, jstring /*dllSha256*/, jstring cacheDir) {
    const std::string path = jstring_to_std(env, dllPath);
    const std::string cache = jstring_to_std(env, cacheDir);
    if (path.empty() || cache.empty()) {
        return lsfg_android::kErrDllUnreadable;
    }
    return lsfg_android::extract_dll_to_spirv(path, cache);
}''',
    '''extern "C" JNIEXPORT jint JNICALL
Java_com_lsfg_android_session_NativeBridge_extractShaders(
        JNIEnv * /*env*/, jobject /*thiz*/,
        jstring /*dllPath*/, jstring /*dllSha256*/, jstring /*cacheDir*/) {
    // RIFE build: framegen runs on the bundled ncnn model (assets/rife), so
    // Lossless.dll extraction is no longer required. Kept so the Kotlin side
    // keeps working unchanged.
    __android_log_print(ANDROID_LOG_INFO, "lsfg-jni",
                        "extractShaders: not required in RIFE build");
    return lsfg_android::kOk;
}''',
    "jni: extractShaders")

t = replace_once(
    t,
    '''extern "C" JNIEXPORT jint JNICALL
Java_com_lsfg_android_session_NativeBridge_probeShaders(
        JNIEnv *env, jobject /*thiz*/, jstring cacheDir) {
    return lsfg_android::probe_shaders_on_device(jstring_to_std(env, cacheDir));
}''',
    '''extern "C" JNIEXPORT jint JNICALL
Java_com_lsfg_android_session_NativeBridge_probeShaders(
        JNIEnv * /*env*/, jobject /*thiz*/, jstring /*cacheDir*/) {
    __android_log_print(ANDROID_LOG_INFO, "lsfg-jni",
                        "probeShaders: not required in RIFE build");
    return lsfg_android::kOk;
}''',
    "jni: probeShaders")

t = replace_once(
    t,
    '''    // Sanity check: at least one cached SPIR-V file must exist. Without shaders,
    // LSFG_3_1::initialize() will throw inside framegen (we catch it but the
    // pipeline will be useless anyway). Failing fast here gives a cleaner error
    // path back to Kotlin so the service can stay in mirror mode.
    auto probeShader = lsfg_android::load_cached_spirv(cache, 255);
    if (probeShader.empty()) {
        return lsfg_android::kErrMissingResource;
    }''',
    '''    // Sanity check: the RIFE model files must have been extracted from the
    // APK assets into <cacheDir>/rife/ before the native pipeline can start.
    // Failing fast here gives a cleaner error path back to Kotlin so the
    // service can stay in mirror mode.
    const auto modelFileExists = [&cache](const char *name) {
        const std::string path = cache + "/rife/" + name;
        FILE *fp = std::fopen(path.c_str(), "rb");
        if (fp == nullptr) return false;
        std::fclose(fp);
        return true;
    };
    if (!modelFileExists("flownet.param") || !modelFileExists("flownet.bin")) {
        return lsfg_android::kErrMissingResource;
    }''',
    "jni: initContext model probe")

write(p, t)
print("patched lsfg_jni.cpp")

# ---------------------------------------------------------------------------
# android_vk_probe.cpp
# ---------------------------------------------------------------------------
p = APP + r"\android_vk_probe.cpp"
t = read(p)

t = replace_once(
    t,
    '#include "android_shader_loader.hpp"\n#include "android_vk_probe.hpp"\n',
    '#include "android_vk_probe.hpp"\n',
    "vk_probe: drop shader_loader include")

t = replace_span(
    t,
    "namespace {\n\nconstexpr uint32_t kAllResourceIds[]",
    "bool device_supports_float16()",
    '''int probe_shaders_on_device(const std::string & /*cacheDir*/) {
    // RIFE build: there are no cached SPIR-V shader blobs to validate — the
    // framegen pipeline runs on the bundled ncnn model and compiles its warp
    // shader through ncnn's runtime compiler.
    LOGI("probe_shaders_on_device: not required in RIFE build");
    return kOk;
}

''',
    "vk_probe: replace probe_shaders_on_device")

write(p, t)
print("patched android_vk_probe.cpp")

print("ALL PATCHES APPLIED")
