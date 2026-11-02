// Public LSFG_3_1 / LSFG_3_1P API surface. Both namespaces delegate to the
// same ncnn+RIFE implementation; the headers carry the visibility("default")
// attributes so the symbols stay exported under -fvisibility=hidden.

#include "lsfg_3_1.hpp"
#include "lsfg_3_1p.hpp"
#include "framegen_impl.hpp"

#include <stdexcept>

#ifdef __ANDROID__
#include <android/hardware_buffer.h>
#endif

namespace {

rife_framegen::Loader toLoader(
    const std::function<std::vector<uint8_t>(const std::string&)>& loader)
{
    return rife_framegen::Loader(loader);
}

} // namespace

// ---------------------------------------------------------------------------
// LSFG_3_1
// ---------------------------------------------------------------------------

void LSFG_3_1::initialize(uint64_t deviceUUID,
    bool isHdr, float flowScale, uint64_t generationCount,
    const std::function<std::vector<uint8_t>(const std::string&)>& loader)
{
    rife_framegen::initialize(deviceUUID, isHdr, flowScale, generationCount, toLoader(loader));
}

int32_t LSFG_3_1::createContext(int in0, int in1, const std::vector<int>& outN,
    VkExtent2D extent, VkFormat format)
{
    (void)in0;
    (void)in1;
    (void)outN;
    (void)extent;
    (void)format;
    throw std::runtime_error("FD contexts not supported in RIFE build");
}

#ifdef __ANDROID__

int32_t LSFG_3_1::createContextFromAHB(AHardwareBuffer* in0, AHardwareBuffer* in1,
    const std::vector<AHardwareBuffer*>& outN, VkExtent2D extent, VkFormat format)
{
    return rife_framegen::createContextFromAHB(in0, in1, outN, extent, format);
}

#endif // __ANDROID__

void LSFG_3_1::presentContext(int32_t id, int inSem, const std::vector<int>& outSem)
{
    rife_framegen::presentContext(id, inSem, outSem);
}

void LSFG_3_1::deleteContext(int32_t id)
{
    rife_framegen::deleteContext(id);
}

void LSFG_3_1::finalize()
{
    rife_framegen::finalize();
}

#ifdef __ANDROID__

void LSFG_3_1::waitIdle()
{
    rife_framegen::waitIdle();
}

#endif // __ANDROID__

// ---------------------------------------------------------------------------
// LSFG_3_1P
// ---------------------------------------------------------------------------

void LSFG_3_1P::initialize(uint64_t deviceUUID,
    bool isHdr, float flowScale, uint64_t generationCount,
    const std::function<std::vector<uint8_t>(const std::string&)>& loader)
{
    rife_framegen::initialize(deviceUUID, isHdr, flowScale, generationCount, toLoader(loader));
}

int32_t LSFG_3_1P::createContext(int in0, int in1, const std::vector<int>& outN,
    VkExtent2D extent, VkFormat format)
{
    (void)in0;
    (void)in1;
    (void)outN;
    (void)extent;
    (void)format;
    throw std::runtime_error("FD contexts not supported in RIFE build");
}

#ifdef __ANDROID__

int32_t LSFG_3_1P::createContextFromAHB(AHardwareBuffer* in0, AHardwareBuffer* in1,
    const std::vector<AHardwareBuffer*>& outN, VkExtent2D extent, VkFormat format)
{
    return rife_framegen::createContextFromAHB(in0, in1, outN, extent, format);
}

#endif // __ANDROID__

void LSFG_3_1P::presentContext(int32_t id, int inSem, const std::vector<int>& outSem)
{
    rife_framegen::presentContext(id, inSem, outSem);
}

void LSFG_3_1P::deleteContext(int32_t id)
{
    rife_framegen::deleteContext(id);
}

void LSFG_3_1P::finalize()
{
    rife_framegen::finalize();
}

#ifdef __ANDROID__

void LSFG_3_1P::waitIdle()
{
    rife_framegen::waitIdle();
}

#endif // __ANDROID__
