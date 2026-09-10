#pragma once

#include <revolution/kpad.h>

struct HBMKPadData {
  KPADStatus* kpad;
  KPADVec2 pos;
  u32 use_devtype;
};

struct HBMControllerData {
  HBMKPadData wiiCon[WPAD_MAX_CONTROLLERS];
};
