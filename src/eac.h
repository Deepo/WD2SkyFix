// Easy Anti-Cheat check for WD2SkyFix.asi and its loader: they change nothing in the game when it finds EAC. The fix
// is for the game with EAC off (launch option -eac_launcher), like every Watch Dogs 2 mod.
#pragma once
#include <windows.h>
#include <tlhelp32.h>

// What was found (EAC's client DLL in this process, or one of its processes running), or nullptr.
inline const wchar_t* EacFound()
{
    if (GetModuleHandleW(L"EasyAntiCheat_x64.dll"))
        return L"EasyAntiCheat_x64.dll in the game process";
    static const wchar_t* const names[] = {L"EAC.exe", L"EasyAntiCheat.exe", L"EasyAntiCheat_EOS.exe", L"EasyAntiCheat_Setup.exe"};
    const HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return nullptr;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    const wchar_t* hit = nullptr;
    for (BOOL ok = Process32FirstW(snap, &pe); ok && !hit; ok = Process32NextW(snap, &pe))
        for (const wchar_t* n : names)
            if (_wcsicmp(pe.szExeFile, n) == 0)
                hit = n;
    CloseHandle(snap);
    return hit;
}
