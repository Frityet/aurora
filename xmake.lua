set_project("aurora")
set_xmakever("3.0.0")
set_languages("c11", "c++20")

add_rules("mode.debug", "mode.release")
add_repositories("pc-port-local $(projectdir)/..")

option("aurora_enable_gx")
    set_default(true)
    set_showmenu(true)
    set_description("Enable GX implementation and WebGPU renderer")
option_end()

option("aurora_enable_dvd")
    set_default(true)
    set_showmenu(true)
    set_description("Enable DVD implementation backed by nod")
option_end()

option("aurora_enable_card")
    set_default(true)
    set_showmenu(true)
    set_description("Enable CARD implementation")
option_end()

option("aurora_enable_rmlui")
    set_default(false)
    set_showmenu(true)
    set_description("Enable RmlUi integration")
option_end()

option("aurora_cache_use_zstd")
    set_default(true)
    set_showmenu(true)
    set_description("Compress WebGPU cache entries with zstd")
option_end()

option("aurora_dawn_version")
    set_default("v20260618.032059")
    set_showmenu(true)
    set_description("Dawn version tag")
option_end()

option("aurora_sdl3_version")
    set_default("3.4.10")
    set_showmenu(true)
    set_description("SDL3 version tag")
option_end()

option("aurora_nod_version")
    set_default("v2.0.0-alpha.10")
    set_showmenu(true)
    set_description("nod version tag")
option_end()

option("aurora_dawn_provider")
    set_default("auto")
    set_showmenu(true)
    set_values("auto", "vendor", "system", "package")
    set_description("How to provide Dawn: auto, vendor, system, or package")
option_end()

option("aurora_dawn_linkage")
    set_default("static")
    set_showmenu(true)
    set_values("shared", "static")
    set_description("Dawn linkage type preference")
option_end()

option("aurora_sdl3_provider")
    set_default("auto")
    set_showmenu(true)
    set_values("auto", "vendor", "system", "package")
    set_description("How to provide SDL3: auto, vendor, system, or package")
option_end()

option("aurora_sdl3_linkage")
    set_default("static")
    set_showmenu(true)
    set_values("shared", "static")
    set_description("SDL3 linkage type preference")
option_end()

option("aurora_nod_provider")
    set_default("auto")
    set_showmenu(true)
    set_values("auto", "vendor", "system", "package")
    set_description("How to provide nod: auto, vendor, system, or package")
option_end()

option("aurora_nod_linkage")
    set_default("static")
    set_showmenu(true)
    set_values("shared", "static")
    set_description("nod linkage type preference")
option_end()

add_requireconfs("dawn-build", {configs = {cxxflags = "-std=c++20"}})

local function provider(name)
    return get_config(name) or "auto"
end

local function shared_linkage(name)
    return (get_config(name) or "static") == "shared"
end

local sdl3_require = "libsdl3 " .. (get_config("aurora_sdl3_version") or "3.4.10")
if provider("aurora_sdl3_provider") == "system" then
    sdl3_require = "libsdl3"
end

add_requires("fmt 11.1.4")
add_requires("abseil 20240722.0", {configs = {cxx_standard = "20"}})
add_requires("xxhash v0.8.3")
add_requires("sqlite3 3.53.0+0")
add_requires("tracy v0.11.1", {configs = {cmake = false, tracy_enable = false}})
add_requires(sdl3_require, {
    system = provider("aurora_sdl3_provider") == "system",
    configs = {shared = shared_linkage("aurora_sdl3_linkage"), x11 = is_plat("linux"), x11_shared = true, wayland = false},
})

if has_config("aurora_enable_gx") then
    if provider("aurora_dawn_provider") == "system" then
        add_requires("dawn-build", {system = true})
    else
        add_requires("dawn-build " .. (get_config("aurora_dawn_version") or "v20260618.032059"),
                     {configs = {shared = shared_linkage("aurora_dawn_linkage")}})
    end
    add_requires("libpng v1.6.58")
    add_requires("zlib")
    local imgui_user_config = path.join(os.scriptdir(), "include", "aurora", "imgui_config.h")
    local imgui_user_config_source = path.join(os.scriptdir(), "lib", "imgui_config.cpp")
    add_requires("imgui v1.91.9b-docking",
                 {configs = {sdl3 = true, sdl3_renderer = true, wgpu = true, wgpu_backend = "dawn",
                             dawn_version = get_config("aurora_dawn_version") or "v20260618.032059",
                             freetype = true, user_config = imgui_user_config,
                             user_config_source = imgui_user_config_source}})
    if has_config("aurora_cache_use_zstd") then
        add_requires("zstd")
    end
