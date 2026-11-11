#include "patches.h"
#include "patch_helpers.h"
#include "variables.h"

#ifdef GOEMON_3DS
#include "rt64_3ds_gbi.h"

DECLARE_FUNC(s32, recomp_get_hud_on_bottom);

// @recomp The touch-screen HUD's backdrop: the pause screen's blue diamonds.
// They are part of its 288x208 RGBA16 picture (the scroll, the frames and the
// map's window on the blue), file 0x868D, which the pause screen decodes with
// func_800144E8_150E8 into the room past its dungeon maps' data. Decoded the same
// way the first time it is drawn, it gives a 16x16 repeat of the diamonds,
// left of the title's frame and above the scroll, and the picture's darkening
// down the screen (its blue, on the left edge), which this draws over the
// touch screen in bands, under the bar and the map (hud_screen.c draws it
// with the frame's first layer, so that on the top screen, where the marked
// spans go while the settings menu is open, the scene covers it).

#define FILE_PAUSE_PICTURE  0x868D      // the pause screen's file list: 0x868C is the maps' data, 0x868E on its small pictures
#define PICTURE_WIDTH       288
#define PICTURE_HEIGHT      208
#define PICTURE_BYTES       (PICTURE_WIDTH * PICTURE_HEIGHT * 2)
#define PICTURE_FILE_SIZE   0xA000      // the file as read (its PIC data, ~0x70A0), before it is decoded
#define TILE_X              48          // the repeat: x 0..63 of rows 0..15 repeat every 16
#define TILE_Y              0
#define TILE_SIZE           16
#define SHADE_X             5           // the left edge's columns, blue all the way down
#define BAND_HEIGHT         8
#define BANDS               (SCREEN_HEIGHT / BAND_HEIGHT)

extern u8 *func_800144E8_150E8(u32 file_id, u8 *scratch, u8 *out);
extern u8 *dungeon_maps_picture_room(u32 *size);

static u8 picture_file[PICTURE_FILE_SIZE] __attribute__((aligned(16)));
static u16 tile[TILE_SIZE * TILE_SIZE] __attribute__((aligned(8)));
static u8 band_level[BANDS];
static s32 backdrop_state;      // 0: not decoded, 1: decoded, -1: unusable

static s32 backdrop_load(void) {
    const u16 *picture;
    const u8 *end;
    u32 room;
    s32 x, y, i, blue;

    if (backdrop_state != 0) {
        return backdrop_state > 0;
    }
    backdrop_state = -1;
    picture = (const u16 *)dungeon_maps_picture_room(&room);
    if (room < PICTURE_BYTES) {
        return FALSE;
    }
    // The decoder returns the end of what it wrote (the pixels start where
    // they are written, as the pause screen draws them); the tile is checked
    // to be the blue diamonds, blue over red in every texel.
    end = func_800144E8_150E8(FILE_PAUSE_PICTURE, picture_file, (u8 *)picture);
    blue = 0;
    for (y = 0; y < TILE_SIZE; y++) {
        for (x = 0; x < TILE_SIZE; x++) {
            u16 texel = picture[(TILE_Y + y) * PICTURE_WIDTH + TILE_X + x];
            tile[y * TILE_SIZE + x] = texel;
            blue += ((texel >> 1) & 0x1F) > ((texel >> 11) & 0x1F) + 8;
        }
    }
    if ((end < (u8 *)picture + PICTURE_BYTES) || (end > (u8 *)picture + room) || (blue != TILE_SIZE * TILE_SIZE)) {
        recomp_printf("HUD backdrop: file 0x%X is not the picture expected (%d bytes, %d of %d texels blue)\n",
                      FILE_PAUSE_PICTURE, (s32)(end - (u8 *)picture), blue, TILE_SIZE * TILE_SIZE);
        return FALSE;
    }
    // Each band's brightness: the picture's blue at the band's height, scaled
    // from the touch screen's 240 lines to the picture's 208, over the tile's
    // (full) blue.
    for (i = 0; i < BANDS; i++) {
        s32 row = ((i * BAND_HEIGHT + BAND_HEIGHT / 2) * PICTURE_HEIGHT) / SCREEN_HEIGHT;
        s32 blue = 0;
        for (x = 0; x < SHADE_X; x++) {
            blue += (picture[row * PICTURE_WIDTH + x] >> 1) & 0x1F;
        }
        band_level[i] = (u8)((blue * 0xFF) / (SHADE_X * 0x1F));
    }
    backdrop_state = 1;
    return TRUE;
}

// From the HUD layer's draw, before the bar: the backdrop in a touch-screen
// span, every state it changes put back. True when it drew.
s32 hud_backdrop_draw(void) {
    Gfx *gfx;
    s32 i;

    if (!recomp_get_hud_on_bottom() || !backdrop_load()) {
        return FALSE;
    }
    gfx = D_8015C5CC_15D1CC;
    gEX3DSBottomScreen(gfx++, 0, 0);
    gDPPipeSync(gfx++);
    gEXPushScissor(gfx++);
    gEXPushOtherMode(gfx++);
    gEXPushCombineMode(gfx++);
    gEXPushPrimColor(gfx++);
    gDPSetScissor(gfx++, G_SC_NON_INTERLACE, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    gDPSetCycleType(gfx++, G_CYC_1CYCLE);
    gDPSetRenderMode(gfx++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
    gDPSetAlphaCompare(gfx++, G_AC_NONE);
    gDPSetTexturePersp(gfx++, G_TP_NONE);
    gDPSetTextureLUT(gfx++, G_TT_NONE);
    gDPSetTextureFilter(gfx++, G_TF_POINT);
    gDPSetCombineLERP(gfx++, TEXEL0, 0, PRIMITIVE, 0, 0, 0, 0, 1, TEXEL0, 0, PRIMITIVE, 0, 0, 0, 0, 1);
    gDPLoadTextureBlock(gfx++, tile, G_IM_FMT_RGBA, G_IM_SIZ_16b, TILE_SIZE, TILE_SIZE, 0,
                        G_TX_WRAP | G_TX_NOMIRROR, G_TX_WRAP | G_TX_NOMIRROR, 4, 4, G_TX_NOLOD, G_TX_NOLOD);
    for (i = 0; i < BANDS; i++) {
        s32 y = i * BAND_HEIGHT;
        gDPSetPrimColor(gfx++, 0, 0, band_level[i], band_level[i], band_level[i], 0xFF);
        gSPTextureRectangle(gfx++, 0, y << 2, SCREEN_WIDTH << 2, (y + BAND_HEIGHT) << 2, G_TX_RENDERTILE,
                            0, y << 5, 1 << 10, 1 << 10);
    }
    gDPPipeSync(gfx++);
    gEXPopPrimColor(gfx++);
    gEXPopCombineMode(gfx++);
    gEXPopOtherMode(gfx++);
    gEXPopScissor(gfx++);
    gEX3DSTopScreen(gfx++);
    D_8015C5CC_15D1CC = gfx;
    return TRUE;
}

#endif
