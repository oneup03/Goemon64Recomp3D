#ifndef __SKY_SCROLL_H__
#define __SKY_SCROLL_H__

#include "patches.h"

// The world camera as last handed to the projection setup (func_80017D8C_1898C,
// via func_80016C44_17844's camera case), captured for the 3D sky in
// background.c.
typedef struct SkyView {
    Vec3f position;
    Vec3f look_at;
    // Camera::unknown_1a: half the vertical field of view in 16-bit binary
    // angle units. func_80017D8C_1898C hands guPerspective
    // fovy = unknown_1a * 2 * 180 / 32768 degrees.
    u16 half_fovy;
    // func_80017D8C_1898C takes the guFrustum path instead when bit 0 of the
    // camera's byte 0x3E is set, and half_fovy means nothing there.
    u8 perspective;
    // g_game_step when this was captured. Backgrounds draw after the camera in
    // the same step, so a mismatch means the capture is stale.
    u32 step;
} SkyView;

extern SkyView g_sky_view;

// Incremented once per game step in func_800012FC_1EFC.
extern u32 g_game_step;

#endif
