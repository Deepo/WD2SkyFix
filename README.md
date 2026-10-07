# WD2SkyFix

Source code of Watch Dogs 2 - Sky Flicker Fix, a ReShade add-on. Download, installation and support:
https://www.nexusmods.com/watchdogs2/mods/387

Building needs Visual Studio 2026 (MSVC) and xmake:

    git clone https://github.com/Deepo/WD2SkyFix.git
    cd WD2SkyFix
    xmake f -p windows -a x64 -m release -y
    xmake -y

The add-on: build\windows\x64\release\WD2SkyFix.addon64

MIT License. Uses the ReShade add-on API headers (BSD-3-Clause OR MIT) in external/reshade:
see THIRD-PARTY-NOTICES.txt.