end

if has_config("aurora_enable_dvd") then
    if provider("aurora_nod_provider") == "system" then
        add_requires("encounter-nod", {system = true})
    else
        add_requires("encounter-nod " .. (get_config("aurora_nod_version") or "v2.0.0-alpha.10"),
                     {configs = {shared = shared_linkage("aurora_nod_linkage")}})
    end
end

if has_config("aurora_enable_rmlui") then
    add_requires("rmlui")
end

local function add_aurora_common_settings(public_target)
    add_includedirs("include", {public = true})
    add_defines("AURORA", "TARGET_PC", {public = public_target ~= false})
    if is_plat("linux") then
        add_syslinks("pthread", "dl", "rt")
    end
end

local function add_dawn_backend_defines()
    add_defines("WEBGPU_DAWN", "DAWN_ENABLE_BACKEND_NULL", {public = true})
    if is_plat("windows", "mingw", "msys") then
        add_defines("DAWN_ENABLE_BACKEND_D3D11", "DAWN_ENABLE_BACKEND_D3D12",
                    "DAWN_ENABLE_BACKEND_VULKAN", {public = true})
    elseif is_plat("macosx", "iphoneos") then
        add_defines("DAWN_ENABLE_BACKEND_METAL", {public = true})
    elseif is_plat("android") then
        add_defines("DAWN_ENABLE_BACKEND_VULKAN", "DAWN_ENABLE_BACKEND_OPENGL",
                    "DAWN_ENABLE_BACKEND_OPENGLES", {public = true})
    else
        add_defines("DAWN_ENABLE_BACKEND_VULKAN", "DAWN_ENABLE_BACKEND_OPENGL",
                    "DAWN_ENABLE_BACKEND_DESKTOP_GL", "DAWN_ENABLE_BACKEND_OPENGLES",
                    {public = true})
    end
end

target("aurora-nw4r")
    set_kind("static")
    add_includedirs("include", {public = true})
    add_files("lib/nw4r/brlan.cpp")
    add_headerfiles("include/(aurora/nw4r/**.hpp)")

target("aurora-base")
    set_kind("static")
    add_aurora_common_settings(true)
    add_files("lib/runtime_state.cpp", "lib/compat.cpp", "lib/audio.cpp", "lib/j_audio_sound_archive.cpp",
              "lib/j_audio_stream.cpp", "lib/device.cpp", "lib/input.cpp", "lib/logging.cpp",
              "lib/system_info.cpp", "lib/rfl/ResourceArchive.cpp")
    add_headerfiles("include/(aurora/audio.hpp)", "include/(aurora/j_audio_sound_archive.hpp)",
                    "include/(aurora/j_audio_stream.hpp)", "include/(aurora/rfl/ResourceArchive.hpp)")
    add_packages("fmt", "libsdl3", "xxhash", {public = true})
    add_packages("abseil", "sqlite3", "tracy")
    if is_plat("macosx", "iphoneos") then
        add_files("lib/system_info_mac.mm")
        add_frameworks("Foundation")
    end

target("aurora-platform")
    set_kind("static")
    add_aurora_common_settings(true)
    add_files("lib/window.cpp")
    add_deps("aurora-base")
    add_packages("fmt", "libsdl3", "xxhash", {public = true})
    add_packages("abseil", "sqlite3", "tracy")
    if has_config("aurora_enable_gx") then
        add_files("lib/imgui.cpp", "lib/webgpu/gpu.cpp", "lib/webgpu/gpu_cache.cpp",
                  "lib/webgpu/gpu_prof.cpp", "lib/dawn/BackendBinding.cpp")
        if is_plat("windows") then
            add_files("lib/dawn/TracyPlatform.cpp")
        else
            add_files("lib/dawn/TracyPlatform.cpp", {cxflags = "-fno-rtti"})
        end
        if is_plat("macosx", "iphoneos") then
            add_files("lib/dawn/MetalBinding.mm", {cxflags = "-fobjc-arc"})
            add_frameworks("Metal")
        end
        add_packages("dawn-build")
        add_packages("imgui", {public = true})
        add_defines("AURORA_ENABLE_GX", "IMGUI_USER_CONFIG=\"aurora/imgui_config.h\"", {public = true})
        add_dawn_backend_defines()
        if has_config("aurora_cache_use_zstd") then
            add_defines("AURORA_CACHE_USE_ZSTD")
            add_packages("zstd")
        end
    end

