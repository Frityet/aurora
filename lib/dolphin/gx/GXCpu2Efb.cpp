#include "gx.hpp"
#include "__gx.h"

#include "../../gfx/depth_peek.hpp"
#include "../../gx/fifo.hpp"

#include <dolphin/gx/GXAurora.h>
#include <dolphin/gx/GXCpu2Efb.h>
#include <aurora/depth_snapshot.hpp>
#include <aurora/exception.hpp>
#include <stdexcept>

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
