#include <dolphin/os.h>
#include <aurora/allocation.hpp>
#include <aurora/guest_thread.hpp>
#include <aurora/process.hpp>
#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <future>
#include <optional>
#include <string>

namespace {
using namespace std::chrono_literals;
int firstCalls = 0;
int secondCalls = 0;
bool sawGuest = false;
bool sawCallbackGuest = false;
OSContext* callbackContext = nullptr;
OSThread* callbackThread = nullptr;

void first() { ++firstCalls; }
void second() { ++secondCalls; }
void rearm_power() {
  ++firstCalls;
  EXPECT_EQ(OSSetPowerCallback(second), nullptr);
}
void rearm_reset() {
  ++firstCalls;
  EXPECT_EQ(OSSetResetCallback(second), nullptr);
}
void inspect_interrupt() {
  ++firstCalls;
  sawGuest = aurora::allocation::routing_state.guest;
  sawCallbackGuest = aurora::allocation::routing_state.callbackGuest;
  callbackContext = OSGetCurrentContext();
  callbackThread = OSGetCurrentThread();
  EXPECT_EQ(OSDisableInterrupts(), FALSE);
  EXPECT_EQ(OSDisableScheduler(), 1);
  EXPECT_EQ(OSEnableScheduler(), 2);
}

class OSResetTest : public testing::Test {
  void SetUp() override {
    OSSetPowerCallback(nullptr);
    OSSetResetCallback(nullptr);
    aurora::os::set_reset_button_pressed(false);
    (void)OSGetResetButtonState();
    firstCalls = secondCalls = 0;
    sawGuest = sawCallbackGuest = false;
    callbackContext = nullptr;
    callbackThread = nullptr;
  }
  void TearDown() override {
    OSSetPowerCallback(nullptr);
    OSSetResetCallback(nullptr);
    aurora::os::set_reset_button_pressed(false);
    (void)OSGetResetButtonState();
  }
};

TEST_F(OSResetTest, ResetPressIsLatchedUntilReadRatherThanUntilRelease) {
  EXPECT_EQ(OSGetResetButtonState(), FALSE);
  aurora::os::set_reset_button_pressed(true);
  EXPECT_EQ(OSGetResetButtonState(), TRUE);
  EXPECT_EQ(OSGetResetButtonState(), FALSE);
  aurora::os::set_reset_button_pressed(true);
  EXPECT_EQ(OSGetResetButtonState(), FALSE);
  aurora::os::set_reset_button_pressed(false);
  aurora::os::set_reset_button_pressed(true);
  aurora::os::set_reset_button_pressed(false);
  EXPECT_EQ(OSGetResetButtonState(), TRUE);
  EXPECT_EQ(OSGetResetButtonState(), FALSE);
  for (int i = 0; i < 3; ++i) {
    aurora::os::set_reset_button_pressed(true);
    aurora::os::set_reset_button_pressed(false);
  }
  EXPECT_EQ(OSGetResetButtonState(), TRUE);
  EXPECT_EQ(OSGetResetButtonState(), FALSE);
}

TEST_F(OSResetTest, ReplacingPowerHandlerReturnsPreviousAndDefaultIsNull) {
  EXPECT_EQ(OSSetPowerCallback(first), nullptr);
  EXPECT_EQ(OSSetPowerCallback(second), first);
  EXPECT_EQ(OSSetPowerCallback(nullptr), second);
  EXPECT_EQ(OSSetPowerCallback(nullptr), nullptr);
  aurora::os::notify_power_button_pressed();
  EXPECT_EQ(firstCalls, 0);
  EXPECT_EQ(secondCalls, 0);
  EXPECT_EQ(OSGetResetButtonState(), FALSE);
  OSSetPowerCallback(first);
  aurora::os::notify_power_button_pressed();
  aurora::os::notify_power_button_pressed();
  EXPECT_EQ(firstCalls, 1);
  EXPECT_EQ(OSSetPowerCallback(nullptr), nullptr);
}

TEST_F(OSResetTest, OneShotCallbacksAreClearedBeforeReentrantRegistration) {
  OSSetPowerCallback(rearm_power);
  aurora::os::notify_power_button_pressed();
  aurora::os::notify_power_button_pressed();
  aurora::os::notify_power_button_pressed();
  EXPECT_EQ(firstCalls, 1);
  EXPECT_EQ(secondCalls, 1);
  EXPECT_EQ(OSSetResetCallback(first), nullptr);
  EXPECT_EQ(OSSetResetCallback(rearm_reset), first);
  aurora::os::set_reset_button_pressed(true);
  aurora::os::set_reset_button_pressed(false);
  aurora::os::set_reset_button_pressed(true);
  aurora::os::set_reset_button_pressed(false);
  aurora::os::set_reset_button_pressed(true);
  EXPECT_EQ(firstCalls, 2);
  EXPECT_EQ(secondCalls, 2);
}

TEST_F(OSResetTest, DeliveryUsesInterruptContextAndCapturedAllocationRouting) {
  auto* caller = OSGetCurrentThread();
  auto* context = OSGetCurrentContext();
  const auto routing = aurora::allocation::routing_state;
  aurora::allocation::routing_state = {false, true};
  OSSetPowerCallback(inspect_interrupt);
  aurora::allocation::routing_state = routing;
  aurora::os::notify_power_button_pressed();
  EXPECT_EQ(firstCalls, 1);
  EXPECT_TRUE(sawGuest);
  EXPECT_TRUE(sawCallbackGuest);
  EXPECT_EQ(callbackThread, caller);
  EXPECT_NE(callbackContext, context);
  EXPECT_EQ(OSGetCurrentContext(), context);
  EXPECT_EQ(aurora::allocation::routing_state.guest, routing.guest);
  EXPECT_EQ(aurora::allocation::routing_state.callbackGuest, routing.callbackGuest);
  EXPECT_EQ(OSDisableInterrupts(), TRUE);
  OSRestoreInterrupts(TRUE);
  EXPECT_EQ(OSDisableScheduler(), 0);
  EXPECT_EQ(OSEnableScheduler(), 1);

  aurora::allocation::routing_state = {true, false};
  OSSetPowerCallback(inspect_interrupt);
  aurora::allocation::routing_state = routing;
  aurora::os::notify_power_button_pressed();
  EXPECT_FALSE(sawGuest);
  EXPECT_FALSE(sawCallbackGuest);
}

TEST_F(OSResetTest, RegistrationAndLatchReadsPreserveDisabledInterruptState) {
  const BOOL enabled = OSDisableInterrupts();
  OSSetPowerCallback(first);
  OSSetResetCallback(second);
  EXPECT_EQ(OSGetResetButtonState(), FALSE);
  EXPECT_EQ(OSDisableInterrupts(), FALSE);
  OSRestoreInterrupts(enabled);
}

TEST_F(OSResetTest, NativeDeliveryWaitsForGuestCpuAndRestoresWorkerRouting) {
  std::future<bool> completed;
  std::promise<void> entering;
  auto entered = entering.get_future();
  {
    const aurora::os::GuestThreadExecutionScope guest;
    OSSetPowerCallback(first);
    completed = std::async(std::launch::async, [&] {
      const auto routing = aurora::allocation::routing_state;
      entering.set_value();
      aurora::os::notify_power_button_pressed();
      return routing.guest == aurora::allocation::routing_state.guest &&
             routing.callbackGuest == aurora::allocation::routing_state.callbackGuest;
    });
    entered.get();
    EXPECT_EQ(completed.wait_for(20ms), std::future_status::timeout);
    EXPECT_EQ(firstCalls, 0);
  }
  EXPECT_TRUE(completed.get());
  EXPECT_EQ(firstCalls, 1);
}

TEST_F(OSResetTest, TerminalCallbackRequestRestoresContextAndIsNotDeliveredAgain) {
  auto* context = OSGetCurrentContext();
  const auto routing = aurora::allocation::routing_state;
  OSSetPowerCallback([] { OSRestart(0x13579); });
  try {
    aurora::os::notify_power_button_pressed();
    FAIL() << "terminal SDK request returned";
  } catch (const aurora::os::ProcessRequest& request) {
    EXPECT_EQ(request.destination, aurora::os::ProcessDestination::Restart);
    EXPECT_EQ(request.resetCode, 0x13579U);
  }
  EXPECT_EQ(OSGetCurrentContext(), context);
  EXPECT_EQ(OSSetPowerCallback(nullptr), nullptr);
  EXPECT_EQ(OSGetResetButtonState(), FALSE);
  EXPECT_EQ(aurora::allocation::routing_state.guest, routing.guest);
  EXPECT_EQ(aurora::allocation::routing_state.callbackGuest, routing.callbackGuest);
  EXPECT_EQ(OSDisableInterrupts(), TRUE);
  OSRestoreInterrupts(TRUE);
  EXPECT_EQ(OSDisableScheduler(), 0);
  EXPECT_EQ(OSEnableScheduler(), 1);
}

TEST_F(OSResetTest, TerminalCallsOnlyDescribeTheRequestedGuestTransition) {
  using Destination = aurora::os::ProcessDestination;
  for (const auto operation : {Destination::Reboot, Destination::Shutdown, Destination::Menu}) {
    try {
      if (operation == Destination::Reboot) OSRebootSystem();
      else if (operation == Destination::Shutdown) OSShutdownSystem();
      else OSReturnToMenu();
      FAIL() << "terminal SDK call returned";
    } catch (const aurora::os::ProcessRequest& request) {
      EXPECT_EQ(request.destination, operation);
      EXPECT_EQ(request.resetCode, 0U);
    }
  }
  EXPECT_EQ(OSGetResetButtonState(), FALSE);
}

TEST_F(OSResetTest, InheritedResetCodeDistinguishesRestartFromColdLaunch) {
  const char* existing = std::getenv("AURORA_PROCESS_RESET_CODE");
  struct RestoreEnvironment {
    std::optional<std::string> value;
    ~RestoreEnvironment() {
      if (value) setenv("AURORA_PROCESS_RESET_CODE", value->c_str(), 1);
      else unsetenv("AURORA_PROCESS_RESET_CODE");
    }
  } restore{existing ? std::optional<std::string>(existing) : std::nullopt};
  unsetenv("AURORA_PROCESS_RESET_CODE");
  EXPECT_EQ(OSGetResetCode(), 0U);
  for (const char* invalid : {"", "-1", "+1", " 1", "1x", "4294967296"}) {
    setenv("AURORA_PROCESS_RESET_CODE", invalid, 1);
    EXPECT_EQ(OSGetResetCode(), 0U);
  }
  setenv("AURORA_PROCESS_RESET_CODE", "0", 1);
  EXPECT_EQ(OSGetResetCode(), 0x80000000U);
  setenv("AURORA_PROCESS_RESET_CODE", "12345", 1);
  EXPECT_EQ(OSGetResetCode(), 0x80003039U);
  setenv("AURORA_PROCESS_RESET_CODE", "4294967295", 1);
  EXPECT_EQ(OSGetResetCode(), 0xFFFFFFFFU);
}
} // namespace
