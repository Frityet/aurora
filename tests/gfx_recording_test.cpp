#include <gtest/gtest.h>

#include "gfx/frame_packet.hpp"
#include "gfx/recording.hpp"
#include "gfx/texture.hpp"
#include "gx/gx.hpp"
#include "gx/fifo.hpp"
#include "gx/destruction_state.hpp"
#include <dolphin/gx.h>
#include <dolphin/gx/GXAurora.h>
#include <dolphin/vi.h>
#include "webgpu/gpu.hpp"

#include <algorithm>
#include <memory>

namespace aurora::gfx {
namespace {

constexpr auto ColorFormat = wgpu::TextureFormat::RGBA8Unorm;
constexpr auto DepthFormat = wgpu::TextureFormat::Depth24Plus;

class GfxRecordingTest : public ::testing::Test {
protected:
  void SetUp() override {
    webgpu::g_graphicsConfig.surfaceConfiguration.format = ColorFormat;
    webgpu::g_graphicsConfig.depthFormat = DepthFormat;
    webgpu::g_graphicsConfig.msaaSamples = 1;
    webgpu::g_frameBuffer.size = {640, 480, 1};
    webgpu::g_frameBuffer.format = ColorFormat;
    webgpu::g_depthBuffer.size = {640, 480, 1};
    webgpu::g_depthBuffer.format = DepthFormat;
    detail::testing::suppress_render_worker(true);
    detail::begin_recording(frame, 0);
  }

  void TearDown() override {
    if (recordingActive) {
      if (is_offscreen()) {
        end_offscreen();
      }
      finish();
      detail::end_recording();
    }
    detail::shutdown_recording();
  }

  void seed(uint32_t width, uint32_t height) {
    detail::testing::seed_offscreen_cache(width, height, ColorFormat, DepthFormat);
  }

  void copy_current_offscreen() {
    const auto& pass = frame.renderPasses.back();
    const auto& size = pass.colorAttachments[SceneColorAttachmentIndex].size;
    auto target = std::make_shared<TextureRef>(wgpu::Texture{}, wgpu::TextureView{}, wgpu::TextureView{}, size,
                                               ColorFormat, 1, GX_TF_RGBA8);
    resolve_pass_into(std::move(target), {0, 0, static_cast<int32_t>(size.width), static_cast<int32_t>(size.height)},
                      false, false, false, {}, 1.f);
  }

  size_t count_efb_passes() const {
    return static_cast<size_t>(
        std::ranges::count_if(frame.renderPasses, [](const auto& pass) { return pass.label.starts_with("EFB"); }));
  }

