#include "patches.h"
#include "graphics.h"

static const f32 g_original_aspect_ratio = (f32)SCREEN_WIDTH / (f32)SCREEN_HEIGHT;

static f32 g_spinning_sun_previous_aspect_ratio = 0.0f;

// @recomp Patched to accomodate the spinning sun effect for widescreen.
RECOMP_PATCH void func_80214530_69BF30(SpinningSunTask *task, Object *object) 
{
    f32 current_aspect_ratio = recomp_get_target_aspect_ratio(g_original_aspect_ratio);

    if ((current_aspect_ratio != g_spinning_sun_previous_aspect_ratio || task->initialized == FALSE) && current_aspect_ratio != 0.0f) {
        object->scale.x = 1.0f * (current_aspect_ratio / g_original_aspect_ratio);
        object->scale.z = 1.0f * (current_aspect_ratio / g_original_aspect_ratio);

        g_spinning_sun_previous_aspect_ratio = current_aspect_ratio;
        task->initialized = TRUE;
    }

    object->rotation.z -= 2;
    object->rotation.z &= 0x3FF;
}

/*
extern Object *func_800119D4_125D4(Task *task, u8 draw_layer);
extern void *func_80014840_15440(void *virtual_address, u32 file_id);
extern void func_80024160_24D60(RipplingBackgroundTask *task, Object *object);
extern RipplingBackground *func_80024670_25270();
extern void func_80035020_35C20();
extern void func_801CC710_65F5C0(RipplingHikimakuTask *task, Object *object);

extern f32 D_801D42E8_667198;
extern f32 D_801D42EC_66719C;
extern f32 D_801D42F0_6671A0;
extern RipplingHikimakuTask *D_801E8704_67B5B4;

RECOMP_PATCH void func_801CC978_65F828(RipplingHikimakuTask *task, Object *object) {
    RipplingBackground *rippling_background = func_80024670_25270();
    Object *child_object; 

    if (rippling_background == NULL) {
        func_80035020_35C20();
        return;
    }

    D_801E8704_67B5B4 = task;

    task->rippling_background = rippling_background;

    rippling_background->unknown_4 = 15;
    rippling_background->unknown_8 = 15;
    rippling_background->unknown_c = 2000;
    rippling_background->unknown_10 = 2000;
    rippling_background->unknown_0 = 3587;
    rippling_background->unknown_30 = func_80014840_15440((void *)(0x08001D10), 1229);
    rippling_background->unknown_38 = 5.0f;
    rippling_background->unknown_14 = D_801D42E8_667198;
    rippling_background->unknown_18 = D_801D42EC_66719C;

    child_object = func_800119D4_125D4((Task *)task, 9);
    child_object->rotation.x = 0x100;
    child_object->rotation.y = 0;
    child_object->rotation.z = 0;
    child_object->heap_element.flags = 3;
    child_object->scale.x = D_801D42F0_6671A0;
    child_object->scale.y = D_801D42F0_6671A0;
    child_object->scale.z = D_801D42F0_6671A0;
    child_object->position.x = 10.0f;
    child_object->position.y = 0.0f;
    child_object->position.z = -300.0f;

    task->unknown_8a = 0;
    // task->unknown_8b = 0;

    rippling_background->unknown_1c = 0;
    rippling_background->displacement_map_function = (void (*)(Task *, Object *))&func_801CC710_65F5C0;

    func_8003521C_35E1C((void (*)(Task *, Object *))&func_80024160_24D60);
}
*/

static f32 g_rippling_hikimaku_previous_aspect_ratio = 0.0f;

