#include <dolphin/os.h>
#include "dolphin/os/internal.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <algorithm>

namespace {

constexpr std::size_t kArenaBytes = 64 * 1024;

std::array<std::uint8_t, kArenaBytes> gArena{};

void resetAllocator(int maxHeaps = 8) {
  std::fill(gArena.begin(), gArena.end(), 0);
  void* start = gArena.data();
  void* end = gArena.data() + gArena.size();
  ASSERT_NE(OSInitAlloc(start, end, maxHeaps), nullptr);
  EXPECT_EQ(OSSetCurrentHeap(-1), -1);
}

} // namespace

TEST(OSMemory, RealMem1AllocationIsZeroedAndPreservesHeapPhysicalAlignment) {
  aurora::g_config.mem1Size = 65543; // The requested extent need not be a multiple of alignment.
  AuroraOSInitMemory();
  ASSERT_NE(MEM1Start, nullptr);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(MEM1Start) & 31u, 0u);
  auto* begin = static_cast<u8*>(MEM1Start);
  EXPECT_EQ(MEM1End, begin + aurora::g_config.mem1Size);
  EXPECT_TRUE(std::all_of(begin, begin + aurora::g_config.mem1Size, [](u8 value) { return value == 0; }));
  void* arena = OSInitAlloc(begin + 256, MEM1End, 1);
  ASSERT_NE(arena, nullptr);
  const auto heap = OSCreateHeap(arena, MEM1End);
  ASSERT_GE(heap, 0);
  for (u32 size : {1u, 31u, 32u, 97u, 4096u}) {
    auto* allocation = OSAllocFromHeap(heap, size);
    ASSERT_NE(allocation, nullptr);
    const u32 physical = OSCachedToPhysical(allocation);
    EXPECT_EQ(physical & 31u, 0u);
    EXPECT_EQ(OSPhysicalToCached((physical >> 5) << 5), allocation);
    OSFreeToHeap(heap, allocation);
  }
  EXPECT_GE(OSCheckHeap(heap), 0);
  OSDestroyHeap(heap);
}

TEST(OSAlloc, InitCreateAllocFreeCheck) {
  resetAllocator();

  OSHeapHandle heap = OSCreateHeap(gArena.data() + 512, gArena.data() + 4096);
  ASSERT_GE(heap, 0);

  EXPECT_EQ(OSSetCurrentHeap(heap), -1);

  void* p = OSAllocFromHeap(heap, 96);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(reinterpret_cast<std::uintptr_t>(p) & 31u, 0u);
  EXPECT_GE(OSReferentSize(p), 96u);

  s32 freeBefore = OSCheckHeap(heap);
  EXPECT_GE(freeBefore, 0);

  OSFreeToHeap(heap, p);
  s32 freeAfter = OSCheckHeap(heap);
  EXPECT_GE(freeAfter, freeBefore);
}

TEST(OSAlloc, AddToHeapMakesSpaceAvailable) {
  resetAllocator();

  OSHeapHandle heap = OSCreateHeap(gArena.data() + 0x1000, gArena.data() + 0x2000);
  ASSERT_GE(heap, 0);

  void* p1 = OSAllocFromHeap(heap, 0xD00);
  ASSERT_NE(p1, nullptr);

  void* p2 = OSAllocFromHeap(heap, 0x400);
  EXPECT_EQ(p2, nullptr);

  OSAddToHeap(heap, gArena.data() + 0x3000, gArena.data() + 0x3800);

  p2 = OSAllocFromHeap(heap, 0x400);
  EXPECT_NE(p2, nullptr);
}

TEST(OSAlloc, AllocFixedCarvesRangeFromHeap) {
  resetAllocator();

  OSHeapHandle heap = OSCreateHeap(gArena.data() + 0x2000, gArena.data() + 0x5000);
  ASSERT_GE(heap, 0);

  s32 freeBefore = OSCheckHeap(heap);
  ASSERT_GT(freeBefore, 0);

  void* fixed = OSAllocFixed(gArena.data() + 0x3000, gArena.data() + 0x3400);
  ASSERT_NE(fixed, nullptr);

  s32 freeAfter = OSCheckHeap(heap);
  EXPECT_GE(freeBefore, freeAfter);
}

TEST(OSAlloc, SetCurrentHeapReturnsPrevious) {
  resetAllocator();

  OSHeapHandle heapA = OSCreateHeap(gArena.data() + 0x1000, gArena.data() + 0x2000);
  OSHeapHandle heapB = OSCreateHeap(gArena.data() + 0x3000, gArena.data() + 0x4000);
  ASSERT_GE(heapA, 0);
  ASSERT_GE(heapB, 0);

  EXPECT_EQ(OSSetCurrentHeap(heapA), -1);
  EXPECT_EQ(OSSetCurrentHeap(heapB), heapA);

  void* p = OSAlloc(128);
  ASSERT_NE(p, nullptr);
  OSFree(p);

  EXPECT_GE(OSCheckHeap(heapB), 0);
}

TEST(OSAlloc, DestroyHeapInvalidatesHandle) {
  resetAllocator();

  OSHeapHandle heap = OSCreateHeap(gArena.data() + 0x1000, gArena.data() + 0x2000);
  ASSERT_GE(heap, 0);

  OSDestroyHeap(heap);
  EXPECT_LT(OSCheckHeap(heap), 0);
  EXPECT_EQ(OSAllocFromHeap(heap, 64), nullptr);
}