target("aurora-core")
    set_kind("static")
    add_aurora_common_settings(true)
    add_files("lib/aurora.cpp")
    add_headerfiles("include/(aurora/**.h)", "include/(revolution.h)", "include/(revolution/**.h)",
                    "include/(RVLFaceLib.h)", "lib/*.hpp")
    add_deps("aurora-base", "aurora-platform")
    add_packages("fmt", "libsdl3", "xxhash", {public = true})
    add_packages("abseil", "sqlite3", "tracy")
    if has_config("aurora_enable_gx") then
        add_deps("aurora-gx")
        add_defines("AURORA_ENABLE_GX", "IMGUI_USER_CONFIG=\"aurora/imgui_config.h\"", {public = true})
        add_dawn_backend_defines()
    end
    if has_config("aurora_enable_rmlui") then
        add_defines("AURORA_ENABLE_RMLUI", {public = true})
        add_files("lib/rmlui.cpp", "lib/rmlui/RuntimeTextureProvider.cpp",
                  "lib/rmlui/RmlUi_Backend_Aurora.cpp", "lib/rmlui/WebGPURenderInterface.cpp",
                  "lib/rmlui/SystemInterface_Aurora.cpp", "lib/rmlui/FileInterface_SDL.cpp",
                  "lib/rmlui/GlassFilter.cpp")
        add_packages("rmlui", {public = true})
    end

target("aurora-os")
    set_kind("static")
    add_aurora_common_settings(true)
    add_files("lib/dolphin/os/OSInit.cpp", "lib/dolphin/os/OSMemory.cpp",
              "lib/dolphin/os/OSBootInfo.cpp", "lib/dolphin/os/OSTime.cpp",
              "lib/dolphin/os/OSCache.cpp",
              "lib/dolphin/os/OSArena.cpp", "lib/dolphin/os/OSAlloc.cpp",
              "lib/dolphin/os/OSAddress.cpp", "lib/dolphin/os/OSReport.cpp",
              "lib/dolphin/AR.cpp", "lib/nand.cpp")
    add_headerfiles("include/(aurora/nand.hpp)")
    add_deps("aurora-base")

target("aurora-si")
    set_kind("static")
    add_aurora_common_settings(true)
    add_files("lib/dolphin/si/si.cpp")
    add_deps("aurora-base")
    add_packages("abseil")

target("aurora-pad")
    set_kind("static")
    add_aurora_common_settings(true)
    add_files("lib/dolphin/pad/pad.cpp", "lib/wpad.cpp")
    add_headerfiles("include/(aurora/wpad.hpp)")
    add_deps("aurora-base", "aurora-si")
    add_packages("abseil")

target("aurora-ms")
    set_kind("static")
    add_aurora_common_settings(true)
    add_files("lib/dolphin/ms/mouse.cpp")
    add_deps("aurora-base")
    add_packages("abseil")

target("aurora-main")
    set_kind("static")
    add_includedirs("include", {public = true})
    add_files("lib/main.cpp")
    add_packages("libsdl3", {public = true})

target("aurora-mtx")
    set_kind("static")
    add_includedirs("include", {public = true})
    add_files("lib/dolphin/mtx/mtx.c", "lib/dolphin/mtx/mtxstack.c",
              "lib/dolphin/mtx/mtxvec.c", "lib/dolphin/mtx/mtx44.c",
              "lib/dolphin/mtx/vec.c", "lib/dolphin/mtx/quat.c")

target("aurora-vi")
    set_kind("static")
    add_aurora_common_settings(true)
    add_files("lib/dolphin/vi/vi.cpp")
    add_deps("aurora-platform")

