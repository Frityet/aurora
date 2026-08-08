add_library(aurora_core STATIC lib/aurora.cpp)
add_library(aurora::core ALIAS aurora_core)
set_target_properties(aurora_core PROPERTIES FOLDER "aurora")

target_compile_definitions(aurora_core PUBLIC AURORA TARGET_PC)
target_include_directories(aurora_core PUBLIC include)
target_link_libraries(aurora_core PUBLIC aurora::base aurora::platform)
target_link_libraries(aurora_core PRIVATE absl::flat_hash_map TracyClient)

if (AURORA_ENABLE_GX)
    target_compile_definitions(aurora_core PUBLIC AURORA_ENABLE_GX WEBGPU_DAWN)
    target_link_libraries(aurora_core PUBLIC aurora::gx)
endif ()

if (AURORA_ENABLE_RMLUI)
    target_compile_definitions(aurora_core PUBLIC AURORA_ENABLE_RMLUI)
    target_sources(aurora_core PRIVATE
            lib/rmlui.cpp
            lib/rmlui/RuntimeTextureProvider.cpp
            lib/rmlui/RmlUi_Backend_Aurora.cpp
            lib/rmlui/WebGPURenderInterface.cpp
            lib/rmlui/SystemInterface_Aurora.cpp
            lib/rmlui/FileInterface_SDL.cpp
    )
    target_link_libraries(aurora_core PUBLIC rmlui rmlui_backends)
endif ()