// @recomp Patched to accomodate the rippling hikimaku background for widescreen.
RECOMP_PATCH void func_801CC710_65F5C0(RipplingHikimakuTask *task, Object *object) 
{
    f32 current_aspect_ratio = recomp_get_target_aspect_ratio(g_original_aspect_ratio);
    RipplingBackground *rippling_background = task->rippling_background;
    s32 row, column;
    s32 grid_width = rippling_background->unknown_4 + 2;
    s32 grid_height = rippling_background->unknown_8 + 2;
    u16 game_loops_ran;
    f64 phase_1;
    f32 wave_x;
    f64 phase_2;
    f32 wave_y;

    if ((current_aspect_ratio != g_rippling_hikimaku_previous_aspect_ratio || task->initialized == FALSE) && current_aspect_ratio != 0.0f) {
        object->scale.x = 0.2f * (current_aspect_ratio / g_original_aspect_ratio);
        object->scale.z = 0.2f * (current_aspect_ratio / g_original_aspect_ratio);

        g_rippling_hikimaku_previous_aspect_ratio = current_aspect_ratio;
        task->initialized = TRUE;
    }

    TAGGING_OBJECT_SET_INTERPOLATE_VERTICES(object);

    for (row = 0; row < grid_height; row++) {
        for (column = 0; column < grid_width; column++) {
            game_loops_ran = D_8015C5C8_15D1C8->game_loops_count;

            phase_1 = (game_loops_ran * D_801D42E0_667190 * 2.0) + (column * 5.0) + (rippling_background->unknown_8 - row) * 5.0 + 5.0;
            wave_x = func_80003E10_4A10((s32)(phase_1 * 9.0) & 0x3FF);

            phase_2 = (game_loops_ran * D_801D42E0_667190 * 2.0) + (column * 8.0) + (rippling_background->unknown_8 - row) * 8.0 + 8.0;
            wave_y = func_80003E10_4A10((s32)(phase_2 * 14.0) & 0x3FF);

            rippling_background->unknown_58[row * grid_width + column] = (wave_x + wave_y) * 200.0f;
        }
    }
}

static f32 g_rippling_karakusa_previous_aspect_ratio = 0.0f;

// @recomp Patched to accomodate the rippling karakusa background for widescreen.
RECOMP_PATCH void func_80213AC8_672A78(RipplingKarakusaTask *task, Object *object) 
{
    f32 current_aspect_ratio = recomp_get_target_aspect_ratio(g_original_aspect_ratio);
    RipplingBackground *rippling_background = task->rippling_background;
    s32 row, column;
    s32 grid_width = rippling_background->unknown_4 + 2;
    s32 grid_height = rippling_background->unknown_8 + 2;
    u16 game_loops_ran;
    f64 phase_1;
    f32 wave_x;
    f64 phase_2;
    f32 wave_y;
    
    if ((current_aspect_ratio != g_rippling_hikimaku_previous_aspect_ratio || task->initialized == FALSE) && current_aspect_ratio != 0.0f) {
        object->scale.x = 0.2f * (current_aspect_ratio / g_original_aspect_ratio);
        object->scale.z = 0.2f * (current_aspect_ratio / g_original_aspect_ratio);

        g_rippling_hikimaku_previous_aspect_ratio = current_aspect_ratio;
        task->initialized = TRUE;
    }

    TAGGING_OBJECT_SET_INTERPOLATE_VERTICES(object);

    for (row = 0; row < grid_height; row++) {
        for (column = 0; column < grid_width; column++) {
            game_loops_ran = D_8015C5C8_15D1C8->game_loops_count;

            phase_1 = (game_loops_ran * D_80217938_6768E8 * 2.0) + (column * 5.0) + (rippling_background->unknown_8 - row) * 5.0 + 5.0;
            wave_x = func_80003E10_4A10((s32)(phase_1 * 9.0) & 0x3FF);

            phase_2 = (game_loops_ran * D_80217938_6768E8 * 2.0) + (column * 8.0) + (rippling_background->unknown_8 - row) * 8.0 + 8.0;
            wave_y = func_80003E10_4A10((s32)(phase_2 * 14.0) & 0x3FF);

            rippling_background->unknown_58[row * grid_width + column] = (wave_x + wave_y) * 200.0f;
        }
    }
}
// The pause menu's pictures: file 17 draws them in tiles with
// func_802145F4_6735A4.
typedef struct MenuPicture {
    u16 width;
    u16 height;
    u32 unknown_4;
    void *address;      // in its file (segmented)
    u16 file_id;
    u16 unknown_e;
} MenuPicture;

extern MenuPicture D_80167C78_168878[];
extern void *func_80014840_15440(void *virtual_address, u32 file_id);

// The map screen's backdrop is a 288x208 picture at (20, 16): the floor list's
// hanging scroll and the map's scroll on the blue pattern, which stops at the
// picture's edges and leaves black around it (the TV's overscan on the N64, and
// both sides of a wide screen). The pattern repeats every 16 pixels and is
// symmetric about the middle of each period, so the picture's first 8 columns,
// mirrored, continue it with the picture's own shading from top to bottom.
#define MAP_PICTURE_X           20
#define MAP_PICTURE_Y           16
#define MAP_PICTURE_WIDTH       288
#define MAP_PICTURE_HEIGHT      208
#define MAP_PATTERN_PERIOD      16
#define MAP_PATTERN_COLUMNS     8
// Rows 148..153 of those columns hold the end of the hanging scroll's rod: the
// pattern there is taken a period higher.
#define MAP_ROD_ROW             148
#define MAP_ROD_ROWS            6

