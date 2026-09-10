#include <aurora/wpad.hpp>

#include <gtest/gtest.h>

namespace {

static_assert(WPAD_ERR_NONE == 0 && WPAD_ERR_NO_CONTROLLER == -1 && WPAD_ERR_BUSY == -2 && WPAD_ERR_TRANSFER == -3);

class WpadProbeTest : public testing::Test {
protected:
  void SetUp() override { aurora::wpad_service().clear(); }
  void TearDown() override { aurora::wpad_service() = m_saved; }

private:
  aurora::WpadService m_saved = aurora::wpad_service();
};

TEST_F(WpadProbeTest, DisconnectedChannelsReturnNoControllerAndNotFoundType) {
  for (s32 channel = 0; channel < WPAD_MAX_CONTROLLERS; ++channel) {
    u32 type = WPAD_DEV_CORE;
    EXPECT_EQ(WPADProbe(channel, &type), WPAD_ERR_NO_CONTROLLER);
    EXPECT_EQ(type, WPAD_DEV_NOT_FOUND);
    EXPECT_EQ(WPADProbe(channel, nullptr), WPAD_ERR_NO_CONTROLLER);
  }
}

TEST_F(WpadProbeTest, ConnectedChannelsReturnZeroAndCoreType) {
  for (s32 channel = 0; channel < WPAD_MAX_CONTROLLERS; ++channel) {
    aurora::wpad_service().set_connected(channel, true);
    u32 type = WPAD_DEV_NOT_FOUND;
    EXPECT_EQ(WPADProbe(channel, &type), WPAD_ERR_NONE);
    EXPECT_EQ(type, WPAD_DEV_CORE);
    EXPECT_EQ(WPADProbe(channel, nullptr), WPAD_ERR_NONE);
  }
}

TEST_F(WpadProbeTest, DisconnectAndNativeInputReconnectionUpdateResult) {
  auto& service = aurora::wpad_service();
  service.set_button_mask(WPAD_CHAN0, WPAD_BUTTON_A);
  EXPECT_EQ(WPADProbe(WPAD_CHAN0, nullptr), WPAD_ERR_NONE);

  WPADDisconnect(WPAD_CHAN0);
  u32 type = WPAD_DEV_CORE;
  EXPECT_EQ(WPADProbe(WPAD_CHAN0, &type), WPAD_ERR_NO_CONTROLLER);
  EXPECT_EQ(type, WPAD_DEV_NOT_FOUND);

  service.set_button_mask(WPAD_CHAN0, WPAD_BUTTON_B);
  EXPECT_EQ(WPADProbe(WPAD_CHAN0, &type), WPAD_ERR_NONE);
  EXPECT_EQ(type, WPAD_DEV_CORE);
  EXPECT_TRUE(service.is_button_held(WPAD_CHAN0, WPAD_BUTTON_B));
}

TEST_F(WpadProbeTest, ProbeDoesNotConsumeButtonsPointerOrStick) {
  auto& service = aurora::wpad_service();
  service.begin_frame();
  service.set_button_mask(WPAD_CHAN0, WPAD_BUTTON_A | WPAD_BUTTON_Z);
  service.set_sub_stick(WPAD_CHAN0, 0.75F, -0.5F);
  service.set_pointer(WPAD_CHAN0, 123.0F, 234.0F, true);
  KPADStatus before;
  ASSERT_EQ(KPADRead(WPAD_CHAN0, &before, 1), 1);

  for (int read = 0; read < 3; ++read) {
    u32 type = WPAD_DEV_NOT_FOUND;
    EXPECT_EQ(WPADProbe(WPAD_CHAN0, &type), WPAD_ERR_NONE);
    EXPECT_EQ(type, WPAD_DEV_CORE);
  }

  KPADStatus after;
  ASSERT_EQ(KPADRead(WPAD_CHAN0, &after, 1), 1);
  EXPECT_EQ(after.hold, before.hold);
  EXPECT_EQ(after.trig, before.trig);
  EXPECT_EQ(after.release, before.release);
  EXPECT_FLOAT_EQ(after.pos.x, before.pos.x);
  EXPECT_FLOAT_EQ(after.pos.y, before.pos.y);
  EXPECT_EQ(after.dpd_valid_fg, before.dpd_valid_fg);
  EXPECT_FLOAT_EQ(service.sub_stick(WPAD_CHAN0).x, 0.75F);
  EXPECT_FLOAT_EQ(service.sub_stick(WPAD_CHAN0).y, -0.5F);
  EXPECT_EQ(service.sub_stick_trigger(WPAD_CHAN0), aurora::WpadStickRight | aurora::WpadStickDown);
}

TEST_F(WpadProbeTest, InvalidNativeChannelsCannotAliasConnectedPorts) {
  aurora::wpad_service().set_connected(WPAD_CHAN0, true);
  for (const s32 channel : {-1, WPAD_MAX_CONTROLLERS}) {
    u32 type = WPAD_DEV_CORE;
    EXPECT_EQ(WPADProbe(channel, &type), WPAD_ERR_NO_CONTROLLER);
    EXPECT_EQ(type, WPAD_DEV_NOT_FOUND);
  }
  EXPECT_EQ(WPADProbe(WPAD_CHAN0, nullptr), WPAD_ERR_NONE);
}

TEST(WpadService, SubStickUsesRetailDirectionalThreshold) {
  auto service = aurora::WpadService{};
  service.set_connected(WPAD_CHAN0, true);

  service.begin_frame();
  service.set_sub_stick(WPAD_CHAN0, 0.2F, -0.2F);
  EXPECT_EQ(service.sub_stick_hold(WPAD_CHAN0), aurora::WpadStickNone);
  EXPECT_EQ(service.sub_stick_trigger(WPAD_CHAN0), aurora::WpadStickNone);

  service.set_sub_stick(WPAD_CHAN0, 0.2001F, -0.2001F);
  EXPECT_EQ(service.sub_stick_hold(WPAD_CHAN0), aurora::WpadStickRight | aurora::WpadStickDown);
  EXPECT_EQ(service.sub_stick_trigger(WPAD_CHAN0), aurora::WpadStickRight | aurora::WpadStickDown);
  EXPECT_EQ(service.sub_stick_release(WPAD_CHAN0), aurora::WpadStickNone);
}

TEST(WpadService, SubStickTriggerAndReleaseArePreviousFrameEdges) {
  auto service = aurora::WpadService{};
  service.set_connected(WPAD_CHAN0, true);

  service.begin_frame();
  service.set_sub_stick(WPAD_CHAN0, 0.75F, 0.5F);
  EXPECT_EQ(service.sub_stick_trigger(WPAD_CHAN0), aurora::WpadStickRight | aurora::WpadStickUp);

  service.begin_frame();
  service.set_sub_stick(WPAD_CHAN0, 1.0F, 0.25F);
  EXPECT_EQ(service.sub_stick_hold(WPAD_CHAN0), aurora::WpadStickRight | aurora::WpadStickUp);
  EXPECT_EQ(service.sub_stick_trigger(WPAD_CHAN0), aurora::WpadStickNone);
  EXPECT_EQ(service.sub_stick_release(WPAD_CHAN0), aurora::WpadStickNone);

  service.begin_frame();
  service.set_sub_stick(WPAD_CHAN0, -0.75F, -0.5F);
  EXPECT_EQ(service.sub_stick_hold(WPAD_CHAN0), aurora::WpadStickLeft | aurora::WpadStickDown);
  EXPECT_EQ(service.sub_stick_trigger(WPAD_CHAN0), aurora::WpadStickLeft | aurora::WpadStickDown);
  EXPECT_EQ(service.sub_stick_release(WPAD_CHAN0), aurora::WpadStickRight | aurora::WpadStickUp);

  service.begin_frame();
  service.set_sub_stick(WPAD_CHAN0, 0.0F, 0.0F);
  EXPECT_EQ(service.sub_stick_hold(WPAD_CHAN0), aurora::WpadStickNone);
  EXPECT_EQ(service.sub_stick_trigger(WPAD_CHAN0), aurora::WpadStickNone);
  EXPECT_EQ(service.sub_stick_release(WPAD_CHAN0), aurora::WpadStickLeft | aurora::WpadStickDown);
}

TEST(WpadService, DisconnectionClearsSubStickAndDirectionalEdges) {
  auto service = aurora::WpadService{};
  service.set_connected(WPAD_CHAN0, true);

  service.begin_frame();
  service.set_sub_stick(WPAD_CHAN0, 1.0F, -1.0F);
  service.set_connected(WPAD_CHAN0, false);

  EXPECT_EQ(service.sub_stick(WPAD_CHAN0).x, 0.0F);
  EXPECT_EQ(service.sub_stick(WPAD_CHAN0).y, 0.0F);
  EXPECT_EQ(service.sub_stick_hold(WPAD_CHAN0), aurora::WpadStickNone);
  EXPECT_EQ(service.sub_stick_trigger(WPAD_CHAN0), aurora::WpadStickNone);
  EXPECT_EQ(service.sub_stick_release(WPAD_CHAN0), aurora::WpadStickNone);
}

} // namespace
