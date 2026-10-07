// WD2SkyFix: ReShade add-on that fixes Watch Dogs 2's sky and shadow flicker on current GPUs.
//
// Cause: the compute shader that computes the sky's ambient lighting every frame (DXBC checksum e90c6810...; 256
// threads in one group project the sky into spherical harmonics) sums the threads' 29 float4 partial results in
// place in a UAV (SkyTempAccumulationBuffer) in 8 halving steps, with no barrier at all. Threads read other warps'
// partial sums before they are written, so the result changes from dispatch to dispatch even with identical
// inputs. That result (SkyBuffer.m_cloudAndSkyIrradianceSH) is the ambient light of the volumetric fog and of
// sky-lit surfaces, so they flicker. Whether the race shows depends on how the GPU schedules warps: it is visible
// on current NVIDIA (RTX 20 and newer) and AMD GPUs.
//
// Fix: when the game creates that shader, keep its per-sample code and its final write byte for byte and replace
// the 8 summing steps with a groupshared tree reduction of one field at a time, with barriers (dxbc_patch.h,
// RewriteSkyReduction). Any other shader, or a shader with this checksum but a different structure, is left alone.
#define NOMINMAX
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <vector>

#include <reshade.hpp>

#include "dxbc_patch.h"

using namespace reshade::api;

extern "C" __declspec(dllexport) const char* NAME = "WD2SkyFix";
extern "C" __declspec(dllexport) const char* DESCRIPTION =
    "Fixes the sky and shadow flicker in Watch Dogs 2 on current GPUs (adds the missing barriers to the sky-light compute shader).";

namespace {

// DXBC checksum of the game's shader (bytes 4..19 of the blob): e90c6810a486972691b6f798b5e2b0e0
constexpr uint8_t kSkyLightSh[16] = {0xe9, 0x0c, 0x68, 0x10, 0xa4, 0x86, 0x97, 0x26, 0x91, 0xb6, 0xf7, 0x98, 0xb5, 0xe2, 0xb0, 0xe0};

std::mutex g_mx;
std::deque<std::vector<uint8_t>> g_blobs;  // patched bytecode, kept alive for the device's lifetime

void LogMsg(reshade::log::level lvl, const char* fmt, int a = 0)
{
    char buf[256];
    std::snprintf(buf, sizeof(buf), fmt, a);
    reshade::log::message(lvl, buf);
}

bool OnCreatePipeline(device*, pipeline_layout, uint32_t count, const pipeline_subobject* subs)
{
    bool changed = false;
    for (uint32_t i = 0; i < count; ++i) {
        if (subs[i].type != pipeline_subobject_type::compute_shader || !subs[i].data)
            continue;
        auto* sd = static_cast<shader_desc*>(subs[i].data);
        if (!sd->code || sd->code_size < 32 || std::memcmp(static_cast<const uint8_t*>(sd->code) + 4, kSkyLightSh, 16) != 0)
            continue;
        std::vector<uint8_t> patched;
        if (!dxbc_patch::RewriteSkyReduction(static_cast<const uint8_t*>(sd->code), sd->code_size, patched)) {
            LogMsg(reshade::log::level::warning, "WD2SkyFix: sky-light compute shader found but its structure is not as expected; left as is");
            continue;
        }
        std::lock_guard<std::mutex> lock(g_mx);
        g_blobs.push_back(std::move(patched));
        sd->code = g_blobs.back().data();
        sd->code_size = g_blobs.back().size();
        changed = true;
        LogMsg(reshade::log::level::info, "WD2SkyFix: sky-light compute shader patched (summing steps rewritten, %d bytes)",
               (int)sd->code_size);
    }
    return changed;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        if (!reshade::register_addon(module))
            return FALSE;
        reshade::register_event<reshade::addon_event::create_pipeline>(OnCreatePipeline);
        break;
    case DLL_PROCESS_DETACH:
        reshade::unregister_addon(module);
        break;
    }
    return TRUE;
}
