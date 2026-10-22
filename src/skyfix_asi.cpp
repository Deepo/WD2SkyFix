// WD2SkyFix.asi: the same fix as the ReShade add-on (skyfix.cpp), without ReShade.
//
// When the game creates the sky-light compute shader (DXBC checksum e90c6810...), the shader's 8 summing steps are
// replaced with a groupshared reduction with barriers (dxbc_patch.h, RewriteSkyReduction); skyfix.cpp has the cause.
// Any other shader, or a shader with this checksum but a different structure, is left alone.
//
// Loaded from the game's bin folder at start-up by an ASI loader: ours (dinput8.dll, asiloader.cpp) or Ultimate ASI
// Loader. How it gets at the shader:
//   - inline hooks (safetyhook) on D3D11CreateDevice and D3D11CreateDeviceAndSwapChain in d3d11.dll;
//   - on each device they return, ID3D11Device::CreateComputeShader (vtable slot 18) is redirected here, once per
//     distinct vtable. With ReShade installed the hooks see ReShade's wrapper device, d3d11's own device or both,
//     depending on hook order; the game's call passes through here either way, because a wrapper calls the real
//     device's method.
// If the runtime refuses the rewritten shader, the original is created instead, so the game never loses its shader.
// Only in WatchDogs2.exe, and nothing at all when Easy Anti-Cheat is found (eac.h).
// Log: WD2SkyFix.log next to the .asi (the previous start: WD2SkyFix.prev.log). On Steam the game starts twice (a
// short-lived first process restarts it through Ubisoft Connect), so the log is the second, real process.
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <vector>

#include <safetyhook.hpp>

#include "asi.h"
#include "dxbc_patch.h"
#include "eac.h"
#include "version.h"

namespace {

// DXBC checksum of the game's shader (bytes 4..19 of the blob): e90c6810a486972691b6f798b5e2b0e0
constexpr uint8_t kSkyLightSh[16] = {0xe9, 0x0c, 0x68, 0x10, 0xa4, 0x86, 0x97, 0x26, 0x91, 0xb6, 0xf7, 0x98, 0xb5, 0xe2, 0xb0, 0xe0};

// ID3D11DeviceVtbl in d3d11.h: QueryInterface, AddRef, Release, then the Create* methods; CreateComputeShader is 18.
constexpr size_t kCreateComputeShaderSlot = 18;

}  // namespace

wchar_t asi::g_dir[MAX_PATH] = {};

void asi::Log(const wchar_t* fmt, ...)
{
    static std::mutex mx;
    std::lock_guard<std::mutex> lock(mx);
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%sWD2SkyFix.log", g_dir);
    FILE* f = nullptr;
    if (_wfopen_s(&f, path, L"a, ccs=UTF-8") != 0 || !f)
        return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fwprintf(f, L"%04u-%02u-%02u %02u:%02u:%02u.%03u [pid %lu] ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
             t.wSecond, t.wMilliseconds, GetCurrentProcessId());
    va_list ap;
    va_start(ap, fmt);
    vfwprintf(f, fmt, ap);
    va_end(ap);
    fputwc(L'\n', f);
    fclose(f);
}

namespace {

using asi::g_dir;
using asi::Log;

// The file name of the module an address is in ("d3d11.dll", "dxgi.dll" for ReShade, ...).
const wchar_t* ModuleOf(const void* address, wchar_t (&buf)[MAX_PATH])
{
    HMODULE m = nullptr;
    buf[0] = 0;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCWSTR>(address), &m))
        GetModuleFileNameW(m, buf, MAX_PATH);
    const wchar_t* slash = wcsrchr(buf, L'\\');
    return slash ? slash + 1 : (buf[0] ? buf : L"?");
}

// ------------------------------------------------------------------ CreateComputeShader
using CreateComputeShaderFn = HRESULT(STDMETHODCALLTYPE*)(ID3D11Device*, const void*, SIZE_T, ID3D11ClassLinkage*,
                                                          ID3D11ComputeShader**);

struct Redirect {
    void** vtable;
    CreateComputeShaderFn original;
};
std::mutex g_mx;
std::vector<Redirect> g_redirects;  // one per distinct device vtable, never removed

