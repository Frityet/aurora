#include "gx.hpp"
#include "__gx.h"

#include "../../gx/fifo.hpp"

#include <cstring>

static __GXData_struct sSavedGXData;

extern "C" {
void GXBeginDisplayList(void* list, u32 size) {
  CHECK(!aurora::gx::fifo::in_display_list(), "Display list began twice!");

  // Flush any pending dirty state before recording
  if (__gx->dirtyState != 0) {
    __GXSetDirtyState();
  }

  // Save current shadow register state if requested
  if (__gx->dlSaveContext != 0) {
    std::memcpy(&sSavedGXData, __gx, sizeof(sSavedGXData));
  }

  __gx->inDispList = 1;

  // Redirect FIFO writes to the user-provided buffer
  aurora::gx::fifo::begin_display_list(static_cast<u8*>(list), size);
}

u32 GXEndDisplayList() {
  // Flush any pending dirty state into the display list
  if (__gx->dirtyState != 0) {
    __GXSetDirtyState();
  }

  // End FIFO redirection and get the byte count (ROUNDUP32)
  u32 bytesWritten = aurora::gx::fifo::end_display_list();

  // Restore saved shadow register state
  if (__gx->dlSaveContext != 0) {
    std::memcpy(__gx, &sSavedGXData, sizeof(*__gx));
  }

  __gx->inDispList = 0;

  return bytesWritten;
}

void GXCallDisplayList(const void* data, u32 nbytes) {
  // Flush any pending dirty state before calling
  if (__gx->dirtyState != 0) {
    __GXSetDirtyState();
  }

  // Flush pending primitives
  if (__gx->vNum != 0 && __gx->bpSent != 0) {
    __GXSendFlushPrim();
  }

  // Native pointers use the Aurora extension namespace; raw retail CALL_DL
  // addresses remain physical addresses. Borrow the span until GP fetch.
  // Submit one finalized record so abort during a high-water wait cancels
  // every remaining header byte, rather than emitting a later field alone.
  u8 command[15]{GX_AURORA};
  const auto subtype = bswap(u16{GX_AURORA_CALL_DISPLAY_LIST});
  const auto address = bswap(u64{reinterpret_cast<uintptr_t>(data)});
  const auto length = bswap(nbytes);
  std::memcpy(command + 1, &subtype, sizeof(subtype));
  std::memcpy(command + 3, &address, sizeof(address));
  std::memcpy(command + 11, &length, sizeof(length));
  aurora::gx::fifo::write_data(command, sizeof(command));
  aurora::gx::fifo::publish();
}

}
