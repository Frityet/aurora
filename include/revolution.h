#pragma once

#include <revolution/types.h>
#include <revolution/nand.h>

#include <dolphin/dvd.h>
#include <dolphin/gd.h>
#include <dolphin/gx.h>
#include <dolphin/mtx.h>
#include <dolphin/os.h>
#include <dolphin/pad.h>
#include <dolphin/vi.h>

#ifndef NO_INLINE
#if defined(__GNUC__) || defined(__clang__)
#define NO_INLINE __attribute__((noinline))
#else
#define NO_INLINE
#endif
#endif

typedef GXColor _GXColor;
typedef GXTexFmt _GXTexFmt;
typedef GXTlut _GXTlut;
typedef GXTexMapID _GXTexMapID;
typedef GXTexWrapMode _GXTexWrapMode;
typedef GXTexFilter _GXTexFilter;
typedef GXAnisotropy _GXAnisotropy;
typedef GXRenderModeObj _GXRenderModeObj;
typedef GXFBClamp _GXFBClamp;
typedef GXGamma _GXGamma;
typedef GXLightID _GXLightID;

#define WPAD_CHAN0 ((s32)0)
#define WPAD_CHAN1 ((s32)1)
#define WPAD_CHAN2 ((s32)2)
#define WPAD_CHAN3 ((s32)3)
#define WPAD_MAX_CONTROLLERS ((s32)4)
#define WPAD_DEV_CORE ((u32)0)
#define WPAD_DEV_FREESTYLE ((u32)1)
#define WPAD_DEV_CLASSIC ((u32)2)
#define WPAD_DEV_NOT_FOUND ((u32)253)
#define WPAD_DEV_UNKNOWN ((u32)255)
#define WPAD_FMT_CORE ((u32)0)
#define WPAD_FMT_CORE_ACC ((u32)1)
#define WPAD_FMT_CORE_ACC_DPD ((u32)2)
#define WPAD_FMT_FREESTYLE ((u32)3)
#define WPAD_FMT_FREESTYLE_ACC ((u32)4)
#define WPAD_FMT_FREESTYLE_ACC_DPD ((u32)5)
#define WPAD_FMT_CLASSIC ((u32)6)
#define WPAD_FMT_CLASSIC_ACC ((u32)7)
#define WPAD_FMT_CLASSIC_ACC_DPD ((u32)8)
#define WPAD_ERR_BUSY ((s32)-2)
#define WPAD_ERR_TRANSFER ((s32)-3)
#define WPAD_ERR_INVALID ((s32)-4)
#define WPAD_SENSOR_BAR_POS_BOTTOM ((u8)0)
#define WPAD_SENSOR_BAR_POS_TOP ((u8)1)
#define WPAD_BATTERY_LEVEL_CRITICAL ((s32)0)
#define WPAD_BATTERY_LEVEL_LOW ((s32)1)
#define WPAD_BATTERY_LEVEL_MEDIUM ((s32)2)
#define WPAD_BATTERY_LEVEL_HIGH ((s32)3)
#define WPAD_BATTERY_LEVEL_MAX ((s32)4)
#define WPAD_MOTOR_STOP ((u32)0)
#define WPAD_MOTOR_RUMBLE ((u32)1)
#define WPAD_BUTTON_LEFT ((u32)0x0001)
#define WPAD_BUTTON_RIGHT ((u32)0x0002)
#define WPAD_BUTTON_DOWN ((u32)0x0004)
#define WPAD_BUTTON_UP ((u32)0x0008)
#define WPAD_BUTTON_PLUS ((u32)0x0010)
#define WPAD_BUTTON_2 ((u32)0x0100)
#define WPAD_BUTTON_1 ((u32)0x0200)
#define WPAD_BUTTON_B ((u32)0x0400)
#define WPAD_BUTTON_A ((u32)0x0800)
#define WPAD_BUTTON_MINUS ((u32)0x1000)
#define WPAD_BUTTON_Z ((u32)0x2000)
#define WPAD_BUTTON_C ((u32)0x4000)
#define WPAD_BUTTON_HOME ((u32)0x8000)
#define KPAD_BUTTON_MASK ((u32)0x0000ffff)
#define KPAD_BUTTON_RPT ((u32)0x80000000)
#define WPAD_ERR_NONE ((s32)0)
#define WPAD_ERR_NO_CONTROLLER ((s32)-1)

typedef struct KPADVec2 {
    f32 x;
    f32 y;
} KPADVec2;

typedef struct KPADVec3 {
    f32 x;
    f32 y;
    f32 z;
} KPADVec3;

