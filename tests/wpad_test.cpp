#include <aurora/wpad.hpp>

#include <gtest/gtest.h>

namespace {

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
