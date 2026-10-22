// WD2SkyFix.asi, part 2: the start-up skip. The mod is for the game with Easy Anti-Cheat off anyway, so it also cuts
// the wait before the main menu. WD2SkyFix.ini next to the .asi, [Startup] SkipIntro: 1 = skip (also when the ini or
// the key is missing), 0 = the game's normal start-up.
//
// The start-up (Steam, 2026-10-22 log, ~34 s from the process start to the main menu): engine start ~5 s, the
// Ubisoft/DedSec logo video (7.5 s), the NVIDIA logo video (7.4 s), then the boot animation and the "login" screens
// (~14.5 s, UI pages, not videos), then the main menu with its background video.
//
// With SkipIntro (Steam and Ubisoft Connect, 2026-10-22: the main menu ~7-9 s after the process start):
//   - The two logo videos are not opened: BinkOpen (hooked in bink2w64.dll) returns 0 for them, as for a missing file.
//   - The boot screens run on timers (the disk is idle; no key skips them), so the engine's clock runs 20 times
//     faster until the main menu: ~2.2 s instead of ~14.5 s, ~1.5 s of it real loading.
// Always: the first Bink videos the game opens and closes are logged with their length, for players' logs.
// [Debug] StartupSamples=1: disk reads and CPU time every half second of the start-up.
// Tried and dropped: the engine's own -nointrovideo switch (added through a GetCommandLineW hook): both logos and the
// boot screens still played. Removing the boot pages outright would mean patching the engine's (copy-protected,
// per-store) menu code for ~1 s more.
#define NOMINMAX
#include <windows.h>
#include <intrin.h>

#include <cstdint>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <vector>

#include <safetyhook.hpp>

#include "asi.h"

using asi::Log;