  detail::FramePacket frame;
  bool recordingActive = true;
};

TEST_F(GfxRecordingTest, CreateRestoreReturnsToEfb) {
  seed(320, 180);
  begin_offscreen(320, 180);
  ASSERT_TRUE(is_offscreen());

  end_offscreen();

  EXPECT_FALSE(is_offscreen());
  ASSERT_EQ(frame.renderPasses.size(), 2u);
  EXPECT_TRUE(frame.renderPasses[0].sealed);
  EXPECT_TRUE(frame.renderPasses[0].discardable);
  EXPECT_EQ(count_efb_passes(), 1u);
}

TEST_F(GfxRecordingTest, EfbPassUsesDiscoveredSceneLayout) {
  ASSERT_FALSE(frame.renderPasses.empty());
  const auto discovered = scene_render_target_layout();
  const auto targetLayout = frame.renderPasses.front().target_layout();

  EXPECT_EQ(targetLayout.key, discovered.key);
  EXPECT_EQ(targetLayout.colorAttachmentCount, discovered.colorAttachmentCount);
  EXPECT_EQ(targetLayout.colorAttachments[SceneColorAttachmentIndex].semantic, ColorAttachmentSemantic::SceneColor);
}

TEST_F(GfxRecordingTest, CopiedOffscreenPassIsRetainedOnRestore) {
  seed(320, 180);
  begin_offscreen(320, 180);
  copy_current_offscreen();

  end_offscreen();

  ASSERT_EQ(frame.renderPasses.size(), 3u);
  EXPECT_TRUE(frame.renderPasses[0].sealed);
  EXPECT_FALSE(frame.renderPasses[0].discardable);
  EXPECT_TRUE(frame.renderPasses[0].has_consumer());
  EXPECT_TRUE(frame.renderPasses[1].sealed);
  EXPECT_TRUE(frame.renderPasses[1].discardable);
}

TEST_F(GfxRecordingTest, ReplacementRetainsCopiedPassesAndDiscardsContinuations) {
  seed(320, 180);
  seed(160, 90);
  begin_offscreen(320, 180);
  copy_current_offscreen();
  begin_offscreen(160, 90);
  copy_current_offscreen();

  end_offscreen();

  ASSERT_EQ(frame.renderPasses.size(), 5u);
  EXPECT_TRUE(frame.renderPasses[0].has_consumer());
  EXPECT_FALSE(frame.renderPasses[0].discardable);
  EXPECT_TRUE(frame.renderPasses[1].discardable);
  EXPECT_TRUE(frame.renderPasses[2].has_consumer());
  EXPECT_FALSE(frame.renderPasses[2].discardable);
  EXPECT_TRUE(frame.renderPasses[3].discardable);
  EXPECT_EQ(count_efb_passes(), 1u);
}

TEST_F(GfxRecordingTest, RepeatedUncopiedCreatesDiscardEarlierPasses) {
  seed(320, 180);
  seed(160, 90);
  begin_offscreen(320, 180);
  begin_offscreen(160, 90);
  end_offscreen();

  ASSERT_EQ(frame.renderPasses.size(), 3u);
  EXPECT_TRUE(frame.renderPasses[0].sealed);
  EXPECT_TRUE(frame.renderPasses[0].discardable);
  EXPECT_TRUE(frame.renderPasses[1].sealed);
  EXPECT_TRUE(frame.renderPasses[1].discardable);
  EXPECT_EQ(count_efb_passes(), 1u);
}

TEST_F(GfxRecordingTest, PublicCreatePassRejectsExistingOffscreenPass) {
  seed(320, 180);
  seed(160, 90);
  ASSERT_TRUE(create_pass(320, 180));

  EXPECT_FALSE(create_pass(160, 90));

  ResolvedTargets ignored;
  EXPECT_TRUE(resolve_pass({.color = false, .depth = false}, ignored));
}

TEST_F(GfxRecordingTest, FinalizedPassesAreSealedOrDeliberatelyDiscarded) {
  seed(320, 180);
  seed(160, 90);
  begin_offscreen(320, 180);
  copy_current_offscreen();
  begin_offscreen(160, 90);
  end_offscreen();
  finish();

  ASSERT_FALSE(frame.renderPasses.empty());
  for (const auto& pass : frame.renderPasses) {
    EXPECT_TRUE(pass.sealed);
    if (!pass.has_consumer() && pass.label.starts_with("Offscreen")) {
      EXPECT_TRUE(pass.discardable);
    }
  }
  detail::end_recording();
  recordingActive = false;
}

// GX state exists before and between frames. Draining queued register writes
// must retain it without requiring a render pass or discarding the commands.
TEST(GfxRecordingStateTest, QueuedViewportAndScissorSurviveInactiveDrains) {
  webgpu::g_graphicsConfig.surfaceConfiguration.format = ColorFormat;
  webgpu::g_graphicsConfig.depthFormat = DepthFormat;
  webgpu::g_graphicsConfig.msaaSamples = 1;
  webgpu::g_frameBuffer.size = {640, 480, 1};
  webgpu::g_frameBuffer.format = ColorFormat;
  webgpu::g_depthBuffer.size = {640, 480, 1};
  webgpu::g_depthBuffer.format = DepthFormat;
  detail::testing::suppress_render_worker(true);
  gx::fifo::init();
  GXInit(nullptr, 0);
  // This CPU fixture has no SDL window. Native policy consumes register
  // writes without window mapping; the real runtime fixture covers the
  // fitted-policy window fallback before its first recording.
  gx::g_gxState.viewportPolicy = AURORA_VIEWPORT_NATIVE;
  GXRenderModeObj mode{};
  mode.fbWidth = 640;
  mode.efbHeight = 480;
  VIConfigure(&mode);

  GXSetViewport(12.f, 24.f, 320.f, 200.f, 0.125f, 0.75f);
  GXSetScissor(16, 32, 300, 180);
  AuroraDrainGXCommands();
  EXPECT_FALSE(is_frame_active());
  EXPECT_FLOAT_EQ(gx::g_gxState.logicalViewport.left, 12.f);
  EXPECT_FLOAT_EQ(gx::g_gxState.logicalViewport.width, 320.f);
  EXPECT_EQ(gx::g_gxState.logicalScissor.x, 16);

  auto check_recorded_state = [](const detail::FramePacket& packet, float left, float top, float width,
                                 float height, int32_t scissorX, int32_t scissorY,
                                 int32_t scissorWidth, int32_t scissorHeight) {
    ASSERT_FALSE(packet.renderPasses.empty());
    const auto& commands = packet.renderPasses.front().commands;
    ASSERT_GE(commands.size(), 2u);
    ASSERT_EQ(commands[0].type, detail::CommandType::SetViewport);
    EXPECT_FLOAT_EQ(commands[0].data.setViewport.left, left);
    EXPECT_FLOAT_EQ(commands[0].data.setViewport.top, top);
    EXPECT_FLOAT_EQ(commands[0].data.setViewport.width, width);
    EXPECT_FLOAT_EQ(commands[0].data.setViewport.height, height);
    EXPECT_FLOAT_EQ(commands[0].data.setViewport.znear, 0.125f);
    EXPECT_FLOAT_EQ(commands[0].data.setViewport.zfar, 0.75f);
    ASSERT_EQ(commands[1].type, detail::CommandType::SetScissor);
    EXPECT_EQ(commands[1].data.setScissor.x, scissorX);
    EXPECT_EQ(commands[1].data.setScissor.y, scissorY);
    EXPECT_EQ(commands[1].data.setScissor.width, scissorWidth);
    EXPECT_EQ(commands[1].data.setScissor.height, scissorHeight);
  };

  gx::g_gxState.viewportPolicy = AURORA_VIEWPORT_FIT;
  detail::FramePacket first;
  detail::begin_recording(first, 0);
  check_recorded_state(first, 12.f, 24.f, 320.f, 200.f, 16, 32, 300, 180);
  finish();
  detail::end_recording();

  gx::g_gxState.viewportPolicy = AURORA_VIEWPORT_NATIVE;
  GXSetViewport(20.f, 30.f, 200.f, 100.f, 0.125f, 0.75f);
  GXSetScissor(40, 60, 180, 90);
  AuroraDrainGXCommands();
  EXPECT_FALSE(is_frame_active());
  EXPECT_FLOAT_EQ(gx::g_gxState.logicalViewport.left, 20.f);
  EXPECT_EQ(gx::g_gxState.logicalScissor.x, 40);
  // A new target remaps the retained logical state at the next recording.
  webgpu::g_frameBuffer.size = {1280, 960, 1};
  webgpu::g_depthBuffer.size = {1280, 960, 1};
  gx::g_gxState.viewportPolicy = AURORA_VIEWPORT_FIT;
  detail::FramePacket second;
  detail::begin_recording(second, 1);
  check_recorded_state(second, 40.f, 60.f, 400.f, 200.f, 80, 120, 360, 180);
  finish();
  detail::end_recording();
  gx::fifo::shutdown();
  gx::shutdown_destruction_state();
  detail::shutdown_recording();
}

} // namespace
} // namespace aurora::gfx
