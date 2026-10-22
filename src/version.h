#pragma once

// "Watch Dogs 2 - Sky Flicker Fix" by Deepo on Nexus Mods. One version for all its files: WD2SkyFix.asi (the fix
// without ReShade), dinput8.dll (the ASI loader for it) and WD2SkyFix.addon64 (the same fix as a ReShade add-on).
// 1.0.0: the first release, the add-on only. 1.1.0: the .asi and its loader. ReShade shows the add-on's version in its
// add-on list and log; the .asi writes its version to WD2SkyFix.log.
#define FIX_NAME "WD2SkyFix"

#define VERSION_MAJOR 1
#define VERSION_MINOR 1
#define VERSION_PATCH 0

#define STRINGIFY_HELPER(x) #x
#define STRINGIFY(x) STRINGIFY_HELPER(x)
#define VERSION_STRING STRINGIFY(VERSION_MAJOR) "." STRINGIFY(VERSION_MINOR) "." STRINGIFY(VERSION_PATCH)

#define COMPANY_NAME      ""
#define PRODUCT_NAME      "Watch Dogs 2 - Sky Flicker Fix"
#define PRODUCT_VERSION   VERSION_STRING
#define FILE_VERSION      VERSION_STRING
#define LEGAL_COPYRIGHT   "Copyright (c) 2026 Deepo. MIT License. Third-party notices: WD2SkyFix-THIRD-PARTY-NOTICES.txt."
#define LEGAL_TRADEMARKS  ""

// Per file: xmake defines WD2_FILE_ASI or WD2_FILE_LOADER for those two targets (also for version.rc).
#if defined(WD2_FILE_ASI)
#define COMMENTS          "Fix for Watch Dogs 2 (DirectX 11), loaded by an ASI loader: adds the missing barriers to the compute shader that computes the sky's ambient light, which otherwise changes from frame to frame on current GPUs and makes the sky, fog and shadows flicker. Any other shader is left untouched. No ReShade needed."
#define FILE_DESCRIPTION  "Watch Dogs 2 - Sky Flicker Fix"
#define INTERNAL_NAME     FIX_NAME ".asi"
#define ORIGINAL_FILENAME FIX_NAME ".asi"
#elif defined(WD2_FILE_LOADER)
#define COMMENTS          "ASI loader for Watch Dogs 2: loads the .asi files next to it when the game starts, and passes all DirectInput calls on to Windows' own dinput8.dll."
#define FILE_DESCRIPTION  "Watch Dogs 2 - Sky Flicker Fix: ASI loader"
#define INTERNAL_NAME     "dinput8.dll"
#define ORIGINAL_FILENAME "dinput8.dll"
#else
#define COMMENTS          "ReShade add-on for Watch Dogs 2 (DirectX 11): adds the missing barriers to the compute shader that computes the sky's ambient light, which otherwise changes from frame to frame on current GPUs and makes the sky, fog and shadows flicker. Any other shader is left untouched."
#define FILE_DESCRIPTION  "Watch Dogs 2 - Sky Flicker Fix (ReShade add-on)"
#define INTERNAL_NAME     FIX_NAME ".addon64"
#define ORIGINAL_FILENAME FIX_NAME ".addon64"
#endif
