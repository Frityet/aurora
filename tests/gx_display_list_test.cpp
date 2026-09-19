#include "gx_test_common.hpp"
#include "internal.hpp"
#include "dolphin/os/internal.hpp"
#include <aurora/guest_thread.hpp>
#include <dolphin/os.h>

#include <array>
#include <atomic>
#include <chrono>
#include <future>
#include <thread>

namespace {
namespace fifo = aurora::gx::fifo;
using namespace std::chrono_literals;
std::atomic<u32> sTokenCommand{0};
std::atomic<u32> sDoneCommand{0};
std::atomic<u32> sTokens{0};
std::atomic<u32> sDones{0};
std::atomic<bool> sPaused{false};
std::atomic<bool> sRelease{false};

void token_callback(u16 token) {
  EXPECT_EQ(token, 7);
  sTokenCommand.store(aurora::gx::g_gxState.bpRegCache[0x40], std::memory_order_relaxed);
  sTokens.fetch_add(1, std::memory_order_release);
}
void done_callback() {
  sDoneCommand.store(aurora::gx::g_gxState.bpRegCache[0x40], std::memory_order_relaxed);
  sDones.fetch_add(1, std::memory_order_release);
}
void paused_token_callback(u16 token) {
  token_callback(token);
  sPaused.store(true, std::memory_order_release);
  while (!sRelease.load(std::memory_order_acquire)) std::this_thread::yield();
}
template <typename Predicate> bool until(Predicate predicate) {
  const auto deadline = std::chrono::steady_clock::now() + 2s;
  while (!predicate()) {
    if (std::chrono::steady_clock::now() > deadline) return false;
    std::this_thread::yield();
  }
  return true;
}
void be32(std::vector<u8>& bytes, u32 value) {
  for (int shift = 24; shift >= 0; shift -= 8) bytes.push_back(value >> shift);
}
void bp(std::vector<u8>& bytes, u32 value) {
  bytes.push_back(GX_LOAD_BP_REG);
  be32(bytes, value);
}
void raw_call(std::vector<u8>& bytes, u32 address, u32 size) {
  bytes.push_back(GX_CMD_CALL_DL);
  be32(bytes, address);
  be32(bytes, size);
}
void native_call(std::vector<u8>& bytes, const void* data, u32 size) {
  bytes.push_back(GX_AURORA);
  bytes.push_back(GX_AURORA_CALL_DISPLAY_LIST >> 8);
  bytes.push_back(GX_AURORA_CALL_DISPLAY_LIST & 255);
  const u64 address = reinterpret_cast<uintptr_t>(data);
  be32(bytes, address >> 32);
  be32(bytes, address);
  be32(bytes, size);
}
class GXDisplayListTest : public GXFifoTest {
protected:
  void SetUp() override {
    GXFifoTest::SetUp();
    // Initialize dirty GX state separately so cursor assertions measure only
    // the display-list call command, not its required preceding state flush.
    decode_fifo(flush_and_capture());
    sTokenCommand = sDoneCommand = sTokens = sDones = 0;
    sPaused = sRelease = false;
    GXSetDrawSyncCallback(nullptr);
    GXSetDrawDoneCallback(nullptr);
    GXSetBreakPtCallback(nullptr);
    savedMem1Size = aurora::g_config.mem1Size;
    aurora::g_config.mem1Size = physical.size();
    MEM1Start = physical.data();
    MEM1End = physical.data() + physical.size();
    OSBaseAddress = reinterpret_cast<uintptr_t>(MEM1Start);
  }
  void TearDown() override {
    sRelease.store(true, std::memory_order_release);
    GXDisableBreakPt();
    GXSetDrawSyncCallback(nullptr);
    GXSetDrawDoneCallback(nullptr);
    GXFifoTest::TearDown();
    MEM1Start = MEM1End = nullptr;
    OSBaseAddress = 0;
    aurora::g_config.mem1Size = savedMem1Size;
  }
  alignas(32) std::array<u8, 1024> physical{};
  u32 savedMem1Size{};
};
}

TEST_F(GXDisplayListTest, NativeCallBorrowsUntilFetchAndOccupiesFifteenBytes) {
  std::vector<u8> list;
  bp(list, 0x40000011);
  const auto before = fifo::cursor_snapshot();
  GXCallDisplayList(list.data(), list.size());
  EXPECT_EQ(fifo::cursor_snapshot().written - before.written, 15u);
  EXPECT_EQ(fifo::cursor_snapshot().consumed, before.consumed);
  list.back() = 0x17; // GP has not fetched the source yet.
  fifo::drain();
  EXPECT_EQ(aurora::gx::g_gxState.bpRegCache[0x40], 0x40000017u);
  EXPECT_EQ(fifo::cursor_snapshot().completed - before.completed, 15u);
}

