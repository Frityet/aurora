#include <dolphin/dvd.h>
#include <aurora/dvd.h>
#include <aurora/allocation.hpp>
#include <dolphin/os.h>
#include <aurora/guest_thread.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <future>
#include <mutex>
#include <memory>
#include <thread>
#include <vector>

// =============================================================================
// Tests that do NOT require a disc image
// =============================================================================

TEST(DVDStubs, Constants) {
  EXPECT_EQ(DVD_STATE_END, 0);
  EXPECT_EQ(DVD_STATE_BUSY, 1);
  EXPECT_EQ(DVD_STATE_CANCELED, 10);
  EXPECT_EQ(DVD_RESULT_GOOD, 0);
  EXPECT_EQ(DVD_RESULT_FATAL_ERROR, -1);
  EXPECT_EQ(DVD_RESULT_CANCELED, -3);
}

TEST(DVDStubs, StructSizes) {
  EXPECT_GT(sizeof(DVDDiskID), 0u);
  EXPECT_GT(sizeof(DVDCommandBlock), 0u);
  EXPECT_GT(sizeof(DVDFileInfo), 0u);
  EXPECT_GT(sizeof(DVDDir), 0u);
  EXPECT_GT(sizeof(DVDDirEntry), 0u);
  EXPECT_GE(sizeof(DVDFileInfo), sizeof(DVDCommandBlock));
}

TEST(DVDStubs, InitWithoutDisc) { DVDInit(); }

TEST(DVDStubs, GetDriveStatus) { EXPECT_EQ(DVDGetDriveStatus(), DVD_STATE_NO_DISK); }

TEST(DVDNoDisc, CheckDiskCompletesInlineWithoutChangingUnrelatedFields) {
  const aurora::os::GuestThreadExecutionScope execution;
  struct State { OSContext* context; unsigned calls = 0; } state{OSGetCurrentContext()};
  DVDCommandBlock block{};
  block.command = 123;
  block.offset = 456;
  block.transferredSize = 789;
  block.userData = &state;
  ASSERT_TRUE(DVDCheckDiskAsync(&block, [](s32 result, DVDCommandBlock* block) {
    auto& state = *static_cast<State*>(block->userData);
    EXPECT_EQ(result, FALSE);
    EXPECT_EQ(block->state, DVD_STATE_END);
    EXPECT_EQ(OSGetCurrentContext(), state.context);
    EXPECT_EQ(OSDisableInterrupts(), FALSE);
    EXPECT_EQ(OSDisableScheduler(), 0);
    EXPECT_EQ(OSEnableScheduler(), 1);
    ++state.calls;
  }));
  EXPECT_EQ(state.calls, 1U);
  EXPECT_EQ(block.command, 123U);
  EXPECT_EQ(block.offset, 456U);
  EXPECT_EQ(block.transferredSize, 789U);
  EXPECT_EQ(block.callback, nullptr);
}

TEST(DVDStubs, Reset) { DVDReset(); }

TEST(DVDStubs, ResetRequired) { EXPECT_EQ(DVDResetRequired(), FALSE); }

TEST(DVDStubs, PauseResume) {
  DVDPause();
  DVDResume();
}

TEST(DVDStubs, AutoInvalidation) {
  BOOL prev = DVDSetAutoInvalidation(TRUE);
  EXPECT_EQ(prev, FALSE);
  prev = DVDSetAutoInvalidation(FALSE);
  EXPECT_EQ(prev, TRUE);
  prev = DVDSetAutoInvalidation(FALSE);
  EXPECT_EQ(prev, FALSE);
}

TEST(DVDStubs, Cancel) {
  DVDCommandBlock block{};
  block.state = DVD_STATE_BUSY;
  s32 result = DVDCancel(&block);
  EXPECT_EQ(result, 0);
  EXPECT_EQ(block.state, DVD_STATE_CANCELED);
}

TEST(DVDStubs, CancelAsync) {
  DVDCommandBlock block{};
  block.state = DVD_STATE_BUSY;
  DVDCancelAsync(&block, [](s32, DVDCommandBlock*) {});
  EXPECT_EQ(block.state, DVD_STATE_CANCELED);
}

TEST(DVDStubs, CancelAll) { EXPECT_EQ(DVDCancelAll(), DVD_RESULT_GOOD); }

TEST(DVDStubs, SeekStubs) {
  DVDFileInfo fi{};
  fi.cb.state = DVD_STATE_BUSY;
  s32 result = DVDSeekPrio(&fi, 0, 2);
  EXPECT_EQ(result, DVD_RESULT_FATAL_ERROR);
  EXPECT_EQ(fi.cb.state, DVD_STATE_FATAL_ERROR);

  fi.cb.state = DVD_STATE_BUSY;
  BOOL ok = DVDSeekAsyncPrio(&fi, 0, [](s32, DVDFileInfo*) {}, 2);
  EXPECT_EQ(ok, TRUE);
  EXPECT_EQ(fi.cb.state, DVD_STATE_FATAL_ERROR);
}

TEST(DVDStubs, GetFSTLocation) { EXPECT_EQ(DVDGetFSTLocation(), nullptr); }

TEST(DVDStubs, GetCurrentDiskID) {
  DVDDiskID* id = DVDGetCurrentDiskID();
  EXPECT_NE(id, nullptr);
}

TEST(DVDStubs, FileInfoStatus) {
  DVDFileInfo fi{};
  fi.cb.state = DVD_STATE_END;
  EXPECT_EQ(DVDGetFileInfoStatus(&fi), DVD_STATE_END);
  fi.cb.state = DVD_STATE_BUSY;
  EXPECT_EQ(DVDGetFileInfoStatus(&fi), DVD_STATE_BUSY);
}

TEST(DVDStubs, TransferredSize) {
  DVDFileInfo fi{};
  fi.cb.transferredSize = 1234;
  EXPECT_EQ(DVDGetTransferredSize(&fi), 1234);
}

TEST(DVDStubs, CommandBlockStatus) {
  DVDCommandBlock block{};
  block.state = DVD_STATE_WAITING;
  EXPECT_EQ(DVDGetCommandBlockStatus(&block), DVD_STATE_WAITING);
}

// =============================================================================
// Without a disc: operations should fail gracefully
// =============================================================================

TEST(DVDNoDisc, OpenFails) {
  DVDFileInfo fi{};
  EXPECT_EQ(DVDOpen("test.bin", &fi), FALSE);
}

TEST(DVDNoDisc, FastOpenFails) {
  DVDFileInfo fi{};
  EXPECT_EQ(DVDFastOpen(0, &fi), FALSE);
}

TEST(DVDNoDisc, CloseNullHandle) {
  DVDFileInfo fi{};
  fi.cb.userData = nullptr;
  fi.cb.state = DVD_STATE_BUSY;
  EXPECT_EQ(DVDClose(&fi), TRUE);
  EXPECT_EQ(fi.cb.state, DVD_STATE_END);
}

TEST(DVDNoDisc, OpenDirFails) {
  DVDDir dir{};
  EXPECT_EQ(DVDOpenDir("/", &dir), FALSE);
}

TEST(DVDNoDisc, CloseDir) {
  DVDDir dir{};
  EXPECT_EQ(DVDCloseDir(&dir), TRUE);
}

TEST(DVDNoDisc, ChangeDirFails) { EXPECT_EQ(DVDChangeDir("/"), FALSE); }

TEST(DVDNoDisc, ConvertPathFails) { EXPECT_EQ(DVDConvertPathToEntrynum("/test"), -1); }

TEST(DVDNoDisc, ConvertEntrynumToPathFails) {
  char buf[16] = "unchanged";
  EXPECT_EQ(DVDConvertEntrynumToPath(0, buf, sizeof(buf)), FALSE);
  EXPECT_STREQ(buf, "");
  EXPECT_EQ(DVDConvertEntrynumToPath(0, nullptr, sizeof(buf)), FALSE);
  EXPECT_EQ(DVDConvertEntrynumToPath(0, buf, 0), FALSE);
}

