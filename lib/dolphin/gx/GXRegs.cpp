#include <revolution/gx/GXRegs.h>
#include "../../gx/fifo.hpp"

extern "C" {
void GXWriteFifoU8(u8 value) { aurora::gx::fifo::write_u8(value); }
void GXWriteFifoU16(u16 value) { aurora::gx::fifo::write_u16(value); }
void GXWriteFifoU32(u32 value) { aurora::gx::fifo::write_u32(value); }
void GXWriteFifoF32(f32 value) { aurora::gx::fifo::write_f32(value); }
}
