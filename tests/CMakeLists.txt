include(FetchContent)
include(GoogleTest)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/AuroraCopyRuntimeDLLs.cmake)

set(CMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE PRE_TEST)

add_executable(functional_test functional_test.cpp)
target_include_directories(functional_test PRIVATE ../include)
add_test(NAME functional COMMAND functional_test)

if (NOT TARGET gtest)
  FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
    URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
    EXCLUDE_FROM_ALL
  )
  FetchContent_MakeAvailable(googletest)
endif ()

add_executable(brlan_tests brlan_test.cpp)
target_link_libraries(brlan_tests PRIVATE aurora::nw4r gtest gtest_main)
gtest_discover_tests(brlan_tests)

add_executable(audio_tests audio_test.cpp)
target_link_libraries(audio_tests PRIVATE aurora::base gtest gtest_main)
gtest_discover_tests(audio_tests)

add_executable(wpad_tests
  wpad_test.cpp
  wpad_test_stubs.cpp
  ../lib/wpad.cpp
)
target_include_directories(wpad_tests PRIVATE ../include)
target_compile_definitions(wpad_tests PRIVATE AURORA TARGET_PC)
target_link_libraries(wpad_tests PRIVATE gtest gtest_main)
gtest_discover_tests(wpad_tests)

add_executable(io_tests
  io_test.cpp
  ../lib/card/FileIO.cpp
  ../lib/io.cpp
)
target_include_directories(io_tests PRIVATE ../lib)
target_link_libraries(io_tests PRIVATE gtest gtest_main ${AURORA_SDL3_TARGET})
aurora_copy_runtime_dlls(io_tests)
gtest_discover_tests(io_tests)

add_executable(time_tests
  time_test.cpp
  ../lib/time.cpp
)
target_include_directories(time_tests PRIVATE
  ../include
  ../lib
)
target_link_libraries(time_tests PRIVATE gtest gtest_main)
gtest_discover_tests(time_tests)

add_executable(os_time_tests
  os_time_test.cpp
  os_time_test_stubs.cpp
  ../lib/time.cpp
  ../lib/dolphin/os/OSTime.cpp
)
target_include_directories(os_time_tests PRIVATE
  ../include
  ../lib
)
target_compile_definitions(os_time_tests PRIVATE AURORA TARGET_PC)
target_link_libraries(os_time_tests PRIVATE fmt::fmt gtest gtest_main)
gtest_discover_tests(os_time_tests)

find_package(Threads REQUIRED)
add_executable(os_execution_tests
  os_execution_test.cpp
  os_mutex_test.cpp
  ../lib/dolphin/os/OSExecution.cpp
  ../lib/dolphin/os/OSMutex.cpp
)
target_include_directories(os_execution_tests PRIVATE ../include)
target_compile_definitions(os_execution_tests PRIVATE AURORA TARGET_PC)
target_link_libraries(os_execution_tests PRIVATE gtest gtest_main Threads::Threads)
gtest_discover_tests(os_execution_tests)

add_executable(os_report_tests
  os_report_test.cpp
  ../lib/dolphin/os/OSReport.cpp
)
target_include_directories(os_report_tests PRIVATE ../include)
target_compile_definitions(os_report_tests PRIVATE AURORA TARGET_PC)
target_link_libraries(os_report_tests PRIVATE gtest gtest_main)
gtest_discover_tests(os_report_tests)