TEST(DVDNoDisc, GetCurrentDir) {
  char buf[256];
  EXPECT_EQ(DVDGetCurrentDir(buf, sizeof(buf)), TRUE);
  EXPECT_STREQ(buf, "/");
}

// =============================================================================
// Tests that require a disc image (conditionally compiled)
// =============================================================================

class DVDDiscTest : public ::testing::Test {
protected:
  static const char* image() {
    if (const char* path = std::getenv("SMGPC_REAL_DISC"); path && *path) return path;
#ifdef DVD_TEST_IMAGE
    return DVD_TEST_IMAGE;
#else
    return nullptr;
#endif
  }
  static void SetUpTestSuite() {
    if (image() == nullptr) GTEST_SKIP() << "Set SMGPC_REAL_DISC for real DVD tests";
    ASSERT_TRUE(aurora_dvd_open(image()));
    DVDInit();
  }

  static void TearDownTestSuite() { aurora_dvd_close(); }
};

// Helper: find the first file entry in root by iterating the directory.
// Returns the entry number and fills fileName, or returns -1 if none found.
static s32 findFirstRootFile(char* fileName, size_t fileNameSize) {
  DVDDir dir{};
  if (!DVDOpenDir("/", &dir))
    return -1;

  DVDDirEntry dirent{};
  s32 fileEntry = -1;
  while (DVDReadDir(&dir, &dirent)) {
    if (!dirent.isDir) {
      fileEntry = static_cast<s32>(dirent.entryNum);
      std::snprintf(fileName, fileNameSize, "/%s", dirent.name);
      break;
    }
  }
  DVDCloseDir(&dir);
  return fileEntry;
}

TEST_F(DVDDiscTest, ConvertPathRoot) { EXPECT_EQ(DVDConvertPathToEntrynum("/"), 0); }

TEST_F(DVDDiscTest, ConvertPathDotDotDot) {
  EXPECT_EQ(DVDConvertPathToEntrynum("."), 0);
  EXPECT_EQ(DVDConvertPathToEntrynum(".."), 0);
}

TEST_F(DVDDiscTest, ConvertPathInvalid) {
  EXPECT_EQ(DVDConvertPathToEntrynum("/nonexistent_file_that_should_not_exist"), -1);
}

TEST_F(DVDDiscTest, ConvertEntrynumToPathRoot) {
  char path[2] = {};
  EXPECT_EQ(DVDConvertEntrynumToPath(0, path, sizeof(path)), TRUE);
  EXPECT_STREQ(path, "/");
}

TEST_F(DVDDiscTest, ConvertEntrynumToPathFile) {
  char expectedPath[256] = {};
  const s32 entryNum = findFirstRootFile(expectedPath, sizeof(expectedPath));
  if (entryNum < 0) {
    GTEST_SKIP() << "No files in root directory";
  }

  char actualPath[256] = {};
  EXPECT_EQ(DVDConvertEntrynumToPath(entryNum, actualPath, sizeof(actualPath)), TRUE);
  EXPECT_STREQ(actualPath, expectedPath);

  char truncatedPath[2] = {};
  EXPECT_EQ(DVDConvertEntrynumToPath(entryNum, truncatedPath, sizeof(truncatedPath)), FALSE);
  EXPECT_EQ(truncatedPath[sizeof(truncatedPath) - 1], '\0');
}

TEST_F(DVDDiscTest, ConvertEntrynumToPathOverlay) {
  const AuroraOverlayCallbacks callbacks{
      .open = [](void*) -> void* { return nullptr; },
      .close = [](void*) {},
      .read = [](void*, uint8_t*, size_t) -> int64_t { return 0; },
      .seek = [](void*, int64_t, int32_t) -> int64_t { return 0; },
  };
  aurora_dvd_overlay_callbacks(&callbacks);

  const AuroraOverlayFile overlay{
      .fileName = "/__aurora_dvd_test__/nested/file.bin",
      .userData = nullptr,
      .size = 0,
  };
  s32 fileEntryNum = -1;
  aurora_dvd_overlay_files(&overlay, 1, &fileEntryNum);
  struct OverlayReset {
    ~OverlayReset() { aurora_dvd_overlay_files(nullptr, 0, nullptr); }
  } overlayReset;

  ASSERT_GE(fileEntryNum, 0);
  char path[256] = {};
  EXPECT_EQ(DVDConvertEntrynumToPath(fileEntryNum, path, sizeof(path)), TRUE);
  EXPECT_STREQ(path, overlay.fileName);

  const s32 dirEntryNum = DVDConvertPathToEntrynum("/__aurora_dvd_test__/nested");
  ASSERT_GE(dirEntryNum, 0);
  EXPECT_EQ(DVDConvertEntrynumToPath(dirEntryNum, path, sizeof(path)), TRUE);
  EXPECT_STREQ(path, "/__aurora_dvd_test__/nested/");
}

TEST_F(DVDDiscTest, OpenDirRoot) {
  DVDDir dir{};
  EXPECT_EQ(DVDOpenDir("/", &dir), TRUE);
  EXPECT_EQ(dir.entryNum, 0u);
  EXPECT_EQ(dir.location, 1u);

  DVDDirEntry dirent{};
  BOOL hasEntry = DVDReadDir(&dir, &dirent);
  if (hasEntry) {
    EXPECT_NE(dirent.name, nullptr);
    EXPECT_GT(std::strlen(dirent.name), 0u);
  }
  DVDCloseDir(&dir);
}

TEST_F(DVDDiscTest, ChangeDirRoot) {
  EXPECT_EQ(DVDChangeDir("/"), TRUE);
  char buf[256];
  DVDGetCurrentDir(buf, sizeof(buf));
  EXPECT_STREQ(buf, "/");
}

TEST_F(DVDDiscTest, OpenCloseFile) {
  char fileName[256] = {};
  s32 fileEntry = findFirstRootFile(fileName, sizeof(fileName));
  if (fileEntry < 0) {
    GTEST_SKIP() << "No files in root directory";
  }

  DVDFileInfo fi{};
  EXPECT_EQ(DVDOpen(fileName, &fi), TRUE);
  EXPECT_GT(fi.length, 0u);
  EXPECT_EQ(fi.cb.state, DVD_STATE_END);
  EXPECT_EQ(DVDClose(&fi), TRUE);

  EXPECT_EQ(DVDFastOpen(fileEntry, &fi), TRUE);
  EXPECT_EQ(DVDClose(&fi), TRUE);
}

TEST_F(DVDDiscTest, ReadFile) {
  char fileName[256] = {};
  if (findFirstRootFile(fileName, sizeof(fileName)) < 0) {
    GTEST_SKIP() << "No files in root directory";
  }

  DVDFileInfo fi{};
  ASSERT_EQ(DVDOpen(fileName, &fi), TRUE);

  u32 readSize = fi.length < 32 ? fi.length : 32;
  alignas(32) std::array<u8, 32> buf{};
  s32 bytesRead = DVDReadPrio(&fi, buf.data(), buf.size(), 0, 2);
  EXPECT_EQ(bytesRead, static_cast<s32>(readSize));
  EXPECT_EQ(DVDGetTransferredSize(&fi), static_cast<s32>(readSize));

  DVDClose(&fi);
}

