// Shared by the parts of WD2SkyFix.asi: the sky fix (skyfix_asi.cpp) and the start-up skip (intro_skip.cpp).
#pragma once
#include <windows.h>

namespace asi {

extern wchar_t g_dir[MAX_PATH];  // the .asi's folder, with a trailing backslash

// One line in WD2SkyFix.log, with time and process id.
void Log(const wchar_t* fmt, ...);

}  // namespace asi

namespace intro_skip {

// Reads [Startup] SkipIntro from WD2SkyFix.ini (default on) and, if on, installs the start-up skip.
void Install();

}  // namespace intro_skip