if (AURORA_ENABLE_GX)
  add_executable(gx_texture_region_tests
    gx_texture_region_test.cpp
    ../lib/dolphin/gx/GXTextureRegion.cpp
  )
  target_include_directories(gx_texture_region_tests PRIVATE ../include)
  target_compile_definitions(gx_texture_region_tests PRIVATE AURORA TARGET_PC)
  target_link_libraries(gx_texture_region_tests PRIVATE gtest gtest_main)
  gtest_discover_tests(gx_texture_region_tests)

  add_executable(gx_destruction_state_tests
    gx_destruction_state_test.cpp
    ../lib/gx/destruction_state.cpp
  )
  target_include_directories(gx_destruction_state_tests PRIVATE ../lib)
  target_link_libraries(gx_destruction_state_tests PRIVATE gtest gtest_main)
  gtest_discover_tests(gx_destruction_state_tests)

  add_executable(gx_z_scale_offset_render_test
    gx_z_scale_offset_render_test.cpp
    ../lib/compat.cpp
  )
  target_link_libraries(gx_z_scale_offset_render_test PRIVATE aurora::core)

  set(AURORA_GX_RENDER_TEST_RUNTIME_DIR "${CMAKE_CURRENT_BINARY_DIR}/gx_z_scale_offset_runtime")
  set(AURORA_GX_RENDER_TEST_CONFIG_DIR "${CMAKE_CURRENT_BINARY_DIR}/gx_z_scale_offset_config")
  set(AURORA_GX_RENDER_TEST_CACHE_DIR "${CMAKE_CURRENT_BINARY_DIR}/gx_z_scale_offset_cache")
  file(MAKE_DIRECTORY
    "${AURORA_GX_RENDER_TEST_RUNTIME_DIR}"
    "${AURORA_GX_RENDER_TEST_CONFIG_DIR}"
    "${AURORA_GX_RENDER_TEST_CACHE_DIR}"
  )
  file(CHMOD "${AURORA_GX_RENDER_TEST_RUNTIME_DIR}"
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
  )

  find_program(AURORA_XVFB_RUN_EXECUTABLE xvfb-run)
  if (AURORA_XVFB_RUN_EXECUTABLE)
    add_test(NAME gx_z_scale_offset_render
      COMMAND "${CMAKE_COMMAND}" -E env
        "XDG_RUNTIME_DIR=${AURORA_GX_RENDER_TEST_RUNTIME_DIR}"
        "XDG_CONFIG_HOME=${AURORA_GX_RENDER_TEST_CONFIG_DIR}"
        "XDG_CACHE_HOME=${AURORA_GX_RENDER_TEST_CACHE_DIR}"
        "${AURORA_XVFB_RUN_EXECUTABLE}" -a $<TARGET_FILE:gx_z_scale_offset_render_test>
    )
  else ()
    message(WARNING "xvfb-run was not found; gx_z_scale_offset_render will run directly and requires a usable display")
    add_test(NAME gx_z_scale_offset_render
      COMMAND "${CMAKE_COMMAND}" -E env
        "XDG_RUNTIME_DIR=${AURORA_GX_RENDER_TEST_RUNTIME_DIR}"
        "XDG_CONFIG_HOME=${AURORA_GX_RENDER_TEST_CONFIG_DIR}"
        "XDG_CACHE_HOME=${AURORA_GX_RENDER_TEST_CACHE_DIR}"
        $<TARGET_FILE:gx_z_scale_offset_render_test>
    )
  endif ()
  set_tests_properties(gx_z_scale_offset_render PROPERTIES LABELS gpu RUN_SERIAL TRUE)

  add_executable(gx_depth_snapshot_render_test
    gx_depth_snapshot_render_test.cpp
    ../lib/compat.cpp
  )
  target_link_libraries(gx_depth_snapshot_render_test PRIVATE aurora::core)

  set(AURORA_GX_DEPTH_SNAPSHOT_RUNTIME_DIR "${CMAKE_CURRENT_BINARY_DIR}/gx_depth_snapshot_runtime")
  set(AURORA_GX_DEPTH_SNAPSHOT_CONFIG_DIR "${CMAKE_CURRENT_BINARY_DIR}/gx_depth_snapshot_config")
  set(AURORA_GX_DEPTH_SNAPSHOT_CACHE_DIR "${CMAKE_CURRENT_BINARY_DIR}/gx_depth_snapshot_cache")
  file(MAKE_DIRECTORY
    "${AURORA_GX_DEPTH_SNAPSHOT_RUNTIME_DIR}"
    "${AURORA_GX_DEPTH_SNAPSHOT_CONFIG_DIR}"
    "${AURORA_GX_DEPTH_SNAPSHOT_CACHE_DIR}"
  )
  file(CHMOD "${AURORA_GX_DEPTH_SNAPSHOT_RUNTIME_DIR}"
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
  )

  if (AURORA_XVFB_RUN_EXECUTABLE)
    add_test(NAME gx_depth_snapshot_render
      COMMAND "${CMAKE_COMMAND}" -E env
        "XDG_RUNTIME_DIR=${AURORA_GX_DEPTH_SNAPSHOT_RUNTIME_DIR}"
        "XDG_CONFIG_HOME=${AURORA_GX_DEPTH_SNAPSHOT_CONFIG_DIR}"
        "XDG_CACHE_HOME=${AURORA_GX_DEPTH_SNAPSHOT_CACHE_DIR}"
        "${AURORA_XVFB_RUN_EXECUTABLE}" -a $<TARGET_FILE:gx_depth_snapshot_render_test>
    )
  else ()
    add_test(NAME gx_depth_snapshot_render
      COMMAND "${CMAKE_COMMAND}" -E env
        "XDG_RUNTIME_DIR=${AURORA_GX_DEPTH_SNAPSHOT_RUNTIME_DIR}"
        "XDG_CONFIG_HOME=${AURORA_GX_DEPTH_SNAPSHOT_CONFIG_DIR}"
        "XDG_CACHE_HOME=${AURORA_GX_DEPTH_SNAPSHOT_CACHE_DIR}"
        $<TARGET_FILE:gx_depth_snapshot_render_test>
    )
  endif ()
  set_tests_properties(gx_depth_snapshot_render PROPERTIES LABELS gpu RUN_SERIAL TRUE)

  add_executable(gx_copy_filter_render_test
    gx_copy_filter_render_test.cpp
    ../lib/compat.cpp
  )
  target_link_libraries(gx_copy_filter_render_test PRIVATE aurora::core)

  set(AURORA_GX_COPY_FILTER_RUNTIME_DIR "${CMAKE_CURRENT_BINARY_DIR}/gx_copy_filter_runtime")
  set(AURORA_GX_COPY_FILTER_CONFIG_DIR "${CMAKE_CURRENT_BINARY_DIR}/gx_copy_filter_config")
  set(AURORA_GX_COPY_FILTER_CACHE_DIR "${CMAKE_CURRENT_BINARY_DIR}/gx_copy_filter_cache")
  file(MAKE_DIRECTORY
    "${AURORA_GX_COPY_FILTER_RUNTIME_DIR}"
    "${AURORA_GX_COPY_FILTER_CONFIG_DIR}"
    "${AURORA_GX_COPY_FILTER_CACHE_DIR}"
  )
  file(CHMOD "${AURORA_GX_COPY_FILTER_RUNTIME_DIR}"
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
  )

  if (AURORA_XVFB_RUN_EXECUTABLE)
    add_test(NAME gx_copy_filter_render
      COMMAND "${CMAKE_COMMAND}" -E env
        "XDG_RUNTIME_DIR=${AURORA_GX_COPY_FILTER_RUNTIME_DIR}"
        "XDG_CONFIG_HOME=${AURORA_GX_COPY_FILTER_CONFIG_DIR}"
        "XDG_CACHE_HOME=${AURORA_GX_COPY_FILTER_CACHE_DIR}"
        "${AURORA_XVFB_RUN_EXECUTABLE}" -a $<TARGET_FILE:gx_copy_filter_render_test>
    )
  else ()
    add_test(NAME gx_copy_filter_render
      COMMAND "${CMAKE_COMMAND}" -E env
        "XDG_RUNTIME_DIR=${AURORA_GX_COPY_FILTER_RUNTIME_DIR}"
        "XDG_CONFIG_HOME=${AURORA_GX_COPY_FILTER_CONFIG_DIR}"
        "XDG_CACHE_HOME=${AURORA_GX_COPY_FILTER_CACHE_DIR}"
        $<TARGET_FILE:gx_copy_filter_render_test>
    )
  endif ()
  set_tests_properties(gx_copy_filter_render PROPERTIES LABELS gpu RUN_SERIAL TRUE)

  add_executable(rfl_character_resource_tests
    rfl_character_resource_test.cpp
    ../lib/rfl/CharacterResource.cpp
  )
  target_include_directories(rfl_character_resource_tests PRIVATE ../include ../lib)
  target_compile_definitions(rfl_character_resource_tests PRIVATE AURORA TARGET_PC)
  target_link_libraries(rfl_character_resource_tests PRIVATE gtest gtest_main)
  gtest_discover_tests(rfl_character_resource_tests)

  # GX FIFO round-trip tests
  # Compile GX sources directly to avoid pulling in the full renderer/WebGPU runtime
  add_executable(gx_fifo_tests
    gx_fifo_test.cpp
    gd_array_test.cpp
    gd_texture_test.cpp
    gx_z_scale_offset_test.cpp
    gx_test_stubs.cpp
    # GX API implementations (encoders)
    ../lib/dolphin/gx/GXBump.cpp
    ../lib/dolphin/gx/GXCull.cpp
    ../lib/dolphin/gx/GXCpu2Efb.cpp
    ../lib/dolphin/gx/GXDispList.cpp
    ../lib/dolphin/gx/GXDraw.cpp
    ../lib/dolphin/gx/GXExtra.cpp
    ../lib/dolphin/gx/GXFifo.cpp
    ../lib/dolphin/gx/GXFrameBuffer.cpp
    ../lib/dolphin/gx/GXGeometry.cpp
    ../lib/dolphin/gx/GXGet.cpp
    ../lib/dolphin/gx/GXLighting.cpp
    ../lib/dolphin/gx/GXManage.cpp
    ../lib/dolphin/gx/GXAurora.cpp
    ../lib/dolphin/gx/GXPerf.cpp
    ../lib/dolphin/gx/GXPixel.cpp
    ../lib/dolphin/gx/GXTev.cpp
    ../lib/dolphin/gx/GXTexture.cpp
    ../lib/dolphin/gx/GXTransform.cpp
    ../lib/dolphin/gx/GXVert.cpp
    # Original GD display-list writers and the real physical-address mapping
    ../lib/dolphin/gd/GDBase.cpp
    ../lib/dolphin/gd/GDGeometry.cpp
    ../lib/dolphin/gd/GDTexture.cpp
    ../lib/dolphin/os/OSAddress.cpp
    ../lib/dolphin/os/OSAlloc.cpp
    ../lib/dolphin/os/OSCache.cpp
    # FIFO/command processor (encoder + decoder)
    ../lib/gx/fifo.cpp
    ../lib/gx/destruction_state.cpp
    ../lib/gx/command_processor.cpp
    ../lib/gx/regs.cpp
    ../lib/gfx/texture_convert.cpp
    ../lib/thread.cpp
    # Display list reader/optimizer
    ../lib/gx/attr_fmt.cpp
    ../lib/gx/dl.cpp
    gx_dl_test.cpp
  )

  target_include_directories(gx_fifo_tests PRIVATE
    ../include
    ../lib
    ../lib/dolphin/gx
  )

  target_compile_definitions(gx_fifo_tests PRIVATE AURORA TARGET_PC)

  target_link_libraries(gx_fifo_tests PRIVATE
    gtest
    gtest_main
    fmt::fmt
    xxhash
    absl::flat_hash_map
    absl::btree
    dawn::dawncpp_headers
    TracyClient
    ${AURORA_SDL3_TARGET}
  )

  gtest_discover_tests(gx_fifo_tests)

  add_executable(gx_texture_cache_tests
    gx_texture_cache_test.cpp
    gx_texture_cache_test_stubs.cpp
    ../lib/gx/texture.cpp
  )
  target_include_directories(gx_texture_cache_tests PRIVATE
    ../include
    ../lib
  )
  target_compile_definitions(gx_texture_cache_tests PRIVATE AURORA TARGET_PC)
  target_link_libraries(gx_texture_cache_tests PRIVATE
    gtest
    gtest_main
    fmt::fmt
    xxhash
    absl::flat_hash_map
    dawn::webgpu_dawn
    TracyClient
    ${AURORA_SDL3_TARGET}
  )
  aurora_copy_runtime_dlls(gx_texture_cache_tests)
  gtest_discover_tests(gx_texture_cache_tests)

  add_executable(texture_replacement_streaming_tests
    texture_replacement_streaming_test.cpp
  )
  target_include_directories(texture_replacement_streaming_tests PRIVATE
    ../include
    ../lib
  )
  target_compile_definitions(texture_replacement_streaming_tests PRIVATE AURORA TARGET_PC)
  target_link_libraries(texture_replacement_streaming_tests PRIVATE
    aurora::gx
    aurora::vi
    gtest
    gtest_main
  )
  aurora_copy_runtime_dlls(texture_replacement_streaming_tests)
  gtest_discover_tests(texture_replacement_streaming_tests)

  add_executable(render_worker_tests
    render_worker_test.cpp
    thread_test.cpp
    ../lib/gfx/render_worker.cpp
    ../lib/thread.cpp
  )
  target_include_directories(render_worker_tests PRIVATE
    ../include
    ../lib
  )
  target_compile_definitions(render_worker_tests PRIVATE AURORA TARGET_PC)
  target_link_libraries(render_worker_tests PRIVATE
    gtest
    gtest_main
    TracyClient
    ${AURORA_SDL3_TARGET}
  )
  gtest_discover_tests(render_worker_tests)

  add_executable(gfx_recording_tests
    gfx_recording_test.cpp
    render_target_layout_test.cpp
  )
  target_include_directories(gfx_recording_tests PRIVATE
    ../include
    ../lib
  )
  target_compile_definitions(gfx_recording_tests PRIVATE AURORA TARGET_PC)
  target_link_libraries(gfx_recording_tests PRIVATE
    aurora::gx
    aurora::vi
    gtest
    gtest_main
  )
  aurora_copy_runtime_dlls(gfx_recording_tests)
  gtest_discover_tests(gfx_recording_tests)