namespace {

// ------------------------------------------------------------------ settings
// How much faster the engine clock runs through the boot screens. Fixed, not a setting: 2026-10-22 tests at 8, 20
// and 30 (boot screens 3.0, 2.2 and 2.1 s; ~1.5 s of it is real loading), so more gains little and only shortens any
// engine timeouts further. Falls back to 1 (off) if the clock hooks can't be installed.
double g_bootSpeed = 20.0;

bool SkipIntroSetting()
{
    wchar_t ini[MAX_PATH];
    swprintf_s(ini, L"%sWD2SkyFix.ini", asi::g_dir);
    const bool exists = GetFileAttributesW(ini) != INVALID_FILE_ATTRIBUTES;
    const UINT v = GetPrivateProfileIntW(L"Startup", L"SkipIntro", 1, ini);
    Log(L"WD2SkyFix.ini: %s; [Startup] SkipIntro = %u", exists ? L"found" : L"not found (defaults)", v);
    return v != 0;
}

// ------------------------------------------------------------------ Bink video log
// The start of Bink's BINK struct (bink.h): Width, Height, Frames, FrameNum, LastFrameNum, FrameRate, FrameRateDiv.
struct BinkHead {
    uint32_t width, height, frames, frameNum, lastFrameNum, frameRate, frameRateDiv;
};
constexpr uint32_t kBinkFileHandle = 0x00800000;  // BINKFILEHANDLE: "name" is a file handle
constexpr uint32_t kBinkFromMemory = 0x04000000;  // BINKFROMMEMORY: "name" is the video in memory ("KB2j..." in game)

// SEH, so no C++ objects in these two.
bool ReadHead(const void* bink, BinkHead& out)
{
    __try {
        std::memcpy(&out, bink, sizeof(out));
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void CopyName(const char* name, char* out, size_t n)
{
    __try {
        size_t i = 0;
        for (; i + 1 < n && name[i]; ++i)
            out[i] = name[i] >= 0x20 && name[i] < 0x7f ? name[i] : '?';  // one log line, plain ASCII
        out[i] = 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        strcpy_s(out, n, "?");
    }
}

struct OpenVideo {
    void* bink;
    ULONGLONG opened;
    char name[160];
};
std::mutex g_videoMx;
std::vector<OpenVideo> g_videos;
SafetyHookInline g_binkOpen, g_binkClose;

bool g_skip = false;                  // [Startup] SkipIntro
volatile LONG g_menuVideoOpened = 0;  // the main menu's background video has opened: the start-up is over

// ------------------------------------------------------------------ faster engine clock for the boot screens
// The epilepsy warning and the "login" screens after the logos wait ~13.5 s with the disk idle (2026-10-22 samples) and
// no key skips them, so they run on timers. From the skipped NVIDIA logo until the main menu's video opens (at most
// kWarpMaxSeconds of real time), the clocks the engine reads (QueryPerformanceCounter, GetTickCount(64), timeGetTime)
// run g_bootSpeed (20) times faster for calls from Disrupt_64.dll only; audio, D3D and Bink keep real time. Engine
// time never goes back: after the window it keeps the lead it gained.
constexpr double kWarpMaxSeconds = 30.0;
SafetyHookInline g_qpc, g_tick, g_tick64, g_timeGetTime;
uintptr_t g_engineBase = 0, g_engineEnd = 0;
LONGLONG g_freq = 1;
SRWLOCK g_warpLock = SRWLOCK_INIT;
volatile LONG g_warpPhase = 0;  // 0 not yet, 1 running, 2 over
LONGLONG g_warpStart = 0;       // real QPC at the start
LONGLONG g_extra = 0;           // QPC ticks the engine's clock is ahead (fixed in phase 2)

LONGLONG RealQpc()
{
    LARGE_INTEGER v;
    g_qpc.unsafe_call<BOOL>(&v);
    return v.QuadPart;
}

bool FromEngine(const void* ret)
{
    const auto a = reinterpret_cast<uintptr_t>(ret);
    return a >= g_engineBase && a < g_engineEnd;
}

void WarpStop(LONGLONG now, const wchar_t* why)
{
    AcquireSRWLockExclusive(&g_warpLock);
    if (g_warpPhase == 1) {
        g_extra = static_cast<LONGLONG>((now - g_warpStart) * (g_bootSpeed - 1.0));
        InterlockedExchange(&g_warpPhase, 2);
        Log(L"boot screens: engine clock back to normal speed (%s) after %.1f s, %.1f s ahead", why,
            static_cast<double>(now - g_warpStart) / g_freq, static_cast<double>(g_extra) / g_freq);
    }
    ReleaseSRWLockExclusive(&g_warpLock);
}

// QPC ticks the engine's clock is ahead, and the real QPC it goes with.
LONGLONG Extra(LONGLONG& now)
{
    const LONG phase = g_warpPhase;
    if (phase != 1) {
        now = RealQpc();
        return phase == 2 ? g_extra : 0;
    }
    AcquireSRWLockShared(&g_warpLock);
    now = RealQpc();
    LONGLONG extra = g_warpPhase == 1 ? static_cast<LONGLONG>((now - g_warpStart) * (g_bootSpeed - 1.0)) : g_extra;
    const bool expired = g_warpPhase == 1 && now - g_warpStart > static_cast<LONGLONG>(kWarpMaxSeconds * g_freq);
    ReleaseSRWLockShared(&g_warpLock);
    if (expired)
        WarpStop(now, L"time limit");
    return extra;
}

BOOL WINAPI QpcHook(LARGE_INTEGER* out)
{
    if (!out || g_warpPhase == 0 || !FromEngine(_ReturnAddress()))
        return g_qpc.unsafe_call<BOOL>(out);
    LONGLONG now;
    const LONGLONG extra = Extra(now);
    out->QuadPart = now + extra;
    return TRUE;
}

ULONGLONG ExtraMs()
{
    LONGLONG now;
    return static_cast<ULONGLONG>(Extra(now) * 1000 / g_freq);
}

DWORD WINAPI GetTickCountHook()
{
    const DWORD real = g_tick.unsafe_call<DWORD>();
    return g_warpPhase == 0 || !FromEngine(_ReturnAddress()) ? real : real + static_cast<DWORD>(ExtraMs());
}

ULONGLONG WINAPI GetTickCount64Hook()
{
    const ULONGLONG real = g_tick64.unsafe_call<ULONGLONG>();
    return g_warpPhase == 0 || !FromEngine(_ReturnAddress()) ? real : real + ExtraMs();
}

DWORD WINAPI TimeGetTimeHook()
{
    const DWORD real = g_timeGetTime.unsafe_call<DWORD>();
    return g_warpPhase == 0 || !FromEngine(_ReturnAddress()) ? real : real + static_cast<DWORD>(ExtraMs());
}

bool HookExport(const wchar_t* dll, const char* name, void* detour, SafetyHookInline& hook)
{
    HMODULE m = GetModuleHandleW(dll);
    if (!m)
        m = LoadLibraryW(dll);
    void* const target = m ? reinterpret_cast<void*>(GetProcAddress(m, name)) : nullptr;
    if (!target)
        return false;
    auto created = safetyhook::InlineHook::create(target, detour, safetyhook::InlineHook::StartDisabled);
    if (!created)
        return false;
    hook = std::move(*created);
    return static_cast<bool>(hook.enable());
}

void InstallClockHooks()
{
    const HMODULE engine = GetModuleHandleW(L"Disrupt_64.dll");
    if (!engine) {
        Log(L"boot screens: Disrupt_64.dll is not loaded yet; not sped up");
        return;
    }
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(engine);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(reinterpret_cast<const uint8_t*>(engine) + dos->e_lfanew);
    g_engineBase = reinterpret_cast<uintptr_t>(engine);
    g_engineEnd = g_engineBase + nt->OptionalHeader.SizeOfImage;
    LARGE_INTEGER f;
    QueryPerformanceFrequency(&f);
    g_freq = f.QuadPart;
    const bool ok = HookExport(L"kernel32.dll", "QueryPerformanceCounter", reinterpret_cast<void*>(&QpcHook), g_qpc) &&
                    HookExport(L"kernel32.dll", "GetTickCount", reinterpret_cast<void*>(&GetTickCountHook), g_tick) &&
                    HookExport(L"kernel32.dll", "GetTickCount64", reinterpret_cast<void*>(&GetTickCount64Hook), g_tick64) &&
                    HookExport(L"winmm.dll", "timeGetTime", reinterpret_cast<void*>(&TimeGetTimeHook), g_timeGetTime);
    Log(L"boot screens: engine clock hooks %s (%.0fx)", ok ? L"installed" : L"NOT all installed; not sped up", g_bootSpeed);
    if (!ok)
        g_bootSpeed = 1.0;
}

void WarpStart()
{
    if (g_bootSpeed <= 1.0 || !g_qpc)
        return;
    AcquireSRWLockExclusive(&g_warpLock);
    if (g_warpPhase == 0) {
        g_warpStart = RealQpc();
        InterlockedExchange(&g_warpPhase, 1);
        Log(L"boot screens: engine clock %.1fx until the main menu", g_bootSpeed);
    }
    ReleaseSRWLockExclusive(&g_warpLock);
}

// Diagnostic for the boot screens: the process's disk reads and CPU time per half second from the .asi's start until
// shortly after the main menu, to tell loading from a fixed-length animation.
ULONGLONG CpuMs()
{
    FILETIME c, e, k, u;
    GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u);
    return ((static_cast<ULONGLONG>(k.dwHighDateTime) << 32 | k.dwLowDateTime) +
            (static_cast<ULONGLONG>(u.dwHighDateTime) << 32 | u.dwLowDateTime)) / 10000ull;
}

DWORD WINAPI StartupSampler(void*)
{
    const ULONGLONG t0 = GetTickCount64();
    IO_COUNTERS io0{};
    GetProcessIoCounters(GetCurrentProcess(), &io0);
    ULONGLONG cpu0 = CpuMs(), last = t0;
    int after = 0;
    for (int i = 0; i < 240 && after < 4; ++i) {
        Sleep(500);
        IO_COUNTERS io{};
        GetProcessIoCounters(GetCurrentProcess(), &io);
        const ULONGLONG now = GetTickCount64(), cpu = CpuMs();
        Log(L"start-up sample t+%5.1f s: read %7.1f MB in %5llu ops; CPU %4.2f cores", (now - t0) / 1000.0,
            (io.ReadTransferCount - io0.ReadTransferCount) / 1048576.0, io.ReadOperationCount - io0.ReadOperationCount,
            static_cast<double>(cpu - cpu0) / static_cast<double>(now - last));
        io0 = io;
        cpu0 = cpu;
        last = now;
        if (g_menuVideoOpened)
            ++after;
    }
    return 0;
}

// The start-up logo videos (2026-10-22 log: 7.5 s each, before the boot animation). Not the main menu's background
// (ui_fullscreen\wd2_mainmenubackground.bik).
bool IsLogoVideo(const char* name)
{
    static const char* const logos[] = {"logo_ubi_dedsec.bik", "s00_nvidia_intro.bik"};
    const char* slash = std::strrchr(name, '\\');
    const char* file = slash ? slash + 1 : name;
    for (const char* l : logos)
        if (_stricmp(file, l) == 0)
            return true;
    return false;
}

// The first kVideoLogLines opens and closes are logged (the start-up and the first loads); the game opens many more
// small videos in play (from memory), which would only fill the log.
constexpr LONG kVideoLogLines = 40;
volatile LONG g_videoLines = 0;

bool VideoLogLine()
{
    const LONG n = InterlockedIncrement(&g_videoLines);
    if (n == kVideoLogLines + 1)
        Log(L"video log: %ld lines written; further videos are not logged", kVideoLogLines);
    return n <= kVideoLogLines;
}

void* WINAPI BinkOpenHook(const char* name, uint32_t flags)
{
    OpenVideo v{nullptr, GetTickCount64(), {}};
    const bool byName = !(flags & (kBinkFileHandle | kBinkFromMemory));
    if (flags & kBinkFileHandle)
        sprintf_s(v.name, "(file handle %p)", static_cast<const void*>(name));
    else if (flags & kBinkFromMemory)
        sprintf_s(v.name, "(from memory at %p)", static_cast<const void*>(name));
    else if (name)
        CopyName(name, v.name, sizeof(v.name));
    if (g_skip && byName && IsLogoVideo(v.name)) {
        Log(L"video skipped: %hs (not opened)", v.name);
        if (std::strstr(v.name, "s00_nvidia_intro"))  // the last logo: the boot screens come next
            WarpStart();
        return nullptr;  // as if the file could not be opened
    }
    void* const bink = g_binkOpen.unsafe_call<void*>(name, flags);
    v.bink = bink;
    if (std::strstr(v.name, "wd2_mainmenubackground")) {
        InterlockedExchange(&g_menuVideoOpened, 1);
        if (g_warpPhase == 1)
            WarpStop(RealQpc(), L"main menu");
    }
    BinkHead h{};
    if (VideoLogLine()) {
        if (bink && ReadHead(bink, h))
            Log(L"video opened: %hs (flags 0x%08x): %ux%u, %u frames at %u/%u fps = %.1f s", v.name, flags, h.width,
                h.height, h.frames, h.frameRate, h.frameRateDiv,
                h.frameRate ? h.frames * static_cast<double>(h.frameRateDiv) / h.frameRate : 0.0);
        else
            Log(L"video opened: %hs (flags 0x%08x): %hs", v.name, flags, bink ? "header unreadable" : "FAILED (BinkOpen returned 0)");
    }
    if (bink) {
        std::lock_guard<std::mutex> lock(g_videoMx);
        g_videos.push_back(v);
    }
    return bink;
}

void WINAPI BinkCloseHook(void* bink)
{
    BinkHead h{};
    const bool head = bink && ReadHead(bink, h);
    {
        std::lock_guard<std::mutex> lock(g_videoMx);
        for (size_t i = 0; i < g_videos.size(); ++i)
            if (g_videos[i].bink == bink) {
                if (VideoLogLine())
                    Log(L"video closed: %hs after %.1f s, at frame %u of %u", g_videos[i].name,
                    (GetTickCount64() - g_videos[i].opened) / 1000.0, head ? h.frameNum : 0, head ? h.frames : 0);
                g_videos.erase(g_videos.begin() + static_cast<ptrdiff_t>(i));
                break;
            }
    }
    g_binkClose.unsafe_call<void>(bink);
}

bool HookBink(HMODULE bink, const char* name, void* detour, SafetyHookInline& hook)
{
    void* const target = reinterpret_cast<void*>(GetProcAddress(bink, name));
    if (!target)
        return false;
    auto created = safetyhook::InlineHook::create(target, detour, safetyhook::InlineHook::StartDisabled);
    if (!created)
        return false;
    hook = std::move(*created);
    return static_cast<bool>(hook.enable());
}

void LogVideos()
{
    HMODULE bink = GetModuleHandleW(L"bink2w64.dll");
    if (!bink)
        bink = LoadLibraryW(L"bink2w64.dll");  // by name: the game's own copy in bin
    if (!bink) {
        Log(L"video log: bink2w64.dll not found (error %lu); videos are not logged", GetLastError());
        return;
    }
    const bool open = HookBink(bink, "BinkOpen", reinterpret_cast<void*>(&BinkOpenHook), g_binkOpen);
    const bool close = HookBink(bink, "BinkClose", reinterpret_cast<void*>(&BinkCloseHook), g_binkClose);
    Log(L"video log: BinkOpen %s, BinkClose %s", open ? L"hooked" : L"NOT hooked", close ? L"hooked" : L"NOT hooked");
}

}  // namespace

void intro_skip::Install()
{
    g_skip = SkipIntroSetting();
    LogVideos();
    if (g_skip)
        InstallClockHooks();
    wchar_t ini[MAX_PATH];
    swprintf_s(ini, L"%sWD2SkyFix.ini", asi::g_dir);
    if (GetPrivateProfileIntW(L"Debug", L"StartupSamples", 0, ini))  // the disk/CPU samples that showed the boot screens wait
        if (const HANDLE t = CreateThread(nullptr, 0, &StartupSampler, nullptr, 0, nullptr))  // runs once DllMain is done
            CloseHandle(t);
}
