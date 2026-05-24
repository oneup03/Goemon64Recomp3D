#ifndef __PATCH_GRAPHICS_H__
#define __PATCH_GRAPHICS_H__

#include "patch_helpers.h"

DECLARE_FUNC(void, recomp_get_window_resolution, u32*, u32*);
DECLARE_FUNC(float, recomp_get_target_aspect_ratio, float);
DECLARE_FUNC(s32, recomp_get_target_framerate, s32);
DECLARE_FUNC(s32, recomp_high_precision_fb_enabled);
DECLARE_FUNC(float, recomp_get_resolution_scale);

// Per-frame signal from the game to indicate that the current scene is
// low-convergence (FMV, file select, top-down minigame, first-person view).
// The renderer's auto-convergence path multiplies the convergence slider by
// the user's scale when this is set. Argument: 0 = normal, non-zero = low.
DECLARE_FUNC(void, recomp_stereo_set_low_convergence_scene, s32 active);

#endif