endif () # AURORA_ENABLE_GX

# DVD API tests
if (TARGET aurora::dvd)
  add_executable(dvd_tests
    test_dvd.cpp
    dvd_test_stubs.cpp
    ../lib/logging.cpp
  )
  target_link_libraries(dvd_tests PRIVATE aurora::dvd gtest gtest_main)
  aurora_copy_runtime_dlls(dvd_tests)

  # Pass disc image path as a compile definition if provided
  if (DEFINED DVD_TEST_IMAGE AND EXISTS "${DVD_TEST_IMAGE}")
    target_compile_definitions(dvd_tests PRIVATE DVD_TEST_IMAGE="${DVD_TEST_IMAGE}")
  endif ()

  gtest_discover_tests(dvd_tests)
endif ()

add_executable(os_alloc_tests
  os_alloc_test.cpp
  os_test_globals.cpp
  ../lib/dolphin/os/OSAlloc.cpp
  ../lib/dolphin/os/OSMemory.cpp
  ../lib/dolphin/os/OSAddress.cpp
  ../lib/logging.cpp
)
target_include_directories(os_alloc_tests PRIVATE
  ../include
  ../lib
)
target_compile_definitions(os_alloc_tests PRIVATE AURORA TARGET_PC)
target_link_libraries(os_alloc_tests PRIVATE gtest gtest_main fmt::fmt ${AURORA_SDL3_TARGET})
gtest_discover_tests(os_alloc_tests)

add_executable(mtx44_tests
  mtx44_test.cpp
)
target_link_libraries(mtx44_tests PRIVATE aurora::mtx)
add_test(NAME mtx44 COMMAND mtx44_tests)
