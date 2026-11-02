#include "rife_net.hpp"
#include "rife_ops.h"

#include <android/log.h>

#include <cstring>

#include "gpu.h"

#define LOG_TAG_RIFE "rife-framegen"
#define LOGR(level, ...) __android_log_print(level, LOG_TAG_RIFE, __VA_ARGS__)

namespace {

ncnn::Layer* Warp_layer_creator(void* /*userdata*/)
{
    return new Warp;
}

} // namespace

RifeNet::RifeNet()
    : loaded(false)
{
}

RifeNet::~RifeNet()
{
    // ncnn::Net releases its resources in its own destructor.
}

bool RifeNet::load(const std::function<std::vector<uint8_t>(const std::string&)>& loader, bool fp16)
{
    ncnn::VulkanDevice* vkdev = ncnn::get_gpu_device(0);

    ncnn::Option opt;
    opt.num_threads = 4;
    opt.use_vulkan_compute = vkdev != nullptr;
    opt.use_fp16_packed = fp16 && vkdev != nullptr;
    opt.use_fp16_storage = fp16 && vkdev != nullptr;
    opt.use_fp16_arithmetic = false;
    opt.use_int8_storage = true;

    flownet.opt = opt;
    if (vkdev)
    {
        flownet.set_vulkan_device(vkdev);
    }

    flownet.register_custom_layer("rife.Warp", Warp_layer_creator);

    const std::vector<uint8_t> paramData = loader("flownet.param");
    if (paramData.empty())
    {
        LOGR(ANDROID_LOG_ERROR, "flownet.param unavailable via loader");
        return false;
    }
    // load_param_mem expects a NUL-terminated string.
    std::string paramStr(reinterpret_cast<const char*>(paramData.data()), paramData.size());
    paramStr.push_back('\0');
    if (flownet.load_param_mem(paramStr.c_str()) != 0)
    {
        LOGR(ANDROID_LOG_ERROR, "flownet load_param_mem failed (%zu bytes)", paramData.size());
        return false;
    }

    const std::vector<uint8_t> binData = loader("flownet.bin");
    if (binData.empty())
    {
        LOGR(ANDROID_LOG_ERROR, "flownet.bin unavailable via loader");
        return false;
    }
    if (flownet.load_model(reinterpret_cast<const unsigned char*>(binData.data())) != 0)
    {
        LOGR(ANDROID_LOG_ERROR, "flownet load_model failed (%zu bytes)", binData.size());
        return false;
    }

    loaded = true;
    LOGR(ANDROID_LOG_INFO, "RIFE flownet loaded (fp16=%d, vulkan=%d)",
         fp16 ? 1 : 0, vkdev != nullptr ? 1 : 0);
    return true;
}

bool RifeNet::run(const ncnn::Mat& in0, const ncnn::Mat& in1, float t, ncnn::Mat& out)
{
    if (!loaded)
        return false;

    const int w = in0.w;
    const int h = in0.h;
    if (in1.w != w || in1.h != h || in0.c != 3 || in1.c != 3)
        return false;

    // RIFE v4 expects the input extent to be a multiple of 32; pad with
    // zeros and crop the output afterwards.
    const int wPad = (w + 31) / 32 * 32;
    const int hPad = (h + 31) / 32 * 32;

    ncnn::Mat pad0(wPad, hPad, 3);
    ncnn::Mat pad1(wPad, hPad, 3);
    for (int c = 0; c < 3; c++)
    {
        float* dst0 = pad0.channel(c);
        float* dst1 = pad1.channel(c);
        const float* src0 = in0.channel(c);
        const float* src1 = in1.channel(c);
        if (wPad == w && hPad == h)
        {
            memcpy(dst0, src0, (size_t)w * h * sizeof(float));
            memcpy(dst1, src1, (size_t)w * h * sizeof(float));
        }
        else
        {
            memset(dst0, 0, (size_t)wPad * hPad * sizeof(float));
            memset(dst1, 0, (size_t)wPad * hPad * sizeof(float));
            for (int y = 0; y < h; y++)
            {
                memcpy(dst0 + (size_t)y * wPad, src0 + (size_t)y * w, (size_t)w * sizeof(float));
                memcpy(dst1 + (size_t)y * wPad, src1 + (size_t)y * w, (size_t)w * sizeof(float));
            }
        }
    }

    // in2: single-channel full-resolution map filled with the timestep.
    ncnn::Mat timestep(wPad, hPad, 1);
    timestep.fill(t);

    ncnn::Extractor ex = flownet.create_extractor();
    if (ex.input("in0", pad0) != 0)
        return false;
    if (ex.input("in1", pad1) != 0)
        return false;
    if (ex.input("in2", timestep) != 0)
        return false;

    ncnn::Mat outPad;
    if (ex.extract("out0", outPad) != 0)
        return false;
    if (outPad.w < w || outPad.h < h || outPad.c != 3)
        return false;

    out.create(w, h, 3);
    if (out.empty())
        return false;
    for (int c = 0; c < 3; c++)
    {
        const float* src = outPad.channel(c);
        float* dst = out.channel(c);
        if (outPad.w == w)
        {
            memcpy(dst, src, (size_t)w * h * sizeof(float));
        }
        else
        {
            for (int y = 0; y < h; y++)
                memcpy(dst + (size_t)y * w, src + (size_t)y * outPad.w, (size_t)w * sizeof(float));
        }
    }
    return true;
}
