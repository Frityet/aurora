#include <dolphin/pad.h>

extern "C" void PADControlMotor(u32, u32) {}

extern "C" BOOL PADSupportsRumble(u32) { return FALSE; }
