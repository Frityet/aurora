#pragma once
#include <dolphin/os/OSThread.h>
extern "C" {
OSPriority __OSGetEffectivePriority(OSThread* thread);
void __OSPromoteThread(OSThread* thread, OSPriority priority);
void __OSUnlockAllMutex(OSThread* thread);
}
