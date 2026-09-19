#ifndef DOLPHIN_GXDISPLIST_H
#define DOLPHIN_GXDISPLIST_H

#include <dolphin/types.h>

#ifdef __cplusplus
extern "C" {
#endif

void GXBeginDisplayList(void* list, u32 size);
u32 GXEndDisplayList(void);
// Source memory is borrowed until GP consumption. Retain the readable span
// until GXDrawDone or AuroraDrainGXCommands before freeing or reusing it.
void GXCallDisplayList(const void* list, u32 nbytes);

#ifdef __cplusplus
}
#endif

#endif