typedef union KPADEXStatus {
    struct {
        KPADVec2 stick;
        KPADVec3 acc;
        f32 acc_value;
        f32 acc_speed;
    } fs;
    struct {
        u32 hold;
        u32 trig;
        u32 release;
        KPADVec2 lstick;
        KPADVec2 rstick;
        f32 ltrigger;
        f32 rtrigger;
    } cl;
} KPADEXStatus;

#ifdef __cplusplus
#define AURORA_KPAD_DEFAULT(value) = value
#else
#define AURORA_KPAD_DEFAULT(value)
#endif

typedef struct KPADStatus {
    u32 hold AURORA_KPAD_DEFAULT(0U);
    u32 trig AURORA_KPAD_DEFAULT(0U);
    u32 release AURORA_KPAD_DEFAULT(0U);
    KPADVec3 acc AURORA_KPAD_DEFAULT({});
    f32 acc_value AURORA_KPAD_DEFAULT(0.0F);
    f32 acc_speed AURORA_KPAD_DEFAULT(0.0F);
    KPADVec2 pos AURORA_KPAD_DEFAULT({});
    KPADVec2 vec AURORA_KPAD_DEFAULT({});
    f32 speed AURORA_KPAD_DEFAULT(0.0F);
    KPADVec2 horizon AURORA_KPAD_DEFAULT({});
    KPADVec2 hori_vec AURORA_KPAD_DEFAULT({});
    f32 hori_speed AURORA_KPAD_DEFAULT(0.0F);
    f32 dist AURORA_KPAD_DEFAULT(0.0F);
    f32 dist_vec AURORA_KPAD_DEFAULT(0.0F);
    f32 dist_speed AURORA_KPAD_DEFAULT(0.0F);
    KPADVec2 acc_vertical AURORA_KPAD_DEFAULT({});
    u8 dev_type AURORA_KPAD_DEFAULT(WPAD_DEV_NOT_FOUND);
    s8 wpad_err AURORA_KPAD_DEFAULT(WPAD_ERR_NO_CONTROLLER);
    s8 dpd_valid_fg AURORA_KPAD_DEFAULT(0);
    u8 data_format AURORA_KPAD_DEFAULT(WPAD_FMT_CORE);
    KPADEXStatus ex_status AURORA_KPAD_DEFAULT({});
} KPADStatus;

#undef AURORA_KPAD_DEFAULT

typedef struct WPADInfo {
    BOOL dpd, speaker, attach, lowBat, nearempty;
    u8 battery, led, protocol, firmware;
} WPADInfo;

typedef void (*WPADCallback)(s32, s32);
typedef WPADCallback WPADConnectCallback;
typedef WPADCallback WPADExtensionCallback;
typedef void* (*WPADAlloc)(u32);
typedef u8 (*WPADFree)(void*);

#ifdef __cplusplus
extern "C" {
#endif
void KPADInit(void);
void KPADReset(void);
void KPADSetBtnRepeat(s32 channel, f32 delay, f32 pulse);
void KPADSetSensorHeight(s32 channel, f32 height);
void KPADSetPosParam(s32 channel, f32 radius, f32 sensitivity);
void KPADSetHoriParam(s32 channel, f32 radius, f32 sensitivity);
void KPADSetDistParam(s32 channel, f32 radius, f32 sensitivity);
void KPADSetAccParam(s32 channel, f32 radius, f32 sensitivity);
s32 KPADRead(s32 channel, KPADStatus sampling_bufs[], u32 length);
s32 WPADProbe(s32 channel, u32* type);
void WPADDisconnect(s32 channel);
void WPADEnableURCC(BOOL enable);
void WPADSetDataFormat(s32 channel, s32 format);
void WPADSetVRes(s32 channel, u32 xres, u32 yres);
void WPADSetAutoSamplingBuf(s32 channel, void* buffer, u32 length);
void WPADControlMotor(s32 channel, u32 command);
BOOL WPADSupportsRumble(s32 channel);
s32 WPADControlSpeaker(s32 channel, u32 command, WPADCallback callback);
BOOL WPADCanSendStreamData(s32 channel);
s32 WPADSendStreamData(s32 channel, void* data, u16 size);
BOOL WPADIsSpeakerEnabled(s32 channel);
u8 WPADGetSpeakerVolume(void);
void WPADStartFastSimpleSync(void);
void WPADStopSimpleSync(void);
WPADConnectCallback WPADSetConnectCallback(s32 channel, WPADConnectCallback callback);
WPADExtensionCallback WPADSetExtensionCallback(s32 channel, WPADExtensionCallback callback);
s32 WPADGetInfoAsync(s32 channel, WPADInfo* info, WPADCallback callback);
void WPADRegisterAllocator(WPADAlloc alloc, WPADFree free);
u32 WPADGetWorkMemorySize(void);
u8 WPADGetSensorBarPosition(void);
void WPADSetAutoSleepTime(u8 minutes);
#ifdef __cplusplus
}
#endif
