local name = "WD2SkyFix"

set_project(name)
add_rules("mode.debug", "mode.release")
set_languages("cxxlatest", "clatest")

-- ===================== WD2SkyFix.addon64 =====================
-- "Watch Dogs 2 - Sky Flicker Fix" by Deepo on Nexus Mods. A ReShade add-on (needs ReShade with add-on support):
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
