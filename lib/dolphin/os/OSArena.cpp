#include <cassert>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <aurora/exception.hpp>
#include <aurora/mem2_arena.hpp>
#include <dolphin/os/OSArena.h>

#include "internal.hpp"

#include <dolphin/os.h>

#define ROUND64(n, a) (((u64)(n) + (a)-1) & ~(u64)((a)-1))
#define TRUNC64(n, a) (((u64)(n)) & ~(u64)((a)-1))

static void* ArenaLow;
static void* ArenaHigh;

namespace {
std::mutex mem2_mutex;
std::uintptr_t mem2_begin;
std::uintptr_t mem2_end;
std::uintptr_t mem2_low;
std::uintptr_t mem2_high;
}

void aurora::bind_mem2_arena(void* memory, std::size_t size) {
  const std::lock_guard lock(mem2_mutex);
  const auto begin = reinterpret_cast<std::uintptr_t>(memory);
  if (mem2_begin != 0 || begin == 0 || (begin & 31) != 0 || size == 0 || size > UINTPTR_MAX - begin) {
    aurora::throw_host_exception<std::invalid_argument>("MEM2 requires one aligned, caller-owned arena");
  }
  mem2_begin = mem2_low = begin;
  mem2_end = mem2_high = begin + size;
}

void aurora::unbind_mem2_arena(void* memory) {
  const std::lock_guard lock(mem2_mutex);
  if (reinterpret_cast<std::uintptr_t>(memory) != mem2_begin) {
    aurora::throw_host_exception<std::logic_error>("MEM2 arena release does not match its owner");
  }
  mem2_begin = mem2_end = mem2_low = mem2_high = 0;
}

void* OSGetMEM2ArenaLo() {
  const std::lock_guard lock(mem2_mutex);
  return reinterpret_cast<void*>(mem2_low);
}
void* OSGetMEM2ArenaHi() {
  const std::lock_guard lock(mem2_mutex);
  return reinterpret_cast<void*>(mem2_high);
}
void OSSetMEM2ArenaLo(void* memory) {
  const std::lock_guard lock(mem2_mutex);
  const auto value = reinterpret_cast<std::uintptr_t>(memory);
  if (mem2_begin == 0 || value < mem2_begin || value > mem2_high) {
    aurora::throw_host_exception<std::out_of_range>("MEM2 low watermark exceeds its retained arena");
  }
  mem2_low = value;
}
void OSSetMEM2ArenaHi(void* memory) {
  const std::lock_guard lock(mem2_mutex);
  const auto value = reinterpret_cast<std::uintptr_t>(memory);
  if (mem2_begin == 0 || value < mem2_low || value > mem2_end) {
    aurora::throw_host_exception<std::out_of_range>("MEM2 high watermark exceeds its retained arena");
  }
  mem2_high = value;
}

void* OSGetArenaHi() {
  return ArenaHigh;
}

void* OSGetArenaLo() {
  return ArenaLow;
}

void OSSetArenaHi(void* newHi) {
  assert(newHi <= MEM1End && newHi >= MEM1Start);
  ArenaHigh = newHi;
}

void OSSetArenaLo(void* newLo) {
  assert(newLo <= MEM1End && newLo >= MEM1Start);
  ArenaLow = newLo;
}

void* OSAllocFromArenaLo(const u32 size, const u32 align) {
  void* ptr = OSGetArenaLo();
  auto arenaLo = static_cast<u8*>(ptr = reinterpret_cast<void*>(ROUND64(ptr, align)));
  arenaLo += size;
  arenaLo = reinterpret_cast<u8*>(ROUND64(arenaLo, align));
  OSSetArenaLo(arenaLo);
  return ptr;
}

void* OSAllocFromArenaHi(const u32 size, const u32 align) {
  void* ptr;

  auto arenaHi = static_cast<u8*>(OSGetArenaHi());
  arenaHi = reinterpret_cast<u8*>(TRUNC64(arenaHi, align));
  arenaHi -= size;
  arenaHi = static_cast<u8*>(ptr = reinterpret_cast<void*>(TRUNC64(arenaHi, align)));
  OSSetArenaHi(arenaHi);
  return ptr;
}

void AuroraInitArena() {
  if (MEM1Start == nullptr) {
    return;
  }

  OSSetArenaLo(static_cast<u8*>(MEM1Start) + ARENA_START_OFFSET);
  OSSetArenaHi(MEM1End);
}
