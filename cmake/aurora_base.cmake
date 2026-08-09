add_library(aurora_base STATIC
        lib/runtime_state.cpp
        lib/compat.cpp
        lib/audio.cpp
        lib/j_audio_sound_archive.cpp
        lib/j_audio_stream.cpp
        lib/device.cpp
        lib/device.hpp
        lib/input.cpp
        lib/logging.cpp
        lib/rfl/ResourceArchive.cpp
        lib/system_info.cpp
        lib/system_info.hpp
)
add_library(aurora::base ALIAS aurora_base)
set_target_properties(aurora_base PROPERTIES FOLDER "aurora")

target_compile_definitions(aurora_base PUBLIC AURORA TARGET_PC)
target_include_directories(aurora_base PUBLIC include)
target_link_libraries(aurora_base PUBLIC fmt::fmt ${AURORA_SDL3_TARGET} xxhash)
target_link_libraries(aurora_base PRIVATE absl::btree absl::flat_hash_map sqlite3 TracyClient)

if (CMAKE_SYSTEM_NAME STREQUAL Windows)
    target_link_libraries(aurora_base PRIVATE wbemuuid.lib comsuppw.lib ntdll.lib DXGI.lib)
elseif (APPLE)
    target_sources(aurora_base PRIVATE lib/system_info_mac.mm)
endif ()

if (IOS)
    find_library(COREHAPTICS_FRAMEWORK CoreHaptics REQUIRED)
    target_sources(aurora_base PRIVATE lib/device_ios.mm)
    set_source_files_properties(lib/device_ios.mm PROPERTIES COMPILE_FLAGS -fobjc-arc)
    target_link_libraries(aurora_base PUBLIC ${COREHAPTICS_FRAMEWORK})
endif ()
