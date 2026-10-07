#pragma once

// WD2SkyFix.addon64: the public file, "Watch Dogs 2 - Sky Flicker Fix" by Deepo on Nexus Mods. 1.0.0 is the first
// release. ReShade shows this version in its add-on list and log.
#define FIX_NAME "WD2SkyFix"

#define VERSION_MAJOR 1
#define VERSION_MINOR 0
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
#define COMMENTS          "ReShade add-on for Watch Dogs 2 (DirectX 11): adds the missing barriers to the compute shader that computes the sky's ambient light, which otherwise changes from frame to frame on current GPUs and makes the sky, fog and shadows flicker. Any other shader is left untouched."
#define FILE_DESCRIPTION  "Watch Dogs 2 - Sky Flicker Fix (ReShade add-on)"
#define INTERNAL_NAME     FIX_NAME ".addon64"
#define ORIGINAL_FILENAME FIX_NAME ".addon64"
