#include <revolution/sc.h>
#include "internal.hpp"

#include <dolphin/os.h>

static bool AlreadyInitialized;

void OSInit() {
  if (AlreadyInitialized) {
    return;
  }

  AlreadyInitialized = true;

  AuroraOSInitMemory();
  AuroraFillBootInfo();
  AuroraInitClock();
  AuroraInitArena();
}

extern "C" u32 OSGetSoundMode(void) {
  return SCGetSoundMode() == SC_SOUND_MODE_MONO ? 0 : 1;
}
