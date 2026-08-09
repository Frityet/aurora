#include "gx/destruction_state.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <latch>

namespace {
class GXDestructionStateTest : public ::testing::Test {
protected:
  void SetUp() override { aurora::gx::shutdown_destruction_state(); }
  void TearDown() override { aurora::gx::shutdown_destruction_state(); }
};

TEST_F(GXDestructionStateTest, CommandsAreEnabledOnlyDuringRendererLifetime) {
  auto calls = 0;
  aurora::gx::with_destruction_commands_enabled([&] { ++calls; });
  EXPECT_EQ(calls, 0);

  aurora::gx::initialize_destruction_state();
  EXPECT_TRUE(aurora::gx::destruction_commands_enabled());
  aurora::gx::with_destruction_commands_enabled([&] { ++calls; });
  EXPECT_EQ(calls, 1);

  aurora::gx::shutdown_destruction_state();
  EXPECT_FALSE(aurora::gx::destruction_commands_enabled());
  aurora::gx::with_destruction_commands_enabled([&] { ++calls; });
  EXPECT_EQ(calls, 1);
}

TEST_F(GXDestructionStateTest, ShutdownWaitsForCommandEmissionAndRejectsLaterCommands) {
  using namespace std::chrono_literals;

  aurora::gx::initialize_destruction_state();
  std::latch commandEntered{1};
  std::latch releaseCommand{1};
  auto command = std::async(std::launch::async, [&] {
    aurora::gx::with_destruction_commands_enabled([&] {
      commandEntered.count_down();
      releaseCommand.wait();
    });
  });
  commandEntered.wait();

  auto shutdown = std::async(std::launch::async, [] { aurora::gx::shutdown_destruction_state(); });
  EXPECT_EQ(shutdown.wait_for(20ms), std::future_status::timeout);

  releaseCommand.count_down();
  EXPECT_EQ(command.wait_for(1s), std::future_status::ready);
  EXPECT_EQ(shutdown.wait_for(1s), std::future_status::ready);

  auto lateCalls = 0;
  aurora::gx::with_destruction_commands_enabled([&] { ++lateCalls; });
  EXPECT_EQ(lateCalls, 0);
}
} // namespace