TEST_F(GXDisplayListTest, LargeListDoesNotOccupyTheSmallPhysicalRing) {
  alignas(32) std::array<u8, 128> ring{};
  GXFifoObj object;
  GXInitFifoBase(&object, ring.data(), ring.size());
  GXInitFifoLimits(&object, 64, 16);
  GXSetCPUFifo(&object);
  GXSetGPFifo(&object);
  std::vector<u8> list(77920, GX_NOP);
  bp(list, 0x40000017);
  {
    const aurora::os::GuestInterruptExecutionScope interrupt;
    GXCallDisplayList(list.data(), list.size());
    EXPECT_EQ(fifo::cursor_snapshot().written, 15u);
  }
  fifo::drain();
  EXPECT_EQ(fifo::cursor_snapshot().consumed, 15u);
  EXPECT_EQ(aurora::gx::g_gxState.bpRegCache[0x40], 0x40000017u);
}

TEST_F(GXDisplayListTest, PhysicalCallUsesNineBytesAndChecksTheWholeSourceExtent) {
  const std::array<u8, 5> list{GX_LOAD_BP_REG, 0x40, 0, 0, 0x17};
  std::memcpy(physical.data() + physical.size() - 32, list.data(), list.size());
  std::vector<u8> call;
  for (const u32 alias : {0u, 0x80000000u, 0xc0000000u}) {
    const auto before = fifo::cursor_snapshot();
    call.clear();
    raw_call(call, alias | (physical.size() - 32 + 7), 63);
    fifo::write_data(call.data(), call.size());
    fifo::drain();
    EXPECT_EQ(fifo::cursor_snapshot().consumed - before.consumed, 9u);
    EXPECT_EQ(aurora::gx::g_gxState.bpRegCache[0x40], 0x40000017u);
  }
  call.clear();
  raw_call(call, physical.size() - 32, 64);
  EXPECT_DEATH(fifo::process(call.data(), call.size()), "physical MEM1 extent");
}

TEST_F(GXDisplayListTest, EmptyNativeListIsACompletedCallAndInvalidNativeSpansAreRejected) {
  GXCallDisplayList(nullptr, 0);
  fifo::drain();
  EXPECT_EQ(fifo::cursor_snapshot().completed, 15u);
  std::vector<u8> call;
  native_call(call, nullptr, 1);
  EXPECT_DEATH(fifo::process(call.data(), call.size()), "invalid readable span");
  call.clear();
  native_call(call, reinterpret_cast<void*>(UINTPTR_MAX), 32);
  EXPECT_DEATH(fifo::process(call.data(), call.size()), "invalid readable span");
}

TEST_F(GXDisplayListTest, NestedNativeAndPhysicalCallsAreSkippedWithoutResolvingThem) {
  std::vector<u8> inner;
  bp(inner, 0x48000007);
  std::vector<u8> outer;
  bp(outer, 0x40000011);
  native_call(outer, inner.data(), inner.size());
  raw_call(outer, 0xffffffff, 0xffffffff); // A skipped nested address is never fetched.
  bp(outer, 0x40000017);
  GXSetDrawSyncCallback(token_callback);
  GXCallDisplayList(outer.data(), outer.size());
  fifo::drain();
  EXPECT_EQ(sTokens.load(), 0u);
  EXPECT_EQ(aurora::gx::g_gxState.bpRegCache[0x40], 0x40000017u);
  EXPECT_EQ(fifo::cursor_snapshot().consumed, 15u);
}

TEST_F(GXDisplayListTest, TokensAndDrawDoneResumeBeforeFollowingListAndOuterCommands) {
  std::vector<u8> list;
  bp(list, 0x40000011);
  bp(list, 0x48000007);
  bp(list, 0x40000012);
  bp(list, 0x45000002);
  bp(list, 0x40000013);
  GXSetDrawSyncCallback(token_callback);
  GXSetDrawDoneCallback(done_callback);
  GXCallDisplayList(list.data(), list.size());
  fifo::write_u8(GX_LOAD_BP_REG);
  fifo::write_u32(0x40000017);
  fifo::drain();
  EXPECT_EQ(sTokens.load(), 1u);
  EXPECT_EQ(sDones.load(), 1u);
  EXPECT_EQ(sTokenCommand.load(), 0x40000011u);
  EXPECT_EQ(sDoneCommand.load(), 0x40000012u);
  EXPECT_EQ(aurora::gx::g_gxState.bpRegCache[0x40], 0x40000017u);
  EXPECT_EQ(fifo::cursor_snapshot().completed, 20u);
}