if has_config("aurora_enable_gx") then
    target("aurora-gx")
        set_kind("static")
        add_aurora_common_settings(true)
        add_dawn_backend_defines()
        add_files("lib/gfx/clear.cpp", "lib/gfx/common.cpp", "lib/gfx/depth_peek.cpp",
                  "lib/gfx/pipeline_cache.cpp", "lib/gfx/render_worker.cpp", "lib/gfx/dds_io.cpp",
                  "lib/gfx/tex_copy_conv.cpp", "lib/gfx/tex_palette_conv.cpp", "lib/gfx/texture.cpp",
                  "lib/gfx/texture_format.cpp",
                  "lib/gfx/texture_convert.cpp", "lib/gfx/texture_replacement.cpp",
                  "lib/gfx/png_io.cpp",
                  "lib/gx/attr_fmt.cpp", "lib/gx/command_processor.cpp", "lib/gx/destruction_state.cpp",
                  "lib/gx/dl.cpp", "lib/gx/fifo.cpp", "lib/gx/gx.cpp", "lib/gx/texture.cpp",
                  "lib/gx/pipeline.cpp", "lib/gx/shader.cpp", "lib/gx/shader_info.cpp",
                  "lib/rfl/CharacterModel.cpp", "lib/rfl/CharacterResource.cpp",
                  "lib/dolphin/gx/GXBump.cpp", "lib/dolphin/gx/GXCull.cpp",
                  "lib/dolphin/gx/GXCpu2Efb.cpp", "lib/dolphin/gx/GXDispList.cpp",
                  "lib/dolphin/gx/GXDraw.cpp", "lib/dolphin/gx/GXExtra.cpp",
                  "lib/dolphin/gx/GXFifo.cpp", "lib/dolphin/gx/GXFrameBuffer.cpp",
                  "lib/dolphin/gx/GXGeometry.cpp", "lib/dolphin/gx/GXGet.cpp",
                  "lib/dolphin/gx/GXLighting.cpp", "lib/dolphin/gx/GXManage.cpp",
                  "lib/dolphin/gx/GXPerf.cpp", "lib/dolphin/gx/GXPixel.cpp",
                  "lib/dolphin/gx/GXTev.cpp", "lib/dolphin/gx/GXTexture.cpp",
                  "lib/dolphin/gx/GXTransform.cpp", "lib/dolphin/gx/GXVert.cpp",
                  "lib/dolphin/gx/GXAurora.cpp")
        add_headerfiles("include/(aurora/rfl/CharacterModel.hpp)")
        if has_config("aurora_enable_rmlui") then
            add_files("lib/rmlui/pipeline.cpp")
            add_defines("AURORA_ENABLE_RMLUI")
            add_packages("rmlui")
        end
        add_deps("aurora-platform", "aurora-vi")
        add_packages("dawn-build", {public = true})
        add_packages("xxhash", "abseil", "sqlite3", "tracy", "libpng", "zlib")

    target("aurora-gd")
        set_kind("static")
        add_aurora_common_settings(true)
        add_files("lib/dolphin/gd/GDBase.cpp", "lib/dolphin/gd/GDGeometry.cpp",
                  "lib/dolphin/gd/GDIndirect.cpp", "lib/dolphin/gd/GDLight.cpp",
                  "lib/dolphin/gd/GDPixel.cpp", "lib/dolphin/gd/GDTev.cpp",
                  "lib/dolphin/gd/GDTexture.cpp", "lib/dolphin/gd/GDTransform.cpp",
                  "lib/dolphin/gd/GDAurora.cpp")
        add_deps("aurora-gx")
end

if has_config("aurora_enable_card") then
    target("aurora-card")
        set_kind("static")
        add_aurora_common_settings(true)
        add_files("lib/card/BlockAllocationTable.cpp", "lib/card/CardRawFile.cpp",
                  "lib/card/CardGciFolder.cpp", "lib/card/Directory.cpp",
                  "lib/card/DolphinCardPath.cpp", "lib/card/File.cpp",
                  "lib/card/FileIO.cpp", "lib/card/SRAM.cpp", "lib/card/Util.cpp",
                  "lib/dolphin/card.cpp")
        add_deps("aurora-base")
end

if has_config("aurora_enable_dvd") then
    target("aurora-dvd")
        set_kind("static")
        add_aurora_common_settings(true)
        add_files("lib/dolphin/dvd/dvd.cpp", "lib/dolphin/dvd/fst.cpp")
        add_deps("aurora-base")
        add_packages("encounter-nod", "libsdl3", {public = true})
        add_packages("fmt", "tracy")
end
