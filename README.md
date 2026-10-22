# WD2SkyFix

Source code of Watch Dogs 2 - Sky Flicker Fix: fixes the game's sky, fog and shadow flicker on current GPUs (a
compute shader with missing barriers) and skips the start-up logos. Download, installation and support:
https://www.nexusmods.com/watchdogs2/mods/387

Building needs Visual Studio 2026 (MSVC) and xmake:

    git clone --recursive https://github.com/Deepo/WD2SkyFix.git
    cd WD2SkyFix
    xmake f -p windows -a x64 -m release -y
    xmake -y

In build\windows\x64\release:

- `WD2SkyFix.asi` (src/skyfix_asi.cpp, src/intro_skip.cpp): the mod since 1.1.0, no ReShade needed. Settings:
  config/WD2SkyFix.ini.
- `dinput8.dll` (src/asiloader.cpp): its ASI loader. Ultimate ASI Loader works too.
- `WD2SkyFix.addon64` (src/skyfix.cpp): the same shader fix as a ReShade add-on, the 1.0.0 release.

The shader rewrite itself is src/dxbc_patch.h, shared by both.

MIT License. Third-party code in external/: safetyhook (BSL-1.0) and Zydis/Zycore (MIT) for the .asi, the ReShade
add-on API headers (BSD-3-Clause OR MIT) for the add-on; see THIRD-PARTY-NOTICES.txt and external/README.md.