// Loads rows source_row.. of the pattern columns, mirrored across the tile's
// 8 texels (a 16-texel period).
static Gfx *map_pattern_band(Gfx *gfx, u16 *pixels, s32 source_row, s32 rows) {
    gDPPipeSync(gfx++);
    gDPSetTextureImage(gfx++, G_IM_FMT_RGBA, G_IM_SIZ_16b, MAP_PICTURE_WIDTH, pixels + source_row * MAP_PICTURE_WIDTH);
    gDPSetTile(gfx++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 2, 0, G_TX_LOADTILE, 0, G_TX_CLAMP, 0, 0, G_TX_CLAMP, 0, 0);
    gDPLoadSync(gfx++);
    gDPLoadTile(gfx++, G_TX_LOADTILE, 0, 0, (MAP_PATTERN_COLUMNS - 1) << G_TEXTURE_IMAGE_FRAC, (rows - 1) << G_TEXTURE_IMAGE_FRAC);
    gDPPipeSync(gfx++);
    gDPSetTile(gfx++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 2, 0, G_TX_RENDERTILE, 0, G_TX_CLAMP, 0, 0, G_TX_MIRROR | G_TX_WRAP, 3, 0);
    gDPSetTileSize(gfx++, G_TX_RENDERTILE, 0, 0, (MAP_PATTERN_COLUMNS - 1) << G_TEXTURE_IMAGE_FRAC, (rows - 1) << G_TEXTURE_IMAGE_FRAC);
    return gfx;
}

// A copy-mode rectangle of the loaded band over x0..x1 and y0..y1 (ends
// exclusive) from band row t, in step with the picture's columns. An extended
// rectangle, placed from the screen's centre (the N64's x = 160 on any width):
// it takes coordinates past the N64's screen, and a row of rectangles with
// plain origins that spans the width would be stretched over the wide screen
// as one backdrop.
static Gfx *map_pattern_rect(Gfx *gfx, s32 x0, s32 x1, s32 y0, s32 y1, s32 t) {
    const s32 s = (((x0 - MAP_PICTURE_X) % MAP_PATTERN_PERIOD) + MAP_PATTERN_PERIOD) % MAP_PATTERN_PERIOD;
    const s32 centre = SCREEN_WIDTH / 2;
    gEXTextureRectangle(gfx++, G_EX_ORIGIN_CENTER, G_EX_ORIGIN_CENTER, (x0 - centre) << 2, y0 << 2, (x1 - 1 - centre) << 2, (y1 - 1) << 2,
        G_TX_RENDERTILE, s << 5, t << 5, 4 << 10, 1 << 10);
    return gfx;
}

