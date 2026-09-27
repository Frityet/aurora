#include <dolphin/pad.h>
#include <dolphin/vi.h>
#include <revolution/sc.h>

extern "C" void PADControlMotor(u32, u32) {}

extern "C" BOOL PADSupportsRumble(u32) { return FALSE; }

extern "C" BOOL VIResetDimmingCount(void) { return TRUE; }

extern "C" u8 SCGetWpadSpeakerVolume(void) { return 0; }
