#include "patches.h"
#include "transform_ids.h"

// Globals defined here; the projection-tagging patches in
// projection_transform_tagging.c / sky_transform_tagging.c set these right
// before the game builds the corresponding projection matrix. RT64 reads
// them as the projection group's matrixId.
s32 cur_perspective_projection_transform_id = 0;
s32 cur_ortho_projection_transform_id = 0;

void reset_projection_ids(void) {
    cur_perspective_projection_transform_id = 0;
    cur_ortho_projection_transform_id = 0;
}
