#ifndef __SKY_DEBUG_H__
#define __SKY_DEBUG_H__

#include "patches.h"

// Temporary instrumentation for the sky scroll investigation: the 2D
// background's scroll (BackgroundGraphicsNode start_x/start_y) is computed by
// per-scene game code that has not been located, so its formula is being
// measured instead - logged against the live camera every few frames, then
// fitted. Set to 0 (or delete this header and its uses) once the scroll is
// replaced.
#define SKY_SCROLL_DEBUG 1

#if SKY_SCROLL_DEBUG
int recomp_printf(const char *fmt, ...);

// Incremented once per game step in func_800012FC_1EFC.
extern u32 g_sky_debug_frame;
// Copy of the last camera passed to the world projection setup, and the
// pointer it came from so separate cameras can be told apart in the log.
extern Camera g_sky_debug_camera;
extern u32 g_sky_debug_camera_addr;
extern u32 g_sky_debug_camera_frame;
// Calls since the last heartbeat, so a log with no SKY lines still says
// whether the background dispatcher or the camera capture ever ran.
extern u32 g_sky_debug_bg_calls;
extern u32 g_sky_debug_cam_calls;
#endif

#endif