CreateComputeShaderFn OriginalFor(ID3D11Device* device)
{
    void** const vtable = *reinterpret_cast<void***>(device);
    std::lock_guard<std::mutex> lock(g_mx);
    for (const Redirect& r : g_redirects)
        if (r.vtable == vtable)
            return r.original;
    return nullptr;
}

HRESULT STDMETHODCALLTYPE CreateComputeShaderHook(ID3D11Device* self, const void* code, SIZE_T size,
                                                  ID3D11ClassLinkage* linkage, ID3D11ComputeShader** shader)
{
    const CreateComputeShaderFn original = OriginalFor(self);
    if (!original) {  // only redirected vtables lead here, so this cannot happen
        Log(L"CreateComputeShader called on an object whose vtable was never redirected: E_FAIL");
        return E_FAIL;
    }
    if (code && size >= 32 && std::memcmp(static_cast<const uint8_t*>(code) + 4, kSkyLightSh, 16) == 0) {
        std::vector<uint8_t> patched;
        if (!dxbc_patch::RewriteSkyReduction(static_cast<const uint8_t*>(code), size, patched)) {
            Log(L"sky-light compute shader found but its structure is not as expected; left as is");
        }
        else {
            // D3D11 copies the bytecode during the call, so the patched blob need not outlive it.
            const HRESULT hr = original(self, patched.data(), patched.size(), linkage, shader);
            if (SUCCEEDED(hr)) {
                Log(L"sky-light compute shader patched (summing steps rewritten, %zu bytes)", patched.size());
                return hr;
            }
            Log(L"sky-light compute shader: the patched version was refused (0x%08lx); creating the original instead",
                static_cast<unsigned long>(hr));
        }
    }
    return original(self, code, size, linkage, shader);
}

void RedirectDevice(ID3D11Device* device, const wchar_t* via)
{
    void** const vtable = *reinterpret_cast<void***>(device);
    std::lock_guard<std::mutex> lock(g_mx);
    for (const Redirect& r : g_redirects)
        if (r.vtable == vtable)
            return;
    void* const current = vtable[kCreateComputeShaderSlot];
    if (current == reinterpret_cast<void*>(&CreateComputeShaderHook))
        return;  // a copy of a vtable redirected before: calling "the original" would loop
    DWORD protect = 0;
    if (!VirtualProtect(&vtable[kCreateComputeShaderSlot], sizeof(void*), PAGE_EXECUTE_READWRITE, &protect)) {
        Log(L"%s returned a device, but its vtable could not be made writable (error %lu): not redirected", via, GetLastError());
        return;
    }
    // Recorded before the switch, so a call arriving right after it finds its original (it waits for g_mx).
    g_redirects.push_back({vtable, reinterpret_cast<CreateComputeShaderFn>(current)});
    InterlockedExchangePointer(&vtable[kCreateComputeShaderSlot], reinterpret_cast<void*>(&CreateComputeShaderHook));
    VirtualProtect(&vtable[kCreateComputeShaderSlot], sizeof(void*), protect, &protect);
    wchar_t vtMod[MAX_PATH], fnMod[MAX_PATH];
    Log(L"%s returned a device: CreateComputeShader redirected (vtable in %s, original in %s)", via,
        ModuleOf(vtable, vtMod), ModuleOf(current, fnMod));
}

// ------------------------------------------------------------------ device creation
SafetyHookInline g_createDevice, g_createDeviceAndSwapChain;

// unsafe_call, not call: call() takes the hook's mutex for the whole call, and device creation can re-enter the
// other hook (ReShade builds D3D11CreateDeviceAndSwapChain on D3D11CreateDevice). The hooks are never removed.
HRESULT WINAPI D3D11CreateDeviceHook(IDXGIAdapter* adapter, D3D_DRIVER_TYPE type, HMODULE software, UINT flags,
                                     const D3D_FEATURE_LEVEL* levels, UINT count, UINT sdk, ID3D11Device** device,
                                     D3D_FEATURE_LEVEL* level, ID3D11DeviceContext** context)
{
    const HRESULT hr = g_createDevice.unsafe_call<HRESULT>(adapter, type, software, flags, levels, count, sdk, device, level, context);
    if (SUCCEEDED(hr) && device && *device)
        RedirectDevice(*device, L"D3D11CreateDevice");
    return hr;
}

