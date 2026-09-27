#pragma once

#include <revolution.h>

#define WPAD_RESULT_SUCCESS WPAD_ERR_NONE
#define WPAD_RESULT_ERR_1 WPAD_ERR_NO_CONTROLLER
#define WPAD_RESULT_ERR_2 WPAD_ERR_BUSY
#define WPAD_RESULT_ERR_3 WPAD_ERR_TRANSFER

#ifdef __cplusplus
extern "C" {
#endif

// The host keyboard/mouse backend does not expose Wii Remote face storage.
// A connected virtual controller reports WPAD_ERR_TRANSFER; a missing one
// reports WPAD_ERR_NO_CONTROLLER. On failure no callback is scheduled.
s32 WPADReadFaceData(s32 channel, void* buffer, u32 size, u32 address,
                     WPADCallback callback);

#ifdef __cplusplus
}
#endif
