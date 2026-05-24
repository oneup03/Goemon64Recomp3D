#ifndef __TRANSFORM_IDS_H__
#define __TRANSFORM_IDS_H__

#include "PR/ultratypes.h"

// Stereoscopic 3D projection transform IDs. These are tagged onto the
// extended-GBI matrix group commands when the game builds the projection
// matrix for each scene type, so RT64's projection processor can decide
// which projections receive per-eye stereo offsets:
//   - GAMEPLAY: world perspective. Receives both off-axis projection shift
//     AND camera-space view shift (depth-dependent parallax).
//   - SKYBOX:  off-axis projection shift only. With no view shift the skybox
//     sits at infinity (maximum positive parallax), behind everything.
//   - HUD:     constant per-eye shift to the user-selected HUD depth.
//
// Numeric values intentionally match BanjoRecomp3D so the RT64-side
// isStereoProjectionId / isStereoHudProjectionId helpers don't need to be
// per-game.
#define PROJECTION_GAMEPLAY_TRANSFORM_ID    0x00001000
#define PROJECTION_SKYBOX_TRANSFORM_ID      0x00001001
#define PROJECTION_HUD_TRANSFORM_ID         0x00001004

// Set by the projection-tagging patches right before the game builds a
// projection matrix; read by RT64 as the projection group's matrixId via
// the extended GBI matrix-group commands.
extern s32 cur_perspective_projection_transform_id;
extern s32 cur_ortho_projection_transform_id;
void reset_projection_ids(void);

#endif