HRESULT WINAPI D3D11CreateDeviceAndSwapChainHook(IDXGIAdapter* adapter, D3D_DRIVER_TYPE type, HMODULE software, UINT flags,
                                                 const D3D_FEATURE_LEVEL* levels, UINT count, UINT sdk,
                                                 const DXGI_SWAP_CHAIN_DESC* desc, IDXGISwapChain** swapchain,
                                                 ID3D11Device** device, D3D_FEATURE_LEVEL* level, ID3D11DeviceContext** context)
{
    const HRESULT hr = g_createDeviceAndSwapChain.unsafe_call<HRESULT>(adapter, type, software, flags, levels, count, sdk,
                                                                       desc, swapchain, device, level, context);
    if (SUCCEEDED(hr) && device && *device)
        RedirectDevice(*device, L"D3D11CreateDeviceAndSwapChain");
    return hr;
}

bool HookExport(HMODULE module, const char* name, void* detour, SafetyHookInline& hook)
{
    void* const target = reinterpret_cast<void*>(GetProcAddress(module, name));
    if (!target) {
        Log(L"%hs not found in d3d11.dll", name);
        return false;
    }
    // Created disabled and enabled once stored, so no call can reach the detour before the hook is in place.
    auto created = safetyhook::InlineHook::create(target, detour, safetyhook::InlineHook::StartDisabled);
    if (!created) {
        Log(L"%hs could not be hooked (safetyhook error %d)", name, static_cast<int>(created.error().type));
        return false;
    }
    hook = std::move(*created);
    if (!hook.enable()) {
        Log(L"%hs could not be hooked (enabling failed)", name);
        return false;
    }
    return true;
}

void Install()
{
    HMODULE d3d11 = GetModuleHandleW(L"d3d11.dll");
    const bool loaded = d3d11 != nullptr;
    if (!d3d11)
        d3d11 = LoadLibraryW(L"d3d11.dll");  // by name, so it is the same d3d11.dll the game gets
    if (!d3d11) {
        Log(L"d3d11.dll could not be loaded (error %lu): nothing installed", GetLastError());
        return;
    }
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(d3d11, path, MAX_PATH);
    const int hooked = HookExport(d3d11, "D3D11CreateDevice", reinterpret_cast<void*>(&D3D11CreateDeviceHook), g_createDevice) +
                       HookExport(d3d11, "D3D11CreateDeviceAndSwapChain",
                                  reinterpret_cast<void*>(&D3D11CreateDeviceAndSwapChainHook), g_createDeviceAndSwapChain);
    Log(L"%d of 2 device creation functions hooked in %s (%s); waiting for the game's device", hooked, path,
        loaded ? L"already loaded" : L"loaded now");
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    // No DisableThreadLibraryCalls: the static C runtime needs the thread notifications.
    if (reason != DLL_PROCESS_ATTACH)
        return TRUE;
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const wchar_t* exeName = wcsrchr(exe, L'\\');
    if (!exeName || _wcsicmp(exeName + 1, L"WatchDogs2.exe") != 0)
        return TRUE;  // not the game: do nothing, and keep its log
    GetModuleFileNameW(module, g_dir, MAX_PATH);
    if (wchar_t* slash = wcsrchr(g_dir, L'\\'))
        slash[1] = 0;

    wchar_t log[MAX_PATH], prev[MAX_PATH];
    swprintf_s(log, L"%sWD2SkyFix.log", g_dir);
    swprintf_s(prev, L"%sWD2SkyFix.prev.log", g_dir);
    MoveFileExW(log, prev, MOVEFILE_REPLACE_EXISTING);
    const wchar_t* cmd = GetCommandLineW();
    const wchar_t* args = wcsstr(cmd, L".exe");
    Log(L"WD2SkyFix %hs (.asi) in %s", VERSION_STRING, exe);
    Log(L"arguments:%s", args ? args + 4 + (args[4] == L'"') : L" ?");
    if (const wchar_t* eac = EacFound()) {
        Log(L"Easy Anti-Cheat found (%s): nothing installed. The fix needs Easy Anti-Cheat off (launch option -eac_launcher).", eac);
        return TRUE;
    }
    try {
        Install();
    }
    catch (...) {
        Log(L"sky fix: stopped by an error; the lines above show how far it got");
    }
    try {
        intro_skip::Install();
    }
    catch (...) {
        Log(L"start-up skip: stopped by an error; the lines above show how far it got");
    }
    return TRUE;
}
