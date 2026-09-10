#pragma once

#include <revolution/types.h>

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

using _GXColor = GXColor;
using _GXTexFmt = GXTexFmt;
using _GXTlut = GXTlut;
using _GXTlutFmt = GXTlutFmt;
using _GXTexMapID = GXTexMapID;
using _GXTexWrapMode = GXTexWrapMode;
using _GXTexFilter = GXTexFilter;
using _GXAnisotropy = GXAnisotropy;
using _GXRenderModeObj = GXRenderModeObj;
using _GXFBClamp = GXFBClamp;
using _GXGamma = GXGamma;
using _GXLightID = GXLightID;

constexpr s32 WPAD_CHAN0 = 0;
constexpr s32 WPAD_CHAN1 = 1;
constexpr s32 WPAD_CHAN2 = 2;
constexpr s32 WPAD_CHAN3 = 3;
constexpr s32 WPAD_MAX_CONTROLLERS = 4;
constexpr u32 WPAD_DEV_CORE = 0;
constexpr u32 WPAD_DEV_FREESTYLE = 1;
constexpr u32 WPAD_DEV_CLASSIC = 2;
constexpr u32 WPAD_DEV_NOT_FOUND = 253;
constexpr u32 WPAD_DEV_UNKNOWN = 255;
constexpr u32 WPAD_FMT_CORE = 0;
constexpr u32 WPAD_FMT_CORE_ACC = 1;
constexpr u32 WPAD_FMT_CORE_ACC_DPD = 2;
constexpr u32 WPAD_FMT_FREESTYLE = 3;
constexpr u32 WPAD_FMT_FREESTYLE_ACC = 4;
constexpr u32 WPAD_FMT_FREESTYLE_ACC_DPD = 5;
constexpr u32 WPAD_FMT_CLASSIC = 6;
constexpr u32 WPAD_FMT_CLASSIC_ACC = 7;
constexpr u32 WPAD_FMT_CLASSIC_ACC_DPD = 8;
constexpr s32 WPAD_ERR_BUSY = -2;
constexpr s32 WPAD_ERR_TRANSFER = -3;

constexpr u32 WPAD_MOTOR_STOP = 0;
constexpr u32 WPAD_MOTOR_RUMBLE = 1;

constexpr u32 WPAD_BUTTON_LEFT = 0x0001;
constexpr u32 WPAD_BUTTON_RIGHT = 0x0002;
constexpr u32 WPAD_BUTTON_DOWN = 0x0004;
constexpr u32 WPAD_BUTTON_UP = 0x0008;
constexpr u32 WPAD_BUTTON_PLUS = 0x0010;
constexpr u32 WPAD_BUTTON_2 = 0x0100;
constexpr u32 WPAD_BUTTON_1 = 0x0200;
constexpr u32 WPAD_BUTTON_B = 0x0400;
constexpr u32 WPAD_BUTTON_A = 0x0800;
constexpr u32 WPAD_BUTTON_MINUS = 0x1000;
constexpr u32 WPAD_BUTTON_Z = 0x2000;
constexpr u32 WPAD_BUTTON_C = 0x4000;
constexpr u32 WPAD_BUTTON_HOME = 0x8000;
constexpr u32 KPAD_BUTTON_MASK = 0x0000ffff;
constexpr u32 KPAD_BUTTON_RPT = 0x80000000;

constexpr s32 WPAD_ERR_NONE = 0;
constexpr s32 WPAD_ERR_NO_CONTROLLER = -1;

constexpr s32 NAND_RESULT_OK = 0;
constexpr s32 NAND_RESULT_ACCESS = -1;
constexpr s32 NAND_RESULT_ALLOC_FAILED = -2;
constexpr s32 NAND_RESULT_BUSY = -3;
constexpr s32 NAND_RESULT_CORRUPT = -4;
constexpr s32 NAND_RESULT_ECC_CRIT = -5;
constexpr s32 NAND_RESULT_EXISTS = -6;
constexpr s32 NAND_RESULT_INVALID = -8;
constexpr s32 NAND_RESULT_MAXBLOCKS = -9;
constexpr s32 NAND_RESULT_MAXFD = -10;
constexpr s32 NAND_RESULT_MAXFILES = -11;
constexpr s32 NAND_RESULT_NOEXISTS = -12;
constexpr s32 NAND_RESULT_NOTEMPTY = -13;
constexpr s32 NAND_RESULT_OPENFD = -14;
constexpr s32 NAND_RESULT_AUTHENTICATION = -15;
constexpr s32 NAND_RESULT_MAXDEPTH = -16;
constexpr s32 NAND_RESULT_UNKNOWN = -64;
constexpr s32 NAND_RESULT_FATAL_ERROR = -128;
constexpr u32 NAND_MAX_PATH = 64U;

struct KPADVec2 {
    f32 x;
    f32 y;
};

struct KPADVec3 {
    f32 x;
    f32 y;
    f32 z;
};

union KPADEXStatus {
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
};

struct KPADStatus {
    u32 hold = 0U;
    u32 trig = 0U;
    u32 release = 0U;
    KPADVec3 acc {};
    f32 acc_value = 0.0F;
    f32 acc_speed = 0.0F;
    KPADVec2 pos {};
    KPADVec2 vec {};
    f32 speed = 0.0F;
    KPADVec2 horizon {};
    KPADVec2 hori_vec {};
    f32 hori_speed = 0.0F;
    f32 dist = 0.0F;
    f32 dist_vec = 0.0F;
    f32 dist_speed = 0.0F;
    KPADVec2 acc_vertical {};
    u8 dev_type = WPAD_DEV_NOT_FOUND;
    s8 wpad_err = WPAD_ERR_NO_CONTROLLER;
    s8 dpd_valid_fg = 0;
    u8 data_format = WPAD_FMT_CORE;
    KPADEXStatus ex_status {};
};

extern "C" {
void KPADSetBtnRepeat(s32 channel, f32 delay, f32 pulse);
void KPADSetSensorHeight(s32 channel, f32 height);
void KPADSetPosParam(s32 channel, f32 radius, f32 sensitivity);
void KPADSetHoriParam(s32 channel, f32 radius, f32 sensitivity);
void KPADSetDistParam(s32 channel, f32 radius, f32 sensitivity);
void KPADSetAccParam(s32 channel, f32 radius, f32 sensitivity);
s32 KPADRead(s32 channel, KPADStatus sampling_bufs[], u32 length);
s32 WPADProbe(s32 channel, u32 *type);
void WPADDisconnect(s32 channel);
void WPADEnableURCC(BOOL enable);
void WPADSetDataFormat(s32 channel, s32 format);
void WPADSetVRes(s32 channel, u32 xres, u32 yres);
void WPADSetAutoSamplingBuf(s32 channel, void *buffer, u32 length);
void WPADControlMotor(s32 channel, u32 command);
BOOL WPADSupportsRumble(s32 channel);
void WPADControlSpeaker(s32 channel, s32 command, void *callback);
void WPADStartFastSimpleSync(void);
void WPADStopSimpleSync(void);
void WPADSetConnectCallback(s32 channel, void *callback);
void WPADSetExtensionCallback(s32 channel, void *callback);
}
