# external

safetyhook and Zydis are git submodules (`git clone --recursive`, or `git submodule update --init --recursive` in a
clone); Zycore is Zydis' own submodule. The ReShade headers are copied unchanged.

| Library | Upstream | Commit | Licence | Used by |
|---|---|---|---|---|
| reshade/include (headers, copied) | https://github.com/crosire/reshade | tag v6.8.0 (add-on API 20) | BSD-3-Clause OR MIT | WD2SkyFix.addon64 |
| safetyhook (submodule) | https://github.com/cursey/safetyhook | f44cc070a8340f2f26649553c49533475417304d (v0.7.0-7) | BSL-1.0 | WD2SkyFix.asi |
| zydis (submodule) | https://github.com/zyantific/zydis | a95bb71013436547f689fc0b380f48edf663066e (v4.0.0-121) | MIT | WD2SkyFix.asi (via safetyhook) |
| zycore (zydis' submodule) | https://github.com/zyantific/zycore-c | 75a36c45ae1ad382b0f4e0ede0af84c11ee69928 (v1.5.2-7) | MIT | WD2SkyFix.asi (via zydis) |
