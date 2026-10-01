#include "patches.h"
#include "ui_funcs.h"
#include "sky_scroll.h"

u32 g_game_step = 0;
SkyView g_sky_view;

// @recomp Patched to enable RT64's extended GBI mode and set the correct refresh rate.
//
// NOTE on stereoscopic 3D: setting cur_perspective_projection_transform_id /
// cur_ortho_projection_transform_id here would do nothing — those globals are
// only consulted by a recompilation patch that explicitly emits
// gEXMatrixGroup() in the gfx command stream, the way BanjoRecomp3D's
// viewport_setRenderPerspectiveMatrix patch does. Goemon's perspective-setup
// function (func_80017D8C_1898C) is raw recompiled MIPS rather than decompiled
// C, so there's no clean place to insert that emission. As a fallback, RT64's
// projection processor treats untagged perspectives (matrixId == G_EX_ID_AUTO)
// as world projections — see isStereoProjectionId in rt64_projection_processor.cpp.
RECOMP_PATCH void func_800012FC_1EFC()
{
    u8 retraces_per_game_step = D_8008CCC0_8D8C0.retraces_per_game_step;

#if 0
    if ((D_8008CCC0_8D8C0.controller[0].button_held_down & L_TRIG) && (D_8008CCC0_8D8C0.controller[0].button_held_down & Z_TRIG) && (D_8008CCC0_8D8C0.controller[0].button_held_down & R_JPAD)) {
        D_8008CCC0_8D8C0.stepw = 10;
    }
#endif

    // @recomp Run Ui Callbacks
    recomp_run_ui_callbacks();

    g_game_step++;

    gEXEnable(D_8015C5CC_15D1CC++);
    // gEXSetRDRAMExtended(D_8015C5CC_15D1CC++, 1);

    if (retraces_per_game_step != 0) {
        gEXSetRefreshRate(D_8015C5CC_15D1CC++, 60 / retraces_per_game_step);
    }

    // Rely on automatic tagging for display lists which are not covered by the draw list system (only Konami Logo so far?).
    // gEXMatrixGroupDecomposedNormal(D_8015C5CC_15D1CC++, G_EX_PUSH, 0, G_EX_EDIT_ALLOW);

    gSPDisplayList(D_8015C5CC_15D1CC++, &D_8006D4E0_6E0E0);
}
