#include "gx.hpp"
#include "__gx.h"

#include "../../gfx/depth_peek.hpp"
#include "../../gx/fifo.hpp"

#include <dolphin/gx/GXAurora.h>
#include <dolphin/gx/GXCpu2Efb.h>
#include <aurora/depth_snapshot.hpp>
#include <aurora/exception.hpp>
#include <stdexcept>
#include <array>
#include <atomic>

namespace aurora::gx {
bool peek_efb_rgba8(u16 x, u16 y, std::array< u8, 4 >& rgba);
}

namespace {
std::atomic< GXAlphaReadMode > sAlphaReadMode{GX_READ_NONE};
}

void GXPokeAlphaRead(GXAlphaReadMode mode) {
  sAlphaReadMode.store(mode, std::memory_order_release);
}

void GXPeekARGB(u16 x, u16 y, u32* color) {
  if (color == nullptr) {
    return;
  }

  std::array< u8, 4 > rgba{};
  if (!aurora::gx::peek_efb_rgba8(x, y, rgba)) {
    *color = 0;
    return;
  }

  u8 alpha = rgba[3];
  switch (sAlphaReadMode.load(std::memory_order_acquire)) {
  case GX_READ_00:
    alpha = 0;
    break;
  case GX_READ_FF:
    alpha = 0xFF;
    break;
  case GX_READ_NONE:
  default:
    break;
  }

  *color = static_cast< u32 >(alpha) << 24 | static_cast< u32 >(rgba[0]) << 16 | static_cast< u32 >(rgba[1]) << 8 |
           static_cast< u32 >(rgba[2]);
}

void GXPeekZ(u16 x, u16 y, u32* z) {
  if (aurora::selected_depth_snapshot != 0) {
    if (z != nullptr && !aurora::gfx::depth_peek::read_snapshot(aurora::selected_depth_snapshot, x, y, *z)) {
      aurora::throw_host_exception<std::logic_error>("GXPeekZ draw-sync snapshot is unavailable or its coordinates are outside the captured EFB");
    }
    return;
  }
  if (z != nullptr) {
    if (aurora::gx::fifo::peek_draw_sync_z(x, y, *z)) return;
    u32 value = 0;
    if (aurora::gfx::depth_peek::read_latest(x, y, value)) {
      *z = value;
    } else {
      *z = 0;
    }
  }

  GX_WRITE_AURORA(GX_AURORA_REQUEST_DEPTH_SNAPSHOT);
}
