// dinput8.dll: the ASI loader shipped with WD2SkyFix.asi, a dinput8.dll stand-in that loads the *.asi files next to
// it when Watch Dogs 2 starts (in place of Ultimate ASI Loader, which works for the .asi too).
//
// Why dinput8: the game's engine DLL (bin\Disrupt_64.dll) imports DINPUT8.dll, a regular import in the Steam copy
// and loaded by name at start-up in the Ubisoft Connect copy (whose import table the copy protection rebuilds), so a
// dinput8.dll in bin is loaded with the engine, before any engine code runs. dinput8 is not a KnownDLL, so Windows
// takes it from the game folder; ReShade uses dxgi.dll. It exports 6 functions, forwarded below.
//
// What it does when loaded (only in WatchDogs2.exe, and nothing when Easy Anti-Cheat is found, eac.h):
//   - LoadLibrary every *.asi in its folder, in name order;
//   - write what it loaded, and the error of anything that failed, to WD2AsiLoader.log next to it (the previous
//     start: WD2AsiLoader.prev.log).
// DirectInput calls are forwarded to System32\dinput8.dll, loaded on first use (not in DllMain).
#include <windows.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

#include "eac.h"
#include "version.h"

static wchar_t g_dir[MAX_PATH] = {};

static void Log(const wchar_t* fmt, ...)
{
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%sWD2AsiLoader.log", g_dir);
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

static void LoadPlugins()
{
    std::vector<std::wstring> names;
    WIN32_FIND_DATAW fd;
    const HANDLE find = FindFirstFileW((std::wstring(g_dir) + L"*.asi").c_str(), &fd);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                names.emplace_back(fd.cFileName);
        } while (FindNextFileW(find, &fd));
        FindClose(find);
    }
    std::sort(names.begin(), names.end(), [](const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
    Log(L"%zu plugin(s) in %s", names.size(), g_dir);
    for (const std::wstring& name : names) {
        const std::wstring path = std::wstring(g_dir) + name;
        if (const HMODULE m = LoadLibraryW(path.c_str()))
            Log(L"loaded %s at %p", name.c_str(), static_cast<void*>(m));
        else
            Log(L"FAILED to load %s: error %lu", name.c_str(), GetLastError());
    }
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason != DLL_PROCESS_ATTACH)
        return TRUE;
    wchar_t self[MAX_PATH], exe[MAX_PATH];
    GetModuleFileNameW(module, self, MAX_PATH);
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    wcscpy_s(g_dir, self);
    if (wchar_t* slash = wcsrchr(g_dir, L'\\'))
        slash[1] = 0;
    // Only the game itself: stay out of any other process that might pick this file up (no log either, so it can't
    // rotate the game's log away).
    const wchar_t* exe_name = wcsrchr(exe, L'\\');
    if (!exe_name || _wcsicmp(exe_name + 1, L"WatchDogs2.exe") != 0)
        return TRUE;

    wchar_t log[MAX_PATH], prev[MAX_PATH];
    swprintf_s(log, L"%sWD2AsiLoader.log", g_dir);
    swprintf_s(prev, L"%sWD2AsiLoader.prev.log", g_dir);
    MoveFileExW(log, prev, MOVEFILE_REPLACE_EXISTING);
    Log(L"WD2AsiLoader %hs (dinput8.dll) in %s", VERSION_STRING, exe);
    if (const wchar_t* eac = EacFound()) {
        Log(L"Easy Anti-Cheat found (%s): no plugins loaded. Mods need Easy Anti-Cheat off (launch option -eac_launcher).", eac);
        return TRUE;
    }
    LoadPlugins();
    return TRUE;
}

// ------------------------------------------------------------------ DirectInput forwarding
static FARPROC Real(const char* name)
{
    static HMODULE real = [] {
        wchar_t path[MAX_PATH];
        GetSystemDirectoryW(path, MAX_PATH);
        wcscat_s(path, L"\\dinput8.dll");
        return LoadLibraryW(path);
    }();
    return real ? GetProcAddress(real, name) : nullptr;
}

template <typename F>
static F RealAs(const char* name)
{
    return reinterpret_cast<F>(Real(name));
}

extern "C" {

HRESULT WINAPI Proxy_DirectInput8Create(HINSTANCE instance, DWORD version, REFIID iid, LPVOID* out, LPUNKNOWN outer)
{
    static const auto fn = RealAs<decltype(&Proxy_DirectInput8Create)>("DirectInput8Create");
    return fn ? fn(instance, version, iid, out, outer) : E_FAIL;
}

HRESULT WINAPI Proxy_DllCanUnloadNow()
{
    // While a plugin is loaded through this DLL it must stay.
    return S_FALSE;
}

HRESULT WINAPI Proxy_DllGetClassObject(REFCLSID clsid, REFIID iid, LPVOID* out)
{
    static const auto fn = RealAs<decltype(&Proxy_DllGetClassObject)>("DllGetClassObject");
    return fn ? fn(clsid, iid, out) : E_FAIL;
}

HRESULT WINAPI Proxy_DllRegisterServer()
{
    static const auto fn = RealAs<decltype(&Proxy_DllRegisterServer)>("DllRegisterServer");
    return fn ? fn() : E_FAIL;
}

HRESULT WINAPI Proxy_DllUnregisterServer()
{
    static const auto fn = RealAs<decltype(&Proxy_DllUnregisterServer)>("DllUnregisterServer");
    return fn ? fn() : E_FAIL;
}

void* WINAPI Proxy_GetdfDIJoystick()
{
    static const auto fn = RealAs<decltype(&Proxy_GetdfDIJoystick)>("GetdfDIJoystick");
    return fn ? fn() : nullptr;
}

}  // extern "C"