static Gfx *map_picture_margins(Gfx *gfx, u16 *pixels) {
    const f32 aspect = recomp_get_target_aspect_ratio(g_original_aspect_ratio);
    const s32 top = MAP_PICTURE_Y;
    const s32 bottom = MAP_PICTURE_Y + MAP_PICTURE_HEIGHT;
    const s32 rod_end = MAP_ROD_ROW + MAP_ROD_ROWS;
    s32 side = 0;
    s32 left, right;

    // Past the screen's edges on either side (the scissor ends the rectangles
    // there), starting a whole number of periods from the picture.
    if (aspect > g_original_aspect_ratio) {
        side = (s32)((SCREEN_HEIGHT * aspect - SCREEN_WIDTH) * 0.5f);
    }
    left = MAP_PICTURE_X - MAP_PATTERN_PERIOD * ((side + MAP_PICTURE_X) / MAP_PATTERN_PERIOD + 2);
    right = MAP_PICTURE_X + MAP_PICTURE_WIDTH + MAP_PATTERN_PERIOD * ((side + SCREEN_WIDTH - MAP_PICTURE_X - MAP_PICTURE_WIDTH) / MAP_PATTERN_PERIOD + 2);

    gEXSetScissor(gfx++, G_SC_NON_INTERLACE, G_EX_ORIGIN_LEFT, G_EX_ORIGIN_RIGHT, 0, 0, 0, SCREEN_HEIGHT);

    // Above the rod, and above the picture (whose screen rows 0..15 are its rows
    // -16..-1: its first period again).
    gfx = map_pattern_band(gfx, pixels, 0, MAP_ROD_ROW);
    gfx = map_pattern_rect(gfx, left, MAP_PICTURE_X, top, top + MAP_ROD_ROW, 0);
    gfx = map_pattern_rect(gfx, MAP_PICTURE_X + MAP_PICTURE_WIDTH, right, top, top + MAP_ROD_ROW, 0);
    gfx = map_pattern_rect(gfx, left, MAP_PICTURE_X, 0, top, 0);
    gfx = map_pattern_rect(gfx, MAP_PICTURE_X, right, 0, top, 0);

    // Beside the rod.
    gfx = map_pattern_band(gfx, pixels, MAP_ROD_ROW - MAP_PATTERN_PERIOD, MAP_ROD_ROWS);
    gfx = map_pattern_rect(gfx, left, MAP_PICTURE_X, top + MAP_ROD_ROW, top + rod_end, 0);
    gfx = map_pattern_rect(gfx, MAP_PICTURE_X + MAP_PICTURE_WIDTH, right, top + MAP_ROD_ROW, top + rod_end, 0);

    // Below it, and below the picture (its last period again).
    gfx = map_pattern_band(gfx, pixels, rod_end, MAP_PICTURE_HEIGHT - rod_end);
    gfx = map_pattern_rect(gfx, left, MAP_PICTURE_X, top + rod_end, bottom, 0);
    gfx = map_pattern_rect(gfx, MAP_PICTURE_X + MAP_PICTURE_WIDTH, right, top + rod_end, bottom, 0);
    gfx = map_pattern_rect(gfx, left, MAP_PICTURE_X, bottom, SCREEN_HEIGHT, MAP_PICTURE_HEIGHT - MAP_PATTERN_PERIOD - rod_end);
    gfx = map_pattern_rect(gfx, MAP_PICTURE_X, right, bottom, SCREEN_HEIGHT, MAP_PICTURE_HEIGHT - MAP_PATTERN_PERIOD - rod_end);

    // The scissor the picture is drawn under.
    gDPSetScissor(gfx++, G_SC_NON_INTERLACE, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    return gfx;
}

// @recomp Patched to draw the map screen's backdrop pattern past its picture's
// edges (above) before the picture's first tile. One tile of a picture: width x
// height texels from (s, t) drawn at (x, y) in copy mode.
RECOMP_PATCH Gfx *func_802145F4_6735A4(Gfx *gfx, s32 x, s32 y, s32 width, s32 height, s32 picture_index, s32 s, s32 t)
{
    MenuPicture *picture = &D_80167C78_168878[picture_index];
    u16 *pixels = (u16 *)func_80014840_15440(picture->address, picture->file_id);
    const s32 line = ((width * 2) + 7) >> 3;

    if ((s == 0) && (t == 0) && (x == MAP_PICTURE_X) && (y == MAP_PICTURE_Y) &&
        (picture->width == MAP_PICTURE_WIDTH) && (picture->height == MAP_PICTURE_HEIGHT) && (pixels != NULL)) {
        gfx = map_picture_margins(gfx, pixels);
    }

    gDPPipeSync(gfx++);
    gDPSetTextureImage(gfx++, G_IM_FMT_RGBA, G_IM_SIZ_16b, picture->width, pixels + (picture->width * t) + s);
    gDPSetTile(gfx++, G_IM_FMT_RGBA, G_IM_SIZ_16b, line, 0, G_TX_LOADTILE, 0, G_TX_CLAMP, 0, 0, G_TX_CLAMP, 0, 0);
    gDPLoadSync(gfx++);
    gDPLoadTile(gfx++, G_TX_LOADTILE, 0, 0, (width - 1) << G_TEXTURE_IMAGE_FRAC, (height - 1) << G_TEXTURE_IMAGE_FRAC);
    gDPPipeSync(gfx++);
    gDPSetTile(gfx++, G_IM_FMT_RGBA, G_IM_SIZ_16b, line, 0, G_TX_RENDERTILE, 0, G_TX_CLAMP, 0, 0, G_TX_CLAMP, 0, 0);
    gDPSetTileSize(gfx++, G_TX_RENDERTILE, 0, 0, (width - 1) << G_TEXTURE_IMAGE_FRAC, (height - 1) << G_TEXTURE_IMAGE_FRAC);
    gSPTextureRectangle(gfx++, x << 2, y << 2, (x + width - 1) << 2, (y + height - 1) << 2, G_TX_RENDERTILE, 0, 0, 4 << 10, 1 << 10);
    gSPEndDisplayList(gfx);
    return gfx;
}
