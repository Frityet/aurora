#include "gx.hpp"
#include "__gx.h"
#include "../../gx/destruction_state.hpp"
#include "dolphin/gx/GXAurora.h"

extern "C" {
void GXDestroyTexObj(GXTexObj* obj_) {
  auto* obj = reinterpret_cast<GXTexObj_*>(obj_);
  const auto texObjId = obj->texObjId;
  obj->texObjId = 0;
  if (texObjId == 0) {
    return;
  }
  aurora::gx::with_destruction_commands_enabled([texObjId] {
    GX_WRITE_AURORA(GX_AURORA_DESTROY_TEXOBJ);
    GX_WRITE_U32(texObjId);
  });
}

void GXDestroyTlutObj(GXTlutObj* obj_) {
  auto* obj = reinterpret_cast<GXTlutObj_*>(obj_);
  const auto tlutObjId = obj->tlutObjId;
  obj->tlutObjId = 0;
  if (tlutObjId == 0) {
    return;
  }
  aurora::gx::with_destruction_commands_enabled([tlutObjId] {
    GX_WRITE_AURORA(GX_AURORA_DESTROY_TLUT);
    GX_WRITE_U32(tlutObjId);
  });
}

void GXDestroyCopyTex(void* dest) {
  const auto identity = reinterpret_cast<std::uintptr_t>(dest);
  if (identity == 0) {
    return;
  }
  aurora::gx::with_destruction_commands_enabled([identity] {
    GX_WRITE_AURORA(GX_AURORA_DESTROY_COPY_TEX);
    GX_WRITE_U64(static_cast<u64>(identity));
  });
}
}
