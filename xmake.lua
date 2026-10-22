local name = "WD2SkyFix"

set_project(name)
add_rules("mode.debug", "mode.release")
set_languages("cxxlatest", "clatest")

-- The same MSVC settings for every file we ship.
local function msvc_flags()
    if is_plat("windows") then
        set_toolchains("msvc")
        add_cxflags("/utf-8", "/W4")
        if is_mode("release") then
            set_optimize("fastest")
            set_strip("all")
            set_runtimes("MT")
            add_ldflags("/OPT:REF", "/OPT:ICF")
        else
            set_strip("none")
            set_runtimes("MTd")
            add_cxflags("/Zi")
            add_defines("_DEBUG")
        end
    end
end

-- xmake does not see version.rc's #include of version.h, so a version bump alone would leave a file's version stale:
-- drop the compiled resource whenever version.h is newer.
local function recompile_version_rc(target)
    local obj = target:objectfile("src/version.rc")
    if os.isfile(obj) and os.mtime("src/version.h") > os.mtime(obj) then
        print("version.h changed: recompiling version.rc for " .. target:name())
        os.rm(obj)
    end
end

-- ===================== WD2SkyFix.addon64 =====================
-- "Watch Dogs 2 - Sky Flicker Fix" by Deepo on Nexus Mods, as a ReShade add-on (needs ReShade with add-on support):
-- when the game creates the sky-light compute shader (DXBC checksum e90c6810...), it replaces the shader's summing
-- steps with a groupshared reduction with barriers (src/dxbc_patch.h). Nothing else is touched.
-- external/reshade: the ReShade add-on API headers (BSD-3-Clause OR MIT), copied unchanged.

target(name)
    set_kind("shared")
    set_prefixname("")
    set_extension(".addon64")

    add_includedirs("src", "external/reshade/include")
    add_headerfiles("src/*.h")
    add_files("src/skyfix.cpp", "src/version.rc")
    before_build(recompile_version_rc)
    msvc_flags()

-- ===================== WD2SkyFix.asi + its loader, dinput8.dll =====================
-- The same fix without ReShade (src/skyfix_asi.cpp): hooks D3D11 device creation and the devices' CreateComputeShader.
-- Loaded from the game's bin folder by our ASI loader (dinput8.dll, src/asiloader.cpp) or Ultimate ASI Loader.
-- external/safetyhook (BSL-1.0) and external/zydis with zycore (MIT) make the inline hooks: git submodules at
-- the commits in external/README.md (git clone --recursive).

target("zycore")
    set_kind("static")
    add_defines("ZYCORE_STATIC_BUILD", {public = true})
    add_includedirs("external/zydis/dependencies/zycore/include", {public = true})
    add_files("external/zydis/dependencies/zycore/src/*.c")
    set_runtimes(is_mode("release") and "MT" or "MTd")

target("zydis")
    add_deps("zycore")
    set_kind("static")
    add_defines("ZYDIS_STATIC_BUILD", {public = true})
    add_includedirs("external/zydis/include", "external/zydis/src", {public = true})
    add_files("external/zydis/src/*.c")
    set_runtimes(is_mode("release") and "MT" or "MTd")

target(name .. "Asi")
    set_kind("shared")
    set_basename(name)
    set_prefixname("")
    set_extension(".asi")
    add_defines("WD2_FILE_ASI")

    add_includedirs("src", "external/safetyhook/include")
    add_headerfiles("src/*.h")
    add_files("src/skyfix_asi.cpp", "src/intro_skip.cpp", "src/version.rc", "external/safetyhook/src/**.cpp")
    add_deps("zydis")
    before_build(recompile_version_rc)
    msvc_flags()

target("WD2AsiLoader")
    set_kind("shared")
    set_basename("dinput8")
    add_defines("WD2_FILE_LOADER")

    add_includedirs("src")
    add_files("src/asiloader.cpp", "src/asiloader.def", "src/version.rc")
    before_build(recompile_version_rc)
    msvc_flags()