TEST_F(GXDisplayListTest, DrainWaitsForListCallbackAndRetainsTheFetchedSnapshot) {
  std::vector<u8> list;
  bp(list, 0x48000007);
  bp(list, 0x40000017);
  GXSetDrawSyncCallback(paused_token_callback);
  fifo::begin_frame();
  GXCallDisplayList(list.data(), list.size());
  ASSERT_TRUE(until([] { return sPaused.load(std::memory_order_acquire); }));
  EXPECT_EQ(fifo::cursor_snapshot().consumed, 15u);
  EXPECT_EQ(fifo::cursor_snapshot().completed, 0u);
  list.back() = 0x11; // The complete list was already fetched into owned storage.
  auto drain = std::async(std::launch::async, [] { fifo::drain(); });
  EXPECT_EQ(drain.wait_for(30ms), std::future_status::timeout);
  sRelease.store(true, std::memory_order_release);
  ASSERT_EQ(drain.wait_for(2s), std::future_status::ready);
  drain.get();
  EXPECT_EQ(aurora::gx::g_gxState.bpRegCache[0x40], 0x40000017u);
  EXPECT_EQ(fifo::cursor_snapshot().completed, 15u);
}

TEST_F(GXDisplayListTest, AbortRevokesPendingListCallbackAndDiscardsItsRemainingCommands) {
  const aurora::os::GuestThreadExecutionScope execution;
  std::vector<u8> list;
  bp(list, 0x48000007);
  bp(list, 0x40000011);
  GXSetDrawSyncCallback(token_callback);
  fifo::begin_frame();
  GXCallDisplayList(list.data(), list.size());
  ASSERT_TRUE(until([] { return fifo::cursor_snapshot().consumed == 15; }));
  {
    const aurora::os::GuestInterruptExecutionScope interrupt;
    GXAbortFrame();
    fifo::write_u8(GX_LOAD_BP_REG);
    fifo::write_u32(0x40000017);
  }
  fifo::drain();
  EXPECT_EQ(sTokens.load(), 0u);
  EXPECT_EQ(aurora::gx::g_gxState.bpRegCache[0x40], 0x40000017u);
  EXPECT_EQ(fifo::cursor_snapshot().completed, 20u);
  EXPECT_TRUE(fifo::detail::sCPUStream.load()->displayList.empty());
}

TEST_F(GXDisplayListTest, EveryPartialNativeCallHeaderWaitsBeforeFetchingTheList) {
  std::vector<u8> list;
  bp(list, 0x48000007);
  GXSetDrawSyncCallback(token_callback);
  fifo::begin_frame();
  for (u32 split = 1; split < 15; ++split) {
    SCOPED_TRACE(split);
    const auto before = fifo::cursor_snapshot();
    GXEnableBreakPt(before.addressBase + (before.initialReadOffset + before.consumed + split) % before.addressSize);
    GXCallDisplayList(list.data(), list.size());
    ASSERT_TRUE(until([] { return fifo::cursor_snapshot().breakpoint; }));
    EXPECT_EQ(fifo::cursor_snapshot().consumed, before.consumed + split);
    EXPECT_EQ(sTokens.load(), split - 1);
    GXDisableBreakPt();
    fifo::drain();
    EXPECT_EQ(sTokens.load(), split);
  }
}

TEST_F(GXDisplayListTest, AbortDuringCallSubmissionDropsTheWholeRemainingHeader) {
  alignas(32) std::array<u8, 128> ring{};
  GXFifoObj object;
  GXInitFifoBase(&object, ring.data(), ring.size());
  GXInitFifoLimits(&object, 4, 1);
  GXSetCPUFifo(&object);
  GXSetGPFifo(&object);
  GXEnableBreakPt(ring.data());
  fifo::begin_frame();
  std::vector<u8> list;
  bp(list, 0x40000011);
  auto producer = std::async(std::launch::async, [&] {
    const aurora::os::GuestThreadExecutionScope execution;
    GXCallDisplayList(list.data(), list.size());
  });
  EXPECT_TRUE(until([] { return fifo::cursor_snapshot().written == 5 && fifo::cursor_snapshot().breakpoint; }));
  EXPECT_EQ(producer.wait_for(10ms), std::future_status::timeout);
  {
    const aurora::os::GuestInterruptExecutionScope interrupt;
    GXAbortFrame();
  }
  ASSERT_EQ(producer.wait_for(2s), std::future_status::ready);
  producer.get();
  EXPECT_EQ(fifo::cursor_snapshot().written, 5u);
  fifo::drain();
  fifo::write_u8(GX_LOAD_BP_REG);
  fifo::write_u32(0x40000017);
  fifo::drain();
  EXPECT_EQ(aurora::gx::g_gxState.bpRegCache[0x40], 0x40000017u);
}

TEST_F(GXDisplayListTest, CompleteListStillRejectsTruncatedCommands) {
  const std::array<u8, 4> truncated{GX_LOAD_BP_REG, 0x40, 0, 0};
  EXPECT_DEATH(fifo::process(truncated.data(), truncated.size(), {}, fifo::InputMode::DisplayList), ".*");
}