TEST_F(DVDDiscTest, ReadAsync) {
  char fileName[256] = {};
  if (findFirstRootFile(fileName, sizeof(fileName)) < 0) {
    GTEST_SKIP() << "No files in root directory";
  }

  DVDFileInfo fi{};
  ASSERT_EQ(DVDOpen(fileName, &fi), TRUE);

  u32 readSize = fi.length < 32 ? fi.length : 32;
  alignas(32) std::array<u8, 32> buf{};
  BOOL ok = DVDReadAsyncPrio(&fi, buf.data(), buf.size(), 0, [](s32, DVDFileInfo*) {}, 2);
  EXPECT_EQ(ok, TRUE);
  for (int i = 0; i < 5000 && (DVDGetFileInfoStatus(&fi) == DVD_STATE_WAITING ||
                               DVDGetFileInfoStatus(&fi) == DVD_STATE_BUSY);
       ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  EXPECT_EQ(DVDGetFileInfoStatus(&fi), DVD_STATE_END);
  EXPECT_EQ(DVDGetTransferredSize(&fi), static_cast<s32>(readSize));

  DVDClose(&fi);
}

TEST_F(DVDDiscTest, DiskID) {
  DVDDiskID* id = DVDGetCurrentDiskID();
  ASSERT_NE(id, nullptr);
  bool hasGameName = false;
  for (int i = 0; i < 4; i++) {
    if (id->gameName[i] != '\0') {
      hasGameName = true;
      break;
    }
  }
  EXPECT_TRUE(hasGameName);
}

TEST_F(DVDDiscTest, RawCommandPreservesCallerData) {
  DVDCommandBlock command{};
  std::promise<void> done;
  auto ready = done.get_future();
  command.userData = &done;
  alignas(32) std::array<u8, 32> bytes{};
  ASSERT_TRUE(DVDReadAbsAsyncPrio(&command, bytes.data(), bytes.size(), 0, [](s32 result, DVDCommandBlock* block) {
    EXPECT_EQ(result, 32);
    static_cast<std::promise<void>*>(block->userData)->set_value();
  }, 2));
  EXPECT_EQ(ready.wait_for(std::chrono::seconds(5)), std::future_status::ready);
  // The cancellation boundary drains any callback still returning after its
  // promise notification before this stack command may be reclaimed.
  EXPECT_EQ(DVDCancel(&command), DVD_RESULT_GOOD);
  EXPECT_EQ(command.userData, &done);
  EXPECT_EQ(std::memcmp(bytes.data(), DVDGetCurrentDiskID(), offsetof(DVDDiskID, padding)), 0);
}

namespace {
struct DescriptorProbe {
  std::array<u8, 64> bytes{};
  std::atomic<unsigned> opens = 0;
  std::atomic<unsigned> closes = 0;
  std::atomic<unsigned> active = 0;
  std::mutex mutex;
  std::condition_variable wake;
  bool blocked = false;
  bool entered = false;
  bool released = false;
  bool blockOpen = false;
  bool openEntered = false;
  bool enterGuestAfterWait = false;
  std::atomic<unsigned> guestEntries = 0;

  void release() {
    const std::lock_guard lock(mutex);
    released = true;
    wake.notify_all();
  }
  bool await_entry() {
    std::unique_lock lock(mutex);
    return wake.wait_for(lock, std::chrono::seconds(5), [&] { return entered; });
  }
  bool await_open() {
    std::unique_lock lock(mutex);
    return wake.wait_for(lock, std::chrono::seconds(5), [&] { return openEntered; });
  }
  void enter_guest_if_requested() {
    if (enterGuestAfterWait) {
      const aurora::os::GuestThreadExecutionScope execution;
      ++guestEntries;
    }
  }
};
struct DescriptorHandle {
  DescriptorProbe* probe;
  std::size_t offset = 0;
};
struct PendingDescriptorScope {
  DescriptorProbe& probe;
  ~PendingDescriptorScope() {
    probe.release();
    DVDCancelAll();
  }
};
const AuroraOverlayCallbacks descriptorCallbacks{
    .open = [](void* data) -> void* {
      auto* probe = static_cast<DescriptorProbe*>(data);
      // Overlay callbacks can perform DVD lookups without the FST lock held.
      EXPECT_GE(DVDConvertPathToEntrynum("/"), 0);
      ++probe->opens;
      ++probe->active;
      {
        std::unique_lock lock(probe->mutex);
        probe->openEntered = true;
        probe->wake.notify_all();
        probe->wake.wait(lock, [&] { return !probe->blockOpen || probe->released; });
      }
      probe->enter_guest_if_requested();
      return new DescriptorHandle{probe};
    },
    .close = [](void* data) {
      auto* handle = static_cast<DescriptorHandle*>(data);
      ++handle->probe->closes;
      --handle->probe->active;
      delete handle;
    },
    .read = [](void* data, u8* output, size_t size) -> int64_t {
      auto& handle = *static_cast<DescriptorHandle*>(data);
      auto& probe = *handle.probe;
      {
        std::unique_lock lock(probe.mutex);
        probe.entered = true;
        probe.wake.notify_all();
        probe.wake.wait(lock, [&] { return !probe.blocked || probe.released; });
      }
      probe.enter_guest_if_requested();
      const auto count = std::min(size, probe.bytes.size() - handle.offset);
      std::copy_n(probe.bytes.data() + handle.offset, count, output);
      handle.offset += count;
      return static_cast<int64_t>(count);
    },
    .seek = [](void* data, int64_t offset, int32_t whence) -> int64_t {
      auto& handle = *static_cast<DescriptorHandle*>(data);
      if (whence != 0 || offset < 0 || static_cast<std::uint64_t>(offset) > handle.probe->bytes.size()) return -1;
      handle.offset = static_cast<std::size_t>(offset);
      return offset;
    },
};
}

class DVDDescriptorTest : public DVDDiscTest {
protected:
  DescriptorProbe first;
  DescriptorProbe second;
  static constexpr const char* firstPath = "/__aurora_descriptor__/first.bin";
  static constexpr const char* secondPath = "/__aurora_descriptor__/second.bin";
  void SetUp() override {
    for (std::size_t i = 0; i < first.bytes.size(); ++i) {
      first.bytes[i] = static_cast<u8>(i);
      second.bytes[i] = static_cast<u8>(255 - i);
    }
    aurora_dvd_overlay_callbacks(&descriptorCallbacks);
    const AuroraOverlayFile files[]{{firstPath, &first, first.bytes.size()}, {secondPath, &second, second.bytes.size()}};
    aurora_dvd_overlay_files(files, 2, nullptr);
  }
  void TearDown() override {
    first.release();
    second.release();
    DVDCancelAll();
    EXPECT_EQ(first.active.load(), 0U);
    EXPECT_EQ(second.active.load(), 0U);
    EXPECT_EQ(first.opens.load(), first.closes.load());
    EXPECT_EQ(second.opens.load(), second.closes.load());
    aurora_dvd_overlay_files(nullptr, 0, nullptr);
  }
};

TEST_F(DVDDescriptorTest, StackReuseWithoutCloseAndCallerData) {
  const aurora::os::GuestThreadExecutionScope execution;
  DVDFileInfo file{};
  int caller = 17;
  file.cb.userData = &caller;
  alignas(32) std::array<u8, 32> bytes;
  for (unsigned i = 0; i < 64; ++i) {
    auto& probe = (i & 1) ? second : first;
    ASSERT_TRUE(DVDOpen((i & 1) ? secondPath : firstPath, &file));
    EXPECT_EQ(file.cb.userData, &caller);
    EXPECT_EQ(probe.opens.load(), probe.closes.load());
    EXPECT_EQ(DVDReadPrio(&file, bytes.data(), bytes.size(), 32, 2), bytes.size());
    EXPECT_TRUE(std::equal(bytes.begin(), bytes.end(), probe.bytes.begin() + 32));
    EXPECT_EQ(probe.opens.load(), (i / 2) + 1);
    EXPECT_EQ(probe.active.load(), 0U);
    EXPECT_EQ(probe.opens.load(), probe.closes.load());
  }
  // The final stack descriptor also intentionally has no DVDClose call.
  EXPECT_EQ(file.cb.userData, &caller);
}

TEST_F(DVDDescriptorTest, CallbackClosesAndReopensSameDescriptor) {
  struct State {
    DescriptorProbe* first;
    DescriptorProbe* second;
    alignas(32) std::array<u8, 32> buffer;
    std::promise<void> done;
    unsigned callbacks = 0;
  } state{&first, &second};
  auto ready = state.done.get_future();
  DVDFileInfo file{};
  file.cb.userData = &state;
  ASSERT_TRUE(DVDOpen(firstPath, &file));
  ASSERT_TRUE(DVDReadAsyncPrio(&file, state.buffer.data(), state.buffer.size(), 0, [](s32 result, DVDFileInfo* info) {
    auto& state = *static_cast<State*>(info->cb.userData);
    EXPECT_EQ(result, 32);
    EXPECT_EQ(state.first->active.load(), 0U);
    EXPECT_TRUE(std::equal(state.buffer.begin(), state.buffer.end(), state.first->bytes.begin()));
    ++state.callbacks;
    EXPECT_TRUE(DVDClose(info));
    EXPECT_TRUE(DVDOpen(secondPath, info));
    EXPECT_EQ(info->cb.userData, &state);
    EXPECT_TRUE(DVDReadAsyncPrio(info, state.buffer.data(), state.buffer.size(), 0, [](s32 result, DVDFileInfo* reused) {
      auto& state = *static_cast<State*>(reused->cb.userData);
      EXPECT_EQ(result, 32);
      EXPECT_EQ(state.second->active.load(), 0U);
      EXPECT_TRUE(std::equal(state.buffer.begin(), state.buffer.end(), state.second->bytes.begin()));
      ++state.callbacks;
      state.done.set_value();
    }, 2));
  }, 2));
  EXPECT_EQ(ready.wait_for(std::chrono::seconds(5)), std::future_status::ready);
  EXPECT_TRUE(DVDClose(&file));
  EXPECT_EQ(state.callbacks, 2U);
  EXPECT_EQ(first.opens.load(), 1U);
  EXPECT_EQ(second.opens.load(), 1U);
}

TEST_F(DVDDescriptorTest, CancelQueuedCommandDoesNotAcquireHandle) {
  first.blocked = true;
  DVDFileInfo active{}, pending{};
  alignas(32) std::array<u8, 32> activeBytes{}, pendingBytes{};
  std::atomic<unsigned> callbacks = 0;
  PendingDescriptorScope pendingScope{first};
  ASSERT_TRUE(DVDOpen(firstPath, &active));
  ASSERT_TRUE(DVDOpen(secondPath, &pending));
  ASSERT_TRUE(DVDReadAsyncPrio(&active, activeBytes.data(), activeBytes.size(), 0, nullptr, 2));
  ASSERT_TRUE(first.await_entry());
  pending.cb.userData = &callbacks;
  ASSERT_TRUE(DVDReadAsyncPrio(&pending, pendingBytes.data(), pendingBytes.size(), 0, [](s32 result, DVDFileInfo* info) {
    EXPECT_EQ(result, DVD_RESULT_CANCELED);
    ++*static_cast<std::atomic<unsigned>*>(info->cb.userData);
  }, 2));
  EXPECT_EQ(DVDCancel(&pending.cb), DVD_RESULT_GOOD);
  EXPECT_EQ(callbacks.load(), 1U);
  EXPECT_EQ(second.opens.load(), 0U);
  EXPECT_EQ(pendingBytes, (std::array<u8, 32>{}));
  first.release();
  EXPECT_TRUE(DVDClose(&active));
  EXPECT_TRUE(DVDClose(&pending));
}

TEST_F(DVDDescriptorTest, CloseWaitsForActiveCommandAndItsCallback) {
  first.blocked = true;
  DVDFileInfo file{};
  alignas(32) std::array<u8, 32> bytes{};
  std::atomic<unsigned> callbacks = 0;
  PendingDescriptorScope pendingScope{first};
  file.cb.userData = &callbacks;
  ASSERT_TRUE(DVDOpen(firstPath, &file));
  ASSERT_TRUE(DVDReadAsyncPrio(&file, bytes.data(), bytes.size(), 0, [](s32 result, DVDFileInfo* info) {
    EXPECT_EQ(result, DVD_RESULT_CANCELED);
    ++*static_cast<std::atomic<unsigned>*>(info->cb.userData);
  }, 2));
  ASSERT_TRUE(first.await_entry());
  auto close = std::async(std::launch::async, [&] { return DVDClose(&file); });
  EXPECT_EQ(close.wait_for(std::chrono::milliseconds(30)), std::future_status::timeout);
  EXPECT_EQ(first.active.load(), 1U);
  first.release();
  ASSERT_EQ(close.wait_for(std::chrono::seconds(5)), std::future_status::ready);
  EXPECT_TRUE(close.get());
  EXPECT_EQ(callbacks.load(), 1U);
  EXPECT_EQ(first.active.load(), 0U);
  EXPECT_EQ(first.closes.load(), 1U);
  EXPECT_EQ(file.cb.userData, &callbacks);
}

TEST_F(DVDDescriptorTest, DiscReplacementInvalidatesBorrowedDescriptor) {
  DVDFileInfo stale{};
  ASSERT_TRUE(DVDOpen(firstPath, &stale));
  aurora_dvd_close();
  ASSERT_TRUE(aurora_dvd_open(image()));
  alignas(32) std::array<u8, 32> bytes{};
  EXPECT_EQ(DVDReadPrio(&stale, bytes.data(), bytes.size(), 0, 2), DVD_RESULT_FATAL_ERROR);
  EXPECT_EQ(first.opens.load(), 0U);
  EXPECT_EQ(bytes, (std::array<u8, 32>{}));
  ASSERT_TRUE(DVDOpen(firstPath, &stale));
  EXPECT_EQ(DVDReadPrio(&stale, bytes.data(), bytes.size(), 0, 2), bytes.size());
  EXPECT_TRUE(std::equal(bytes.begin(), bytes.end(), first.bytes.begin()));
}

TEST_F(DVDDescriptorTest, CatalogReplacementInvalidatesBorrowedDescriptor) {
  DVDFileInfo stale{};
  ASSERT_TRUE(DVDOpen(firstPath, &stale));
  const AuroraOverlayFile replacement{firstPath, &second, second.bytes.size()};
  aurora_dvd_overlay_files(&replacement, 1, nullptr);
  alignas(32) std::array<u8, 32> bytes{};
  EXPECT_EQ(DVDReadPrio(&stale, bytes.data(), bytes.size(), 0, 2), DVD_RESULT_FATAL_ERROR);
  EXPECT_EQ(first.opens.load(), 0U);
  EXPECT_EQ(second.opens.load(), 0U);
  ASSERT_TRUE(DVDOpen(firstPath, &stale));
  EXPECT_EQ(DVDReadPrio(&stale, bytes.data(), bytes.size(), 0, 2), bytes.size());
  EXPECT_TRUE(std::equal(bytes.begin(), bytes.end(), second.bytes.begin()));
}

TEST_F(DVDDescriptorTest, CatalogReplacementDrainsOpenAndReadBeforePayloadRetirement) {
  for (bool blockOpen : {true, false}) {
    auto old = std::make_unique<DescriptorProbe>();
    old->bytes.fill(0x4D);
    old->blockOpen = blockOpen;
    old->blocked = !blockOpen;
    old->enterGuestAfterWait = true;
    const AuroraOverlayFile original{firstPath, old.get(), old->bytes.size()};
    aurora_dvd_overlay_files(&original, 1, nullptr);
    DVDFileInfo file{};
    alignas(32) std::array<u8, 32> bytes{};
    {
      PendingDescriptorScope pendingScope{*old};
      ASSERT_TRUE(DVDOpen(firstPath, &file));
      ASSERT_TRUE(DVDReadAsyncPrio(&file, bytes.data(), bytes.size(), 0, nullptr, 2));
      ASSERT_TRUE(blockOpen ? old->await_open() : old->await_entry());
      std::promise<void> replacing;
      auto started = replacing.get_future();
      auto replacement = std::async(std::launch::async, [&] {
        // The replacement wait must release this CPU ownership: the old
        // operation deliberately needs it after its test barrier is released.
        const aurora::os::GuestThreadExecutionScope execution;
        replacing.set_value();
        const AuroraOverlayFile updated{firstPath, &second, second.bytes.size()};
        aurora_dvd_overlay_files(&updated, 1, nullptr);
        return old->active.load();
      });
      EXPECT_EQ(started.wait_for(std::chrono::seconds(5)), std::future_status::ready);
      EXPECT_EQ(replacement.wait_for(std::chrono::milliseconds(30)), std::future_status::timeout);
      old->release();
      ASSERT_EQ(replacement.wait_for(std::chrono::seconds(5)), std::future_status::ready);
      EXPECT_EQ(replacement.get(), 0U);
      EXPECT_TRUE(DVDClose(&file));
      EXPECT_EQ(old->opens.load(), 1U);
      EXPECT_EQ(old->closes.load(), 1U);
      EXPECT_GT(old->guestEntries.load(), 0U);
      EXPECT_TRUE(std::all_of(bytes.begin(), bytes.end(), [](u8 b) { return b == 0x4D; }));
    }
    old.reset(); // No operation may retain or dereference this old payload.
    ASSERT_TRUE(DVDOpen(firstPath, &file));
    EXPECT_EQ(DVDReadPrio(&file, bytes.data(), bytes.size(), 0, 2), bytes.size());
    EXPECT_TRUE(std::equal(bytes.begin(), bytes.end(), second.bytes.begin()));
  }
}

TEST_F(DVDDescriptorTest, CompletionWakesGuestAfterInterruptReturnsAndRestoresRouting) {
  const aurora::os::GuestThreadExecutionScope execution;
  for (bool guest : {true, false}) {
    struct State {
      OSMessageQueue queue{};
      OSMessage storage{};
      OSContext* callerContext;
      bool guest;
      bool callbackReturned = false;
      bool observedReturn = false;
    } state{{}, {}, OSGetCurrentContext(), guest};
    OSInitMessageQueue(&state.queue, &state.storage, 1);
    OSThread waiter{};
    std::array<u8, 16384> stack{};
    ASSERT_TRUE(OSCreateThread(&waiter, [](void* data) -> void* {
      auto& state = *static_cast<State*>(data);
      EXPECT_TRUE(OSReceiveMessage(&state.queue, nullptr, OS_MESSAGE_BLOCK));
      state.observedReturn = state.callbackReturned;
      return nullptr;
    }, &state, stack.data() + stack.size(), stack.size(), 5, 0));
    OSResumeThread(&waiter);
    DVDFileInfo file{};
    alignas(32) std::array<u8, 32> bytes{};
    file.cb.userData = &state;
    ASSERT_TRUE(DVDOpen(firstPath, &file));
    {
      const aurora::allocation::ClientAllocationScope allocations({guest, guest});
      const aurora::allocation::HostAllocationScope native;
      ASSERT_TRUE(DVDReadAsyncPrio(&file, bytes.data(), bytes.size(), 0, [](s32 result, DVDFileInfo* file) {
        auto& state = *static_cast<State*>(file->cb.userData);
        EXPECT_EQ(result, 32);
        EXPECT_EQ(DVDGetFileInfoStatus(file), DVD_STATE_END);
        EXPECT_NE(OSGetCurrentContext(), state.callerContext);
        EXPECT_EQ(OSDisableInterrupts(), FALSE);
        EXPECT_EQ(OSDisableScheduler(), 1);
        EXPECT_EQ(OSEnableScheduler(), 2);
        EXPECT_EQ(aurora::allocation::routing_state.guest, state.guest);
        EXPECT_EQ(aurora::allocation::routing_state.callbackGuest, state.guest);
        EXPECT_TRUE(OSSendMessage(&state.queue, nullptr, OS_MESSAGE_NOBLOCK));
        EXPECT_FALSE(state.observedReturn);
        state.callbackReturned = true;
      }, 2));
      EXPECT_FALSE(aurora::allocation::routing_state.guest);
      EXPECT_EQ(aurora::allocation::routing_state.callbackGuest, guest);
    }
    EXPECT_TRUE(OSJoinThread(&waiter, nullptr));
    EXPECT_TRUE(DVDClose(&file));
    EXPECT_TRUE(state.observedReturn);
    EXPECT_EQ(file.cb.userData, &state);
    EXPECT_EQ(OSGetCurrentContext(), state.callerContext);
    EXPECT_EQ(OSDisableScheduler(), 0);
    EXPECT_EQ(OSEnableScheduler(), 1);
    EXPECT_EQ(OSDisableInterrupts(), TRUE);
    OSRestoreInterrupts(TRUE);
  }
}

TEST_F(DVDDescriptorTest, CheckDiskQueuesIdleAndPausedQueriesWithCapturedCallbackRouting) {
  const aurora::os::GuestThreadExecutionScope execution;
  for (bool paused : {false, true}) {
    for (bool guest : {false, true}) {
      struct State { OSMessageQueue queue{}; OSMessage message{}; OSContext* caller; bool guest; unsigned calls = 0; }
          state{{}, {}, OSGetCurrentContext(), guest};
      OSInitMessageQueue(&state.queue, &state.message, 1);
      DVDCommandBlock block{};
      block.addr = &state;
      block.offset = 123;
      block.length = 456;
      block.transferredSize = 789;
      block.userData = &state;
      if (paused) DVDPause();
      {
        const aurora::allocation::ClientAllocationScope policy({guest, guest});
        const aurora::allocation::HostAllocationScope native;
        ASSERT_TRUE(DVDCheckDiskAsync(&block, [](s32 result, DVDCommandBlock* block) {
          auto& state = *static_cast<State*>(block->userData);
          EXPECT_EQ(result, TRUE);
          EXPECT_EQ(block->state, DVD_STATE_END);
          EXPECT_EQ(DVDGetDriveStatus(), DVD_STATE_END);
          EXPECT_NE(OSGetCurrentContext(), state.caller);
          EXPECT_EQ(OSDisableInterrupts(), FALSE);
          EXPECT_EQ(OSDisableScheduler(), 1);
          EXPECT_EQ(OSEnableScheduler(), 2);
          EXPECT_EQ(aurora::allocation::routing_state.guest, state.guest);
          EXPECT_EQ(aurora::allocation::routing_state.callbackGuest, state.guest);
          ++state.calls;
          EXPECT_TRUE(OSSendMessage(&state.queue, nullptr, OS_MESSAGE_NOBLOCK));
        }));
      }
      EXPECT_EQ(state.calls, 0U);
      if (paused) {
        EXPECT_EQ(DVDGetCommandBlockStatus(&block), DVD_STATE_WAITING);
        EXPECT_EQ(DVDGetDriveStatus(), DVD_STATE_PAUSING);
        DVDResume();
      }
      EXPECT_TRUE(OSReceiveMessage(&state.queue, nullptr, OS_MESSAGE_BLOCK));
      EXPECT_EQ(DVDCancel(&block), DVD_RESULT_GOOD);
      EXPECT_EQ(state.calls, 1U);
      EXPECT_EQ(block.command, DVD_COMMAND_CHECK_DISK);
      EXPECT_EQ(block.addr, &state);
      EXPECT_EQ(block.offset, 123U);
      EXPECT_EQ(block.length, 456U);
      EXPECT_EQ(block.transferredSize, 789U);
      EXPECT_EQ(block.userData, &state);
    }
  }
}

TEST_F(DVDDescriptorTest, CheckDiskDuringActiveReadCompletesInTheCallerContext) {
  first.blocked = true;
  PendingDescriptorScope cleanup{first};
  const aurora::os::GuestThreadExecutionScope execution;
  DVDFileInfo file{};
  alignas(32) std::array<u8, 32> bytes{};
  ASSERT_TRUE(DVDOpen(firstPath, &file));
  ASSERT_TRUE(DVDReadAsyncPrio(&file, bytes.data(), bytes.size(), 0, nullptr, 2));
  ASSERT_TRUE(first.await_entry());
  DVDPause();
  EXPECT_EQ(DVDGetDriveStatus(), DVD_STATE_BUSY);
  struct State { OSContext* caller; unsigned calls = 0; } state{OSGetCurrentContext()};
  DVDCommandBlock block{};
  block.command = 123;
  block.userData = &state;
  ASSERT_TRUE(DVDCheckDiskAsync(&block, [](s32 result, DVDCommandBlock* block) {
    auto& state = *static_cast<State*>(block->userData);
    EXPECT_EQ(result, TRUE);
    EXPECT_EQ(block->state, DVD_STATE_END);
    EXPECT_EQ(OSGetCurrentContext(), state.caller);
    EXPECT_EQ(OSDisableInterrupts(), FALSE);
    EXPECT_EQ(OSDisableScheduler(), 0);
    EXPECT_EQ(OSEnableScheduler(), 1);
    ++state.calls;
  }));
  EXPECT_EQ(state.calls, 1U);
  EXPECT_EQ(block.command, 123U);
  EXPECT_EQ(block.callback, nullptr);
  DVDResume();
  first.release();
  EXPECT_TRUE(DVDClose(&file));
}

TEST_F(DVDDescriptorTest, QueuedCancellationPreservesCallerContextAndBothRoutingPolicies) {
  first.blocked = true;
  PendingDescriptorScope cleanup{first};
  const aurora::os::GuestThreadExecutionScope execution;
  struct State { OSContext* context; unsigned calls = 0; } state{OSGetCurrentContext()};
  DVDFileInfo active{}, queued{};
  alignas(32) std::array<u8, 32> activeBytes{}, queuedBytes{};
  ASSERT_TRUE(DVDOpen(firstPath, &active));
  ASSERT_TRUE(DVDOpen(secondPath, &queued));
  ASSERT_TRUE(DVDReadAsyncPrio(&active, activeBytes.data(), activeBytes.size(), 0, nullptr, 2));
  ASSERT_TRUE(first.await_entry());
  queued.cb.userData = &state;
  {
    const aurora::allocation::ClientAllocationScope allocations({true, true});
    ASSERT_TRUE(DVDReadAsyncPrio(&queued, queuedBytes.data(), queuedBytes.size(), 0, [](s32 result, DVDFileInfo* file) {
      auto& state = *static_cast<State*>(file->cb.userData);
      EXPECT_EQ(result, DVD_RESULT_CANCELED);
      EXPECT_EQ(OSGetCurrentContext(), state.context);
      EXPECT_EQ(OSDisableInterrupts(), FALSE);
      EXPECT_EQ(OSDisableScheduler(), 0);
      EXPECT_EQ(OSEnableScheduler(), 1);
      EXPECT_TRUE(aurora::allocation::routing_state.guest);
      EXPECT_TRUE(aurora::allocation::routing_state.callbackGuest);
      EXPECT_EQ(state.calls++, 0U);
    }, 2));
  }
  {
    const aurora::allocation::ClientAllocationScope allocations({false, false});
    EXPECT_TRUE(DVDCancelAsync(&queued.cb, [](s32 result, DVDCommandBlock* block) {
      auto& state = *static_cast<State*>(block->userData);
      EXPECT_EQ(result, DVD_RESULT_GOOD);
      EXPECT_EQ(OSGetCurrentContext(), state.context);
      EXPECT_EQ(OSDisableInterrupts(), FALSE);
      EXPECT_EQ(OSDisableScheduler(), 0);
      EXPECT_EQ(OSEnableScheduler(), 1);
      EXPECT_FALSE(aurora::allocation::routing_state.guest);
      EXPECT_FALSE(aurora::allocation::routing_state.callbackGuest);
      EXPECT_EQ(state.calls++, 1U);
    }));
  }
  EXPECT_EQ(state.calls, 2U);
  EXPECT_EQ(second.opens.load(), 0U);
  EXPECT_EQ(OSDisableInterrupts(), TRUE);
  OSRestoreInterrupts(TRUE);
  first.release();
  EXPECT_TRUE(DVDClose(&active));
  EXPECT_TRUE(DVDClose(&queued));
}

TEST_F(DVDDescriptorTest, NativeRetirementWaitsReleaseGuestCpuForCompletion) {
  enum class Boundary { Close, Cancel, CancelAll, Stop };
  for (auto boundary : {Boundary::Close, Boundary::Cancel, Boundary::CancelAll, Boundary::Stop}) {
    first.blocked = true;
    first.released = false;
    first.entered = false;
    PendingDescriptorScope cleanup{first};
    DVDFileInfo file{};
    alignas(32) std::array<u8, 32> bytes{};
    std::atomic<unsigned> callbacks = 0;
    file.cb.userData = &callbacks;
    ASSERT_TRUE(DVDOpen(firstPath, &file));
    {
      const aurora::allocation::ClientAllocationScope allocations({true, true});
      ASSERT_TRUE(DVDReadAsyncPrio(&file, bytes.data(), bytes.size(), 0, [](s32 result, DVDFileInfo* file) {
        EXPECT_EQ(result, DVD_RESULT_CANCELED);
        EXPECT_EQ(OSDisableInterrupts(), FALSE);
        EXPECT_EQ(OSDisableScheduler(), 1);
        EXPECT_EQ(OSEnableScheduler(), 2);
        EXPECT_TRUE(aurora::allocation::routing_state.guest);
        ++*static_cast<std::atomic<unsigned>*>(file->cb.userData);
      }, 2));
    }
    ASSERT_TRUE(first.await_entry());
    std::promise<void> retiring;
    auto started = retiring.get_future();
    auto retired = std::async(std::launch::async, [&] {
      const aurora::os::GuestThreadExecutionScope execution;
      const aurora::allocation::ClientAllocationScope allocations({false, false});
      retiring.set_value();
      switch (boundary) {
      case Boundary::Close: EXPECT_TRUE(DVDClose(&file)); break;
      case Boundary::Cancel: EXPECT_EQ(DVDCancel(&file.cb), DVD_RESULT_GOOD); break;
      case Boundary::CancelAll: EXPECT_EQ(DVDCancelAll(), DVD_RESULT_GOOD); break;
      case Boundary::Stop: aurora_dvd_close(); break;
      }
      EXPECT_FALSE(aurora::allocation::routing_state.guest);
      EXPECT_FALSE(aurora::allocation::routing_state.callbackGuest);
    });
    ASSERT_EQ(started.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    EXPECT_EQ(retired.wait_for(std::chrono::milliseconds(30)), std::future_status::timeout);
    first.release();
    ASSERT_EQ(retired.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    retired.get();
    EXPECT_EQ(callbacks.load(), 1U);
    EXPECT_EQ(first.active.load(), 0U);
    EXPECT_EQ(file.cb.userData, &callbacks);
    if (boundary == Boundary::Stop) ASSERT_TRUE(aurora_dvd_open(image()));
  }
}

TEST_F(DVDDescriptorTest, StaleCatalogReadStillDeliversCapturedInterruptCallback) {
  DVDFileInfo file{};
  alignas(32) std::array<u8, 32> bytes{};
  ASSERT_TRUE(DVDOpen(firstPath, &file));
  const AuroraOverlayFile replacement{firstPath, &second, second.bytes.size()};
  aurora_dvd_overlay_files(&replacement, 1, nullptr);
  const aurora::os::GuestThreadExecutionScope execution;
  struct State { OSMessageQueue queue{}; OSMessage storage{}; } state;
  OSInitMessageQueue(&state.queue, &state.storage, 1);
  file.cb.userData = &state;
  {
    const aurora::allocation::ClientAllocationScope allocations({true, true});
    ASSERT_TRUE(DVDReadAsyncPrio(&file, bytes.data(), bytes.size(), 0, [](s32 result, DVDFileInfo* file) {
      auto& state = *static_cast<State*>(file->cb.userData);
      EXPECT_EQ(result, DVD_RESULT_FATAL_ERROR);
      EXPECT_EQ(DVDGetFileInfoStatus(file), DVD_STATE_FATAL_ERROR);
      EXPECT_EQ(OSDisableInterrupts(), FALSE);
      EXPECT_EQ(OSDisableScheduler(), 1);
      EXPECT_EQ(OSEnableScheduler(), 2);
      EXPECT_TRUE(aurora::allocation::routing_state.guest);
      EXPECT_TRUE(OSSendMessage(&state.queue, nullptr, OS_MESSAGE_NOBLOCK));
    }, 2));
  }
  EXPECT_TRUE(OSReceiveMessage(&state.queue, nullptr, OS_MESSAGE_BLOCK));
  EXPECT_TRUE(DVDClose(&file));
  EXPECT_EQ(first.opens.load(), 0U);
  EXPECT_EQ(second.opens.load(), 0U);
  EXPECT_EQ(bytes, (std::array<u8, 32>{}));
}

TEST(DVDCallbacks, LowRegistrationRetainsPolicyAndRestoresCallerState) {
  const aurora::os::GuestThreadExecutionScope execution;
  struct State { OSContext* context; unsigned calls = 0; } state{OSGetCurrentContext()};
  static State* active;
  active = &state;
  const auto callback = +[](u32 result) {
    EXPECT_EQ(result, 0U);
    EXPECT_NE(OSGetCurrentContext(), active->context);
    EXPECT_EQ(OSDisableInterrupts(), FALSE);
    EXPECT_EQ(OSDisableScheduler(), 1);
    EXPECT_EQ(OSEnableScheduler(), 2);
    EXPECT_TRUE(aurora::allocation::routing_state.guest);
    ++active->calls;
    EXPECT_NE(DVDLowClearCallback(), nullptr);
  };
  {
    const aurora::allocation::ClientAllocationScope allocations({true, true});
    EXPECT_EQ(DVDLowSetResetCoverCallback(callback), nullptr);
  }
  {
    const aurora::allocation::ClientAllocationScope allocations({false, false});
    DVDLowReset();
    DVDLowReset();
    EXPECT_FALSE(aurora::allocation::routing_state.guest);
    EXPECT_FALSE(aurora::allocation::routing_state.callbackGuest);
  }
  EXPECT_EQ(state.calls, 1U);
  EXPECT_EQ(OSGetCurrentContext(), state.context);
  EXPECT_EQ(OSDisableInterrupts(), TRUE);
  OSRestoreInterrupts(TRUE);
  EXPECT_EQ(OSDisableScheduler(), 0);
  EXPECT_EQ(OSEnableScheduler(), 1);
  active = nullptr;
}

TEST_F(DVDDescriptorTest, ActiveAsyncCancellationDefersBothCallbacksToOneInterrupt) {
  for (bool all : {false, true}) {
    first.blocked = true;
    first.released = false;
    first.entered = false;
    PendingDescriptorScope cleanup{first};
    const aurora::os::GuestThreadExecutionScope execution;
    struct State {
      OSMessageQueue queue{};
      OSMessage storage{};
      OSContext* interrupt = nullptr;
      unsigned calls = 0;
    } state;
    OSInitMessageQueue(&state.queue, &state.storage, 1);
    DVDFileInfo file{};
    alignas(32) std::array<u8, 32> bytes{};
    file.cb.userData = &state;
    ASSERT_TRUE(DVDOpen(firstPath, &file));
    {
      const aurora::allocation::ClientAllocationScope allocations({true, true});
      ASSERT_TRUE(DVDReadAsyncPrio(&file, bytes.data(), bytes.size(), 0, [](s32 result, DVDFileInfo* file) {
        auto& state = *static_cast<State*>(file->cb.userData);
        EXPECT_EQ(result, -3);
        EXPECT_EQ(DVDGetFileInfoStatus(file), DVD_STATE_CANCELED);
        EXPECT_EQ(OSDisableInterrupts(), FALSE);
        EXPECT_EQ(OSDisableScheduler(), 1);
        EXPECT_EQ(OSEnableScheduler(), 2);
        EXPECT_TRUE(aurora::allocation::routing_state.guest);
        state.interrupt = OSGetCurrentContext();
        EXPECT_EQ(state.calls++, 0U);
      }, 2));
    }
    ASSERT_TRUE(first.await_entry());
    const auto canceled = +[](s32 result, DVDCommandBlock* block) {
      ASSERT_NE(block, nullptr);
      auto& state = *static_cast<State*>(block->userData);
      EXPECT_EQ(result, 0);
      EXPECT_EQ(OSGetCurrentContext(), state.interrupt);
      EXPECT_EQ(OSDisableInterrupts(), FALSE);
      EXPECT_EQ(OSDisableScheduler(), 1);
      EXPECT_EQ(OSEnableScheduler(), 2);
      EXPECT_FALSE(aurora::allocation::routing_state.guest);
      EXPECT_EQ(state.calls++, 1U);
      EXPECT_TRUE(OSSendMessage(&state.queue, nullptr, OS_MESSAGE_NOBLOCK));
    };
    {
      const aurora::allocation::ClientAllocationScope allocations({false, false});
      EXPECT_TRUE(all ? DVDCancelAllAsync(canceled) : DVDCancelAsync(&file.cb, canceled));
      EXPECT_FALSE(all ? DVDCancelAllAsync(canceled) : DVDCancelAsync(&file.cb, canceled));
    }
    EXPECT_EQ(state.calls, 0U);
    EXPECT_EQ(DVDGetFileInfoStatus(&file), DVD_STATE_BUSY);
    first.release();
    EXPECT_TRUE(OSReceiveMessage(&state.queue, nullptr, OS_MESSAGE_BLOCK));
    EXPECT_TRUE(DVDClose(&file));
    EXPECT_EQ(state.calls, 2U);
  }
}

TEST_F(DVDDescriptorTest, PauseRetainsQueuedCommandsAndCancellationResumesTheWorker) {
  const aurora::os::GuestThreadExecutionScope execution;
  DVDFileInfo file{};
  alignas(32) std::array<u8, 32> bytes{};
  struct State { unsigned calls = 0; OSContext* context; } state{0, OSGetCurrentContext()};
  file.cb.userData = &state;
  ASSERT_TRUE(DVDOpen(firstPath, &file));
  DVDPause();
  EXPECT_EQ(DVDGetDriveStatus(), DVD_STATE_PAUSING);
  ASSERT_TRUE(DVDReadAsyncPrio(&file, bytes.data(), bytes.size(), 0, [](s32 result, DVDFileInfo* file) {
    auto& state = *static_cast<State*>(file->cb.userData);
    EXPECT_EQ(result, -3);
    EXPECT_EQ(OSGetCurrentContext(), state.context);
    EXPECT_EQ(OSDisableInterrupts(), FALSE);
    EXPECT_EQ(OSDisableScheduler(), 0);
    EXPECT_EQ(OSEnableScheduler(), 1);
    ++state.calls;
  }, 2));
  EXPECT_EQ(DVDGetFileInfoStatus(&file), DVD_STATE_WAITING);
  EXPECT_EQ(first.opens.load(), 0U);
  // The real cancellation-all contract resumes after draining queued work and
  // delivers 0/nullptr when there was no executing command.
  static State* active;
  active = &state;
  EXPECT_TRUE(DVDCancelAllAsync([](s32 result, DVDCommandBlock* block) {
    EXPECT_EQ(result, 0);
    EXPECT_EQ(block, nullptr);
    EXPECT_EQ(OSGetCurrentContext(), active->context);
    EXPECT_EQ(OSDisableInterrupts(), FALSE);
    EXPECT_EQ(OSDisableScheduler(), 0);
    EXPECT_EQ(OSEnableScheduler(), 1);
    EXPECT_EQ(active->calls++, 1U);
  }));
  active = nullptr;
  EXPECT_EQ(state.calls, 2U);
  EXPECT_EQ(DVDGetDriveStatus(), DVD_STATE_END);
  EXPECT_EQ(DVDGetFileInfoStatus(&file), DVD_STATE_CANCELED);
  EXPECT_EQ(first.opens.load(), 0U);
  // Reusing the same terminal command must work after cancellation resumed it.
  EXPECT_EQ(DVDReadPrio(&file, bytes.data(), bytes.size(), 0, 2), 32);
  EXPECT_TRUE(DVDClose(&file));
  EXPECT_EQ(first.opens.load(), 1U);
  DVDCommandBlock idle{};
  EXPECT_TRUE(DVDCancelAsync(&idle, nullptr));
  EXPECT_EQ(idle.state, DVD_STATE_END);
}

TEST_F(DVDDescriptorTest, CancelAllCallbackCanCancelAndReclaimAnotherQueuedDescriptor) {
  const aurora::os::GuestThreadExecutionScope execution;
  for (bool close : {false, true}) {
    struct State {
      bool close;
      std::unique_ptr<DVDFileInfo> middle = std::make_unique<DVDFileInfo>();
      std::array<unsigned, 3> order{};
      unsigned count = 0;
    } state{close};
    DVDFileInfo firstFile{}, lastFile{};
    alignas(32) std::array<std::array<u8, 32>, 3> bytes{};
    ASSERT_TRUE(DVDOpen(firstPath, &firstFile));
    ASSERT_TRUE(DVDOpen(firstPath, state.middle.get()));
    ASSERT_TRUE(DVDOpen(firstPath, &lastFile));
    firstFile.cb.userData = state.middle->cb.userData = lastFile.cb.userData = &state;
    DVDPause();
    ASSERT_TRUE(DVDReadAsyncPrio(&firstFile, bytes[0].data(), bytes[0].size(), 0, [](s32 result, DVDFileInfo* file) {
      auto& state = *static_cast<State*>(file->cb.userData);
      EXPECT_EQ(result, -3);
      state.order[state.count++] = 1;
      if (state.close) EXPECT_TRUE(DVDClose(state.middle.get()));
      else EXPECT_EQ(DVDCancel(&state.middle->cb), 0);
      state.middle.reset();
    }, 2));
    ASSERT_TRUE(DVDReadAsyncPrio(state.middle.get(), bytes[1].data(), bytes[1].size(), 0, [](s32 result, DVDFileInfo* file) {
      auto& state = *static_cast<State*>(file->cb.userData);
      EXPECT_EQ(result, -3);
      state.order[state.count++] = 2;
    }, 2));
    ASSERT_TRUE(DVDReadAsyncPrio(&lastFile, bytes[2].data(), bytes[2].size(), 0, [](s32 result, DVDFileInfo* file) {
      auto& state = *static_cast<State*>(file->cb.userData);
      EXPECT_EQ(result, -3);
      state.order[state.count++] = 3;
    }, 2));
    EXPECT_TRUE(DVDCancelAllAsync(nullptr));
    EXPECT_EQ(state.middle, nullptr);
    EXPECT_EQ(state.count, 3U);
    EXPECT_EQ(state.order, (std::array<unsigned, 3>{1, 2, 3}));
    EXPECT_EQ(first.opens.load(), 0U);
    EXPECT_TRUE(DVDClose(&firstFile));
    EXPECT_TRUE(DVDClose(&lastFile));
  }
}

TEST_F(DVDDescriptorTest, CompletedCallbackCanReclaimItsDescriptorBeforeCancelAll) {
  const aurora::os::GuestThreadExecutionScope execution;
  struct State {
    OSMessageQueue queue{};
    OSMessage storage{};
    bool reclaimed = false;
  } state;
  OSInitMessageQueue(&state.queue, &state.storage, 1);
  auto* file = new DVDFileInfo{};
  alignas(32) std::array<u8, 32> bytes{};
  file->cb.userData = &state;
  ASSERT_TRUE(DVDOpen(firstPath, file));
  ASSERT_TRUE(DVDReadAsyncPrio(file, bytes.data(), bytes.size(), 0, [](s32 result, DVDFileInfo* file) {
    auto& state = *static_cast<State*>(file->cb.userData);
    EXPECT_EQ(result, 32);
    delete file;
    state.reclaimed = true;
    EXPECT_TRUE(DVDCancelAllAsync([](s32 result, DVDCommandBlock* block) {
      EXPECT_EQ(result, 0);
      EXPECT_EQ(block, nullptr);
      EXPECT_EQ(OSDisableScheduler(), 1);
      EXPECT_EQ(OSEnableScheduler(), 2);
    }));
    EXPECT_TRUE(OSSendMessage(&state.queue, nullptr, OS_MESSAGE_NOBLOCK));
  }, 2));
  EXPECT_TRUE(OSReceiveMessage(&state.queue, nullptr, OS_MESSAGE_BLOCK));
  EXPECT_TRUE(state.reclaimed);
  EXPECT_EQ(first.active.load(), 0U);
}

TEST_F(DVDDescriptorTest, DiscServiceCloseFromItsWorkerCallbackIsRejected) {
  const auto previous = ::testing::GTEST_FLAG(death_test_style);
  ::testing::GTEST_FLAG(death_test_style) = "threadsafe";
  EXPECT_DEATH({
    DVDFileInfo file{};
    alignas(32) u8 bytes[32]{};
    std::promise<void> returned;
    auto done = returned.get_future();
    file.cb.userData = &returned;
    DVDOpen(firstPath, &file);
    DVDReadAsyncPrio(&file, bytes, sizeof(bytes), 0, [](s32, DVDFileInfo* file) {
      aurora_dvd_close();
      static_cast<std::promise<void>*>(file->cb.userData)->set_value();
    }, 2);
    done.wait();
  }, "DVD disc service cannot be stopped or replaced from its executing worker callback");
  ::testing::GTEST_FLAG(death_test_style) = previous;
}

TEST_F(DVDDescriptorTest, IdleWorkerCanRetireAfterMainGuestThreadLocalTeardown) {
  const auto previous = ::testing::GTEST_FLAG(death_test_style);
  ::testing::GTEST_FLAG(death_test_style) = "threadsafe";
  EXPECT_EXIT({
    { const aurora::os::GuestThreadExecutionScope execution; }
    // Deliberately bypass fixture teardown. Standard process exit first retires
    // the calling thread's TLS and then the idle DVD worker's static owner.
    std::exit(0);
  }, ::testing::ExitedWithCode(0), "");
  ::testing::GTEST_FLAG(death_test_style) = previous;
}

TEST_F(DVDDescriptorTest, StoppingWorkerKeepsQueuedDescriptorDiscoverableUntilGuestDelivery) {
  const aurora::os::GuestThreadExecutionScope execution;
  struct State { unsigned readCallbacks = 0; bool reclaimed = false; } state;
  auto* file = new DVDFileInfo{};
  alignas(32) std::array<u8, 32> bytes{};
  file->cb.userData = &state;
  ASSERT_TRUE(DVDOpen(firstPath, file));
  DVDPause();
  ASSERT_TRUE(DVDReadAsyncPrio(file, bytes.data(), bytes.size(), 0, [](s32 result, DVDFileInfo* file) {
    EXPECT_EQ(result, -3);
    ++static_cast<State*>(file->cb.userData)->readCallbacks;
  }, 2));
  std::promise<void> stopping;
  auto started = stopping.get_future();
  auto stopped = std::async(std::launch::async, [&] {
    stopping.set_value();
    aurora_dvd_close();
  });
  EXPECT_EQ(started.wait_for(std::chrono::seconds(5)), std::future_status::ready);
  EXPECT_EQ(stopped.wait_for(std::chrono::milliseconds(30)), std::future_status::timeout);
  // The stop caller needs this CPU to dispatch the queued callback. Until it
  // acquires it, the real queue must still allow this guest to cancel/reclaim.
  EXPECT_TRUE(DVDCancelAsync(&file->cb, [](s32 result, DVDCommandBlock* block) {
    EXPECT_EQ(result, 0);
    auto& state = *static_cast<State*>(block->userData);
    EXPECT_EQ(state.readCallbacks, 1U);
    delete reinterpret_cast<DVDFileInfo*>(block);
    state.reclaimed = true;
  }));
  EXPECT_TRUE(state.reclaimed);
  {
    const aurora::os::GuestThreadWaitScope wait;
    EXPECT_EQ(stopped.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    stopped.get();
  }
  EXPECT_EQ(state.readCallbacks, 1U);
  EXPECT_EQ(first.opens.load(), 0U);
  ASSERT_TRUE(aurora_dvd_open(image()));
}
