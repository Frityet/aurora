#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void* OSGetMEM2ArenaLo(void);
void* OSGetMEM2ArenaHi(void);
void OSSetMEM2ArenaLo(void*);
void OSSetMEM2ArenaHi(void*);

#ifdef __cplusplus
}
#endif
