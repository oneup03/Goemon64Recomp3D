#include "patches.h"
#include "sky_scroll.h"
#include "graphics.h"
#include "transform_ids.h"

typedef struct graphics_node {
	struct graphics_node *next;
	u8 flags;
	u8 unk_8;
	s16 priority;
} GraphicsNode;

typedef struct background_graphics_node {
	GraphicsNode node;
	u8 unk_8[4];
	s16 overlay_file_id;
	u16 flags;
	s16 texture_width;
	s16 texture_height;
	f32 start_x;
	f32 start_y;
	f32 upper_left_corner_x;
	f32 upper_left_corner_y;
	f32 rectangle_width;
	f32 rectangle_height;
	u8 alpha;
	u8 unk_2d;
	s16 unk_2e;
	s16 unk_30;
	s16 unk_32;
	s16 unk_34;
	u8 unk_36;
	u8 unk_37;
	u8 *texture_data;
	u8 *palette_data;
	u16 bits_per_pixel;
} BackgroundGraphicsNode;

void func_80022A74_23674(BackgroundGraphicsNode *node);
void func_80022214_22E14(BackgroundGraphicsNode *node);
void func_80021894_22494(u8 *texture_data, u8 *palette_data, f32 start_x, f32 start_y);

extern s32 D_8006D158_6DD58;
extern s32 D_8006D15C_6DD5C;
extern s32 D_8006D160_6DD60; // g_background_width
extern s32 D_8006D164_6DD64; // g_background_height

// @recomp Math for the 3D sky below.

#define SKY_PI 3.14159265f

// atan on [-1, 1]; minimax, max error ~1e-5 rad. Patches have no libm.
static f32 sky_atan_unit(f32 x) {
    const f32 x2 = x * x;
    return x * (0.99997726f + x2 * (-0.33262347f + x2 * (0.19354346f + x2 * (-0.11643287f + x2 * (0.05265332f + x2 * -0.01172120f)))));
}

static f32 sky_atan2(f32 y, f32 x) {
    const f32 ax = (x < 0.0f) ? -x : x;
    const f32 ay = (y < 0.0f) ? -y : y;
    f32 a;
    if ((ax == 0.0f) && (ay == 0.0f)) {
        return 0.0f;
    }
    a = (ay <= ax) ? sky_atan_unit(ay / ax) : (SKY_PI * 0.5f - sky_atan_unit(ax / ay));
    if (x < 0.0f) {
        a = SKY_PI - a;
    }
    return (y < 0.0f) ? -a : a;
}

// Taylor series, accurate to ~1e-6 for |x| < pi/2 - a half FoV always is.
static f32 sky_sin(f32 x) {
    const f32 x2 = x * x;
    return x * (1.0f + x2 * (-1.0f / 6.0f + x2 * (1.0f / 120.0f + x2 * (-1.0f / 5040.0f + x2 * (1.0f / 362880.0f)))));
}

static f32 sky_cos(f32 x) {
    const f32 x2 = x * x;
    return 1.0f + x2 * (-0.5f + x2 * (1.0f / 24.0f + x2 * (-1.0f / 720.0f + x2 * (1.0f / 40320.0f + x2 * (-1.0f / 3628800.0f)))));
}

// v mod period, into [0, period).
static f32 sky_wrap(f32 v, f32 period) {
    s32 n = (s32)(v / period);
    v -= (f32)n * period;
    if (v < 0.0f) {
        v += period;
    }
    if (v >= period) {
        v -= period;
    }
    return v;
}

// v into [-half, half).
static f32 sky_wrap_signed(f32 v, f32 period) {
    return sky_wrap(v + period * 0.5f, period) - period * 0.5f;
}

// Which background the accumulated heading belongs to. The same node memory is
// recycled for unrelated backgrounds (the title logo and the field sky share a
// node address), so the pointer alone does not identify it.
static BackgroundGraphicsNode *s_sky_node = NULL;
static u8 *s_sky_texture = NULL;
static u16 s_sky_flags = 0;
static u32 s_sky_last_step = 0;

// @recomp The 3D sky.
//
// Goemon's yaw-following sky is a 2D panorama scrolled across a flat rect, and
// no flat image can sit at infinity in a perspective view: something infinitely
// far away at bearing theta from the view axis lands at NDC m00 * tan(theta), so
// it crosses the screen 1 / cos^2(theta) faster towards the edges and is
// magnified 1 / cos(theta) vertically there. A flat sky moves and scales the
// same everywhere, and against the world reads as turning with the camera and
// being squashed at the edges.
//
// So the panorama is drawn as what it represents: a textured cylinder around the
// camera, with the camera's rotation and none of its translation, under a
// projection tagged as the skybox (PROJECTION_SKYBOX_TRANSFORM_ID). RT64 then
// projects it like any 3D geometry, widens it for widescreen like the world,
// interpolates it between game frames like the world's camera, and gives it the
// skybox's stereo treatment - projection shear without the eye offset, i.e.
// exactly the disparity of infinity.
//
// It wraps the 640-texel panorama twice per turn: the art covers 360 degrees in
// 640 texels but only ~68 degrees in 240 rows, so a single wrap would stretch it
// ~1.7x wide, while two keep its texels near square. The cost is that the same
// sky appears in opposite directions.
//
// The cylinder is fixed to the world the moment the sky is recognised, from the
// game's own framing then (its centre texel, vertical scroll and the camera's
// pitch), so it starts exactly where the game draws it; after that every motion
// is the camera's, apart from the panorama's own drift (clouds), which it
// follows. A sky is recognised at once when it scrolls as Oedo Town's does,
// and otherwise once the camera has turned a little with the sky scrolling
// along (sky_track).

#define SKY_RADIUS              1000.0f
#define SKY_WRAPS_PER_TURN      2
#define SKY_COLUMN_TEXELS       32
#define SKY_BAND_ROWS           48
// Past the panorama's top and bottom rows the sky continues as caps, curving
// up to this elevation (and down to its negative), each stretching the edge
// row - so a camera that pitches up in a cutscene sees the top of the sky
// rather than nothing.
#define SKY_CAP_ELEVATION       (88.0f * SKY_PI / 180.0f)
// The top cap stretches the panorama's top row only this far up, fading from
// it to one sky colour (the top row's commonest): stretched all the way to the
// pole, the sun and the clouds that touch the top row drew white streaks
// across the dome.
#define SKY_CAP_FADE            (6.0f * SKY_PI / 180.0f)
// Pitch beyond which every column is drawn: looking steeply up or down, the
// columns behind the camera come into view.
#define SKY_CULL_PITCH_SIN      0.34f
#define SKY_VISIBLE_HALF_ANGLE  (85.0f * SKY_PI / 180.0f)
#define SKY_MAX_QUADS           480
// Bounds for the per-frame corner tables: column edges around a turn, and row
// boundaries down the panorama. sky_track rejects textures that exceed them.
#define SKY_MAX_EDGES           129
#define SKY_MAX_BANDS           32

// The cylinder, anchored by sky_track: the panorama texel at world bearing 0
// (unwrapped), texels per radian of bearing, and the vertical mapping - the
// game's vertical scroll, the tangent of the camera's pitch and cot(fovy / 2)
// when it was anchored.
static f32 s_sky_u_at_zero_bearing = 0.0f;
// The sky colour the top cap fades to (sky_measure_zenith), measured when the
// cylinder is anchored.
static u8 s_sky_zenith[3] = { 0, 0, 0 };
static int s_sky_zenith_valid = 0;
static f32 s_sky_texels_per_radian = 0.0f;
static f32 s_sky_anchor_sy = 0.0f;
static f32 s_sky_anchor_tan_pitch = 0.0f;
static f32 s_sky_anchor_cot = 1.0f;
// The camera heading at the call before, and its step: the game scrolls the
// flat sky from the camera as it stood a frame earlier than the one it draws
// with. Measured in Oedo Town with the camera turning: start_x matched the
// previous frame's heading within 0.33 texels, the current one only within
// 3.4 typically and 18 at worst, which flipped the sky between 3D and flat
// on every quick turn.
static f32 s_sky_prev_view_yaw = 0.0f;
static f32 s_sky_prev_view_tan_pitch = 0.0f;
static u32 s_sky_prev_view_step = 0;
static int s_sky_prev_view_valid = 0;
// The camera this step: heading, forward vector and cot(fovy / 2).
static f32 s_sky_yaw = 0.0f;
static f32 s_sky_forward[3] = { 0.0f, 0.0f, 1.0f };
static f32 s_sky_cot = 1.0f;

// A sky that follows the camera is scrolled as start_x = offset - heading *
// width / 360deg, so start_x plus the heading in texels (its offset) holds
// still while the camera turns. Each sky has its own: Oedo Town's is half the
// width, the sunset field's is not, and drifting clouds move it slowly. The
// tracked sky's offset last step, smoothed (the drift without the scroll's
// quantisation), and when the cylinder was anchored, with the anchor's texel.
static f32 s_sky_offset = 0.0f;
static f32 s_sky_offset_smooth = 0.0f;
static f32 s_sky_offset_anchor = 0.0f;
static f32 s_sky_u_anchor = 0.0f;
// A background being watched while the camera turns: its offset when the
// watch began and last step, and the heading turned since.
static BackgroundGraphicsNode *s_sky_candidate_node = NULL;
static u8 *s_sky_candidate_texture = NULL;
static u16 s_sky_candidate_flags = 0;
static u32 s_sky_candidate_step = 0;
static f32 s_sky_candidate_yaw = 0.0f;
static f32 s_sky_candidate_turn = 0.0f;
static f32 s_sky_candidate_start_x = 0.0f;
static f32 s_sky_candidate_scroll = 0.0f;
static u32 s_sky_candidate_calls = 0;
// The watched scroll's moves each way, for the still camera's drift, which
// must go one way (clouds) rather than back and forth (a heading's jitter).
static f32 s_sky_candidate_forward = 0.0f;
static f32 s_sky_candidate_back = 0.0f;
// Recognised by the still camera's drift or by Oedo Town's rule; and a
// background that was, then failed its rate check when the camera turned
// (one that scrolls by itself, or a static one that matched the rule by
// chance): not taken those ways again, or it would switch on and off.
static int s_sky_by_still = 0;
static int s_sky_by_fast = 0;
static BackgroundGraphicsNode *s_sky_still_ban_node = NULL;
static u8 *s_sky_still_ban_texture = NULL;
static u16 s_sky_still_ban_flags = 0;
// The tracked sky's scroll: texels per radian of heading. Oedo Town's is one
// width per turn, the Impact battles' two. A sky recognised by Oedo Town's
// rule at once has it confirmed over its first turn as a watched one has
// (any sky's scroll sweeps through that rule's offset as the camera turns),
// and its drift is followed only after.
static f32 s_sky_rate = 0.0f;
static int s_sky_rate_confirmed = 0;
static f32 s_sky_check_yaw = 0.0f;
static f32 s_sky_check_turn = 0.0f;
static f32 s_sky_check_start_x = 0.0f;
static f32 s_sky_check_scroll = 0.0f;
static u32 s_sky_check_calls = 0;
// The scroll's drift per call, measured over windows of SKY_STILL_CALLS calls
// in which the camera held still, taken off the scroll when the rate is
// checked over a turn.
static f32 s_sky_drift_per_call = 0.0f;
// The offset may move this much from one step to the next while the sky is
// tracked (near +-180 degrees Oedo Town's was measured 1.8 off); past it for
// up to 4 steps the sky is drawn flat and still tracked (a cut that comes
// back), longer and it is lost.
#define SKY_STEP_TOLERANCE      6.0f
// A watched background is the sky once the camera has turned this far and its
// scroll moved by a whole number of widths per turn (1 or 2), to within
// SKY_LEARN_SLIP of one width per turn. A backdrop that doesn't scroll with
// the heading shows 0. The turn is measured with the heading the game
// scrolled with (the frame before's): over a short turn the frame of lag is
// a large share of it.
#define SKY_LEARN_TURN          (8.0f * SKY_PI / 180.0f)
#define SKY_LEARN_SLIP          0.25f
#define SKY_LEARN_MAX_WRAPS     2
#define SKY_DRIFT_SMOOTHING     0.1f
// A camera that holds its heading (within SKY_STILL_TURN) for SKY_STILL_CALLS
// calls while the scroll drifts on by at least SKY_STILL_MIN_DRIFT texels, no
// faster than SKY_STILL_MAX_DRIFT a call: clouds drifting over a fixed view
// (the sunset field, whose camera keeps one heading). Such a sky is drawn in
// 3D with its rate taken as one width per turn and checked if the camera
// turns, as by Oedo Town's rule; while the camera holds still, its drift is
// followed.
#define SKY_STILL_TURN          (0.5f * SKY_PI / 180.0f)
#define SKY_STILL_CALLS         15
#define SKY_STILL_MIN_DRIFT     2.0f
#define SKY_STILL_MAX_DRIFT     4.0f
// Oedo Town's rule: the offset within this many texels of half the width (a
// room's skies sit up to 4 off while the camera swings in), at once, so a
// room's sky is drawn in 3D from its first scrolled frame; never on a frame
// whose scroll is still 0, 0: a background's first frame, before the game
// scrolls it, with the camera at its default heading, matches the rule
// exactly and would anchor the cylinder far too low.
#define SKY_FAST_TOLERANCE      4.0f

// Patches have no libm. Newton from the usual bit-level estimate: four steps
// reach full float precision, and this runs a handful of times a frame.
static f32 sky_sqrt(f32 x) {
    union { f32 f; u32 i; } u;
    f32 y;
    int i;
    if (x <= 0.0f) {
        return 0.0f;
    }
    u.f = x;
    u.i = 0x1FBD1DF5 + (u.i >> 1);
    y = u.f;
    for (i = 0; i < 4; i++) {
        y = 0.5f * (y + x / y);
    }
    return y;
}

static f32 sky_floor(f32 v) {
    f32 t = (f32)(s32)v;
    return (t > v) ? (t - 1.0f) : t;
}

// Where the CPU reads a background's image. A texture in the background's
// overlay file is addressed through segment 8 (texture_data 0x08000000, set
// in func_80021740_22340), which only the RSP resolves: the file's address
// comes from the same table the gSPSegment there takes it from. Anything else
// that is not a KSEG0 address is not read (NULL).
static const u8 *sky_cpu_address(BackgroundGraphicsNode *node, const u8 *p) {
    u32 a = (u32)p;
    if ((a >> 24) == 0x08) {
        const u32 base = (u32)func_800141C4_14DC4(node->overlay_file_id);
        if ((base == 0) || (base == 0xFFFFFFFF)) {
            return NULL;
        }
        return (const u8 *)(0x80000000 | ((base + (a & 0x00FFFFFF)) & 0x00FFFFFF));
    }
    if ((a >> 28) != 0x8) {
        return NULL;
    }
    return p;
}

// The open sky's colour: the commonest texel of the panorama's top row (the
// sun and the clouds that reach it are the minority there), decoded through
// the palette for CI textures. 0 for a format it does not read.
static int sky_measure_zenith(BackgroundGraphicsNode *node, u8 *rgb) {
    u16 values[32];
    u16 counts[32];
    const s32 width = node->texture_width;
    const int is_8b = (node->flags & (1 << 3)) != 0;
    const int is_ci = (node->flags & (1 << 0)) != 0;
    const int is_ia = !is_ci && ((node->flags & (1 << 1)) != 0);
    const u8 *texture = sky_cpu_address(node, node->texture_data);
    const u8 *palette = is_ci ? sky_cpu_address(node, node->palette_data) : NULL;
    int n = 0, i, best = -1;
    s32 x;
    u16 v;

    if ((texture == NULL) || (width <= 0) || (is_ci && (!is_8b || (palette == NULL)))) {
        return 0;
    }
    for (x = 0; x < width; x++) {
        v = is_8b ? texture[x] : (u16)((texture[2 * x] << 8) | texture[2 * x + 1]);
        for (i = 0; (i < n) && (values[i] != v); i++) {
        }
        if (i == n) {
            if (n == 32) {
                continue;
            }
            values[n] = v;
            counts[n] = 0;
            n++;
        }
        counts[i]++;
    }
    for (i = 0; i < n; i++) {
        if ((best < 0) || (counts[i] > counts[best])) {
            best = i;
        }
    }
    if (best < 0) {
        return 0;
    }
    v = values[best];
    if (is_ia) {
        const u8 intensity = is_8b ? (u8)((v >> 4) * 17) : (u8)(v >> 8);
        rgb[0] = rgb[1] = rgb[2] = intensity;
        return 1;
    }
    if (is_ci) {
        v = (u16)((palette[2 * v] << 8) | palette[2 * v + 1]);
    }
    else if (is_8b) {
        return 0;
    }
    // RGBA 5551.
    rgb[0] = (u8)((((v >> 11) & 0x1F) << 3) | (((v >> 11) & 0x1F) >> 2));
    rgb[1] = (u8)((((v >> 6) & 0x1F) << 3) | (((v >> 6) & 0x1F) >> 2));
    rgb[2] = (u8)((((v >> 1) & 0x1F) << 3) | (((v >> 1) & 0x1F) >> 2));
    return 1;
}

// The scroll's whole widths per turn from the scroll and the heading turned
// over a watch (0..SKY_LEARN_MAX_WRAPS; 0 for a scroll that doesn't follow the
// heading), -1 for none of them; *wraps gets the measure.
static s32 sky_whole_wraps(f32 scroll, f32 turn, f32 width_per_radian, f32 *wraps) {
    const f32 w = (-scroll / turn) / width_per_radian;
    const s32 whole = (s32)(w + ((w < 0.0f) ? -0.5f : 0.5f));
    *wraps = w;
    if ((whole < 0) || (whole > SKY_LEARN_MAX_WRAPS) || (w - (f32)whole > SKY_LEARN_SLIP) || ((f32)whole - w > SKY_LEARN_SLIP)) {
        return -1;
    }
    return whole;
}

static void sky_watch(BackgroundGraphicsNode *node, f32 yaw) {
    s_sky_candidate_node = node;
    s_sky_candidate_texture = node->texture_data;
    s_sky_candidate_flags = node->flags;
    s_sky_candidate_step = g_game_step;
    s_sky_candidate_yaw = yaw;
    s_sky_candidate_turn = 0.0f;
    s_sky_candidate_start_x = node->start_x;
    s_sky_candidate_scroll = 0.0f;
    s_sky_candidate_calls = 0;
    s_sky_candidate_forward = 0.0f;
    s_sky_candidate_back = 0.0f;
}

// Decides whether this background is the yaw-following sky and, if so,
// anchors (first time) or updates the cylinder. Returns 1 to draw it in 3D.
static int sky_track(BackgroundGraphicsNode *node) {
    const f32 texture_width = (f32)node->texture_width;
    // One width per turn, Oedo Town's scroll.
    const f32 width_per_radian = texture_width / (2.0f * SKY_PI);
    f32 dx, dy, dz, horizontal, yaw, scroll_yaw, scroll_tan_pitch, half_fovy, rate, reference, offset, change;
    f32 prev_yaw, prev_tan_pitch, learn_yaw, turn, wraps;
    s32 whole;
    int tracking, watching, prev_ok, anchor, still = 0, fast = 0, fast_match, banned;

    // The panorama path (bit 15) uses start_x with different units, and the
    // cylinder needs whole columns, so the width must divide into them.
    if ((node->flags & (1 << 15)) || (node->texture_width <= 0) || (node->texture_height <= 0) ||
        ((node->texture_width % SKY_COLUMN_TEXELS) != 0) ||
        (((SKY_WRAPS_PER_TURN * node->texture_width) / SKY_COLUMN_TEXELS) + 1 > SKY_MAX_EDGES) ||
        ((node->texture_height / SKY_BAND_ROWS) + 2 > SKY_MAX_BANDS)) {
        return 0;
    }
    if ((g_sky_view.step != g_game_step) || !g_sky_view.perspective || (g_sky_view.half_fovy == 0)) {
        return 0;
    }

    dx = g_sky_view.look_at.x - g_sky_view.position.x;
    dy = g_sky_view.look_at.y - g_sky_view.position.y;
    dz = g_sky_view.look_at.z - g_sky_view.position.z;
    horizontal = sky_sqrt(dx * dx + dz * dz);
    if (horizontal < 1e-4f) {
        return 0;
    }

    tracking = (node == s_sky_node) && (node->texture_data == s_sky_texture) && (node->flags == s_sky_flags) &&
        ((g_game_step - s_sky_last_step) <= 4);
    watching = !tracking && (node == s_sky_candidate_node) && (node->texture_data == s_sky_candidate_texture) &&
        (node->flags == s_sky_candidate_flags) && ((g_game_step - s_sky_candidate_step) <= 4);
    // The scroll's rate and the offset expected: the tracked sky's, or for a
    // new background Oedo Town's (half the width), recognised at once.
    rate = tracking ? s_sky_rate : width_per_radian;
    reference = tracking ? s_sky_offset : texture_width * 0.5f;

    // The heading the game scrolled with: this frame's, or (usually, while the
    // camera turns) the frame before's, whichever puts the offset nearer.
    yaw = sky_atan2(dx, dz);
    scroll_yaw = yaw;
    scroll_tan_pitch = dy / horizontal;
    offset = sky_wrap(node->start_x + yaw * rate, texture_width);
    change = sky_wrap_signed(offset - reference, texture_width);
    prev_ok = s_sky_prev_view_valid && (g_sky_view.step != s_sky_prev_view_step) && ((g_sky_view.step - s_sky_prev_view_step) <= 4);
    prev_yaw = s_sky_prev_view_yaw;
    prev_tan_pitch = s_sky_prev_view_tan_pitch;
    learn_yaw = prev_ok ? prev_yaw : yaw;
    if (prev_ok) {
        const f32 offset_prev = sky_wrap(node->start_x + s_sky_prev_view_yaw * rate, texture_width);
        const f32 change_prev = sky_wrap_signed(offset_prev - reference, texture_width);
        if (((change_prev < 0.0f) ? -change_prev : change_prev) < ((change < 0.0f) ? -change : change)) {
            offset = offset_prev;
            change = change_prev;
            scroll_yaw = s_sky_prev_view_yaw;
            scroll_tan_pitch = s_sky_prev_view_tan_pitch;
        }
    }
    s_sky_prev_view_yaw = yaw;
    s_sky_prev_view_tan_pitch = dy / horizontal;
    s_sky_prev_view_step = g_sky_view.step;
    s_sky_prev_view_valid = 1;
    banned = (node == s_sky_still_ban_node) && (node->texture_data == s_sky_still_ban_texture) && (node->flags == s_sky_still_ban_flags);
    fast_match = !tracking && !banned && ((node->start_x != 0.0f) || (node->start_y != 0.0f)) &&
        (change <= SKY_FAST_TOLERANCE) && (change >= -SKY_FAST_TOLERANCE);


    anchor = !tracking;
    if (tracking && !s_sky_rate_confirmed) {
        // Recognised by Oedo Town's rule: the rate confirmed over the first
        // turn, or the sky re-anchored with the one measured, or dropped.
        s_sky_check_turn += sky_wrap_signed(learn_yaw - s_sky_check_yaw, 2.0f * SKY_PI);
        s_sky_check_scroll += sky_wrap_signed(node->start_x - s_sky_check_start_x, texture_width);
        s_sky_check_calls++;
        s_sky_check_yaw = learn_yaw;
        s_sky_check_start_x = node->start_x;
        turn = (s_sky_check_turn < 0.0f) ? -s_sky_check_turn : s_sky_check_turn;
        if ((turn < SKY_STILL_TURN) && (s_sky_check_calls >= SKY_STILL_CALLS)) {
            // A window in which the camera held still: the check starts again
            // from here, and for a sky taken by its drift, its scroll is the
            // drift. (One taken by Oedo Town's rule has none: there such a
            // window's scroll is a small turn's, which taken off a slow turn's
            // many calls once read real one-width skies as 1.3 widths.)
            if (s_sky_by_still) {
                s_sky_drift_per_call = s_sky_check_scroll / (f32)s_sky_check_calls;
            }
            s_sky_check_turn = 0.0f;
            s_sky_check_scroll = 0.0f;
            s_sky_check_calls = 0;
        }
        else if (turn >= SKY_LEARN_TURN) {
            // The nearest of the rates (the question is only which: the sky
            // matched one already), none past them.
            const f32 wraps = (-(s_sky_check_scroll - s_sky_drift_per_call * (f32)s_sky_check_calls) / s_sky_check_turn) / width_per_radian;
            const s32 whole = ((wraps < -0.5f) || (wraps > (f32)SKY_LEARN_MAX_WRAPS + 0.5f)) ? -1 : (s32)(wraps + ((wraps < 0.0f) ? -0.5f : 0.5f));
            // None of the rates; or none at all for a sky taken by Oedo Town's
            // rule (a static backdrop that matched it). One taken by its drift
            // over a still camera may not follow the heading at all (the
            // sunset field's).
            if ((whole < 0) || ((whole == 0) && !s_sky_by_still)) {
                if (s_sky_by_still || s_sky_by_fast) {
                    s_sky_still_ban_node = node;
                    s_sky_still_ban_texture = node->texture_data;
                    s_sky_still_ban_flags = node->flags;
                }
                s_sky_node = NULL;
                sky_watch(node, learn_yaw);
                return 0;
            }
            s_sky_rate_confirmed = 1;
            if ((f32)whole * width_per_radian != s_sky_rate) {
                rate = (f32)whole * width_per_radian;
                scroll_yaw = prev_ok ? prev_yaw : yaw;
                scroll_tan_pitch = prev_ok ? prev_tan_pitch : dy / horizontal;
                offset = sky_wrap(node->start_x + scroll_yaw * rate, texture_width);
                anchor = 1;
            }
            else {
            }
        }
    }
    if (tracking && !anchor) {
        if ((change > SKY_STEP_TOLERANCE) || (change < -SKY_STEP_TOLERANCE)) {
            // Flat this step; tracked for up to 4 steps (s_sky_last_step is
            // left as it was), then watched afresh.
            return 0;
        }
        // The cylinder follows the offset's drift (clouds): advanced by the
        // drift measured every call, so it moves evenly, and pulled toward the
        // offset seen when that can be trusted (the rate confirmed, a scroll
        // that doesn't follow the heading, or a camera holding still).
        s_sky_offset = offset;
        s_sky_offset_smooth = sky_wrap(s_sky_offset_smooth + s_sky_drift_per_call, texture_width);
        if (s_sky_rate_confirmed || (s_sky_rate == 0.0f) || ((s_sky_check_turn < SKY_STILL_TURN) && (s_sky_check_turn > -SKY_STILL_TURN))) {
            s_sky_offset_smooth = sky_wrap(s_sky_offset_smooth + SKY_DRIFT_SMOOTHING * sky_wrap_signed(offset - s_sky_offset_smooth, texture_width), texture_width);
        }
        s_sky_u_at_zero_bearing = s_sky_u_anchor + sky_wrap_signed(s_sky_offset_smooth - s_sky_offset_anchor, texture_width);
    }
    else if (tracking) {
        // Re-anchored with the measured rate (above).
    }
    else if (fast_match) {
        // Oedo Town's rule, on a new or a watched background.
        fast = 1;
    }
    else if (!watching) {
        // A new background: watched.
        sky_watch(node, learn_yaw);
        return 0;
    }
    else {
        // The scroll against the heading turned since the watch began: a
        // background scrolled by the heading moves by a whole number of widths
        // per turn (with the game's frame of lag, which a long enough turn
        // makes small); one that isn't moves by none.
        const f32 moved = sky_wrap_signed(node->start_x - s_sky_candidate_start_x, texture_width);
        s_sky_candidate_turn += sky_wrap_signed(learn_yaw - s_sky_candidate_yaw, 2.0f * SKY_PI);
        s_sky_candidate_scroll += moved;
        if (moved > 0.0f) {
            s_sky_candidate_forward += moved;
        }
        else {
            s_sky_candidate_back -= moved;
        }
        s_sky_candidate_yaw = learn_yaw;
        s_sky_candidate_start_x = node->start_x;
        s_sky_candidate_step = g_game_step;
        s_sky_candidate_calls++;
        turn = (s_sky_candidate_turn < 0.0f) ? -s_sky_candidate_turn : s_sky_candidate_turn;
        if (turn < SKY_STILL_TURN) {
            const f32 drift = (s_sky_candidate_scroll < 0.0f) ? -s_sky_candidate_scroll : s_sky_candidate_scroll;
            const f32 against = (s_sky_candidate_forward < s_sky_candidate_back) ? s_sky_candidate_forward : s_sky_candidate_back;
            if ((s_sky_candidate_calls < SKY_STILL_CALLS) || (drift < SKY_STILL_MIN_DRIFT) ||
                (drift > SKY_STILL_MAX_DRIFT * (f32)s_sky_candidate_calls) || (against > 0.1f * drift) || banned) {
                return 0;
            }
            still = 1;
        }
        else if (turn < SKY_LEARN_TURN) {
            return 0;
        }
    }
    if (fast) {
        // Taken at one width per turn (the offset and heading as chosen
        // above, nearest the rule), confirmed over the first turn.
        rate = width_per_radian;
    }
    else if (still) {
        // Clouds drifting over a fixed view (above), taken not to follow the
        // heading until a turn shows otherwise: its offset is its scroll.
        rate = 0.0f;
        scroll_yaw = yaw;
        scroll_tan_pitch = dy / horizontal;
        offset = sky_wrap(node->start_x, texture_width);
    }
    else if (watching) {
        whole = sky_whole_wraps(s_sky_candidate_scroll, s_sky_candidate_turn, width_per_radian, &wraps);
        if (whole < 1) {
            sky_watch(node, learn_yaw);
            return 0;
        }
        rate = (f32)whole * width_per_radian;
        // Anchored on the heading the game scrolls with: the frame before's
        // (measured in Oedo Town), when there is one.
        if (prev_ok) {
            scroll_yaw = prev_yaw;
            scroll_tan_pitch = prev_tan_pitch;
        }
        else {
            scroll_yaw = yaw;
            scroll_tan_pitch = dy / horizontal;
        }
        offset = sky_wrap(node->start_x + scroll_yaw * rate, texture_width);
    }

    half_fovy = (f32)g_sky_view.half_fovy * (SKY_PI / 32768.0f);
    s_sky_cot = sky_cos(half_fovy) / sky_sin(half_fovy);
    s_sky_yaw = yaw;
    {
        const f32 length = sky_sqrt(dx * dx + dy * dy + dz * dz);
        s_sky_forward[0] = dx / length;
        s_sky_forward[1] = dy / length;
        s_sky_forward[2] = dz / length;
    }

    if (anchor) {
        // Anchor so the centre of the screen shows what the game shows there
        // now: the game's texel at the screen centre is start_x + 160 (its rect
        // starts at the left edge, one texel per unit), and the cylinder maps
        // bearing phi to texel u = u0 - phi * K.
        s_sky_node = node;
        s_sky_texture = node->texture_data;
        s_sky_flags = node->flags;
        s_sky_candidate_node = NULL;
        // Confirmed when learned or re-rated; by Oedo Town's rule or a still
        // camera, checked from here (with the drift the still camera measured).
        s_sky_rate_confirmed = !still && !fast;
        if (!tracking) {
            s_sky_by_still = still;
            s_sky_by_fast = fast;
        }
        if (!tracking) {
            s_sky_drift_per_call = still ? s_sky_candidate_scroll / (f32)s_sky_candidate_calls : 0.0f;
        }
        s_sky_check_yaw = learn_yaw;
        s_sky_check_turn = 0.0f;
        s_sky_check_start_x = node->start_x;
        s_sky_check_scroll = 0.0f;
        s_sky_check_calls = 0;
        s_sky_rate = rate;
        s_sky_texels_per_radian = (f32)SKY_WRAPS_PER_TURN * texture_width / (2.0f * SKY_PI);
        s_sky_u_at_zero_bearing = node->start_x + 160.0f - node->upper_left_corner_x + scroll_yaw * s_sky_texels_per_radian;
        s_sky_u_anchor = s_sky_u_at_zero_bearing;
        s_sky_offset = offset;
        s_sky_offset_smooth = offset;
        s_sky_offset_anchor = offset;
        s_sky_anchor_sy = node->start_y;
        s_sky_anchor_tan_pitch = scroll_tan_pitch;
        s_sky_anchor_cot = s_sky_cot;
        s_sky_zenith_valid = sky_measure_zenith(node, s_sky_zenith);
    }
    s_sky_last_step = g_game_step;
    return 1;
}

// Height on the cylinder of panorama row t. Where the game drew row t at the
// screen centre when the sky was anchored - row t at screen row t - sy, so NDC
// y = 1 - (t - sy) / 120 - is the elevation e with y = cot * tan(e - pitch)
// there; the height is R * tan(e), expanded with the tangent sum so no trig is
// needed.
static f32 sky_row_height(f32 t) {
    const f32 slope = (1.0f - (t - s_sky_anchor_sy) / 120.0f) / s_sky_anchor_cot;
    const f32 denominator = 1.0f - s_sky_anchor_tan_pitch * slope;
    if ((denominator < 0.05f) && (denominator > -0.05f)) {
        return (denominator < 0.0f) ? -20.0f * SKY_RADIUS : 20.0f * SKY_RADIUS;
    }
    return SKY_RADIUS * (s_sky_anchor_tan_pitch + slope) / denominator;
}

// F3DEX 1.x vertex and two-triangle commands. The patches build against the
// Fast3D encodings, which differ in exactly these: F3DEX packs vertex indices
// doubled and adds G_TRI2.
#define SKY_F3DEX_G_VTX   0x04
#define SKY_F3DEX_G_TRI2  0xB1

static void sky_vertex(Gfx **gfx, Vtx *v, u32 n) {
    (*gfx)->words.w0 = (SKY_F3DEX_G_VTX << 24) | (0 << 16) | ((n << 10) | ((sizeof(Vtx) * n) - 1));
    (*gfx)->words.w1 = (u32)v;
    (*gfx)++;
}

static void sky_quad(Gfx **gfx) {
    (*gfx)->words.w0 = (SKY_F3DEX_G_TRI2 << 24) | ((0 * 2) << 16) | ((1 * 2) << 8) | (2 * 2);
    (*gfx)->words.w1 = ((0 * 2) << 16) | ((2 * 2) << 8) | (3 * 2);
    (*gfx)++;
}

static Vtx s_sky_vertices[2][SKY_MAX_QUADS * 4];
// Generous: each quad is a tile load (7 commands), a vertex load and a TRI2.
static Gfx s_sky_display_list[2][SKY_MAX_QUADS * 10 + 64];
static f32 s_sky_matrices[2][3][16];
static u32 s_sky_buffer = 0;

static s16 sky_round(f32 v) {
    return (s16)((v >= 0.0f) ? (v + 0.5f) : (v - 0.5f));
}

static void sky_set_vertex(Vtx *v, f32 x, f32 y, f32 z, f32 s, f32 t) {
    v->v.ob[0] = sky_round(x);
    v->v.ob[1] = sky_round(y);
    v->v.ob[2] = sky_round(z);
    v->v.flag = 0;
    v->v.tc[0] = (s16)(s * 32.0f);
    v->v.tc[1] = (s16)(t * 32.0f);
    v->v.cn[0] = 0xFF;
    v->v.cn[1] = 0xFF;
    v->v.cn[2] = 0xFF;
    v->v.cn[3] = 0xFF;
}

static void sky_set_vertex_colour(Vtx *v, const u8 *rgb, u8 alpha) {
    v->v.cn[0] = rgb[0];
    v->v.cn[1] = rgb[1];
    v->v.cn[2] = rgb[2];
    v->v.cn[3] = alpha;
}

// One textured quad: a tile load covering texels [texel0, texel0 + column]
// and rows [load_row0, load_row1] (plus a texel of overlap so bilinear
// filtering at the edges samples neighbours rather than clamping), its four
// vertices, and the two triangles. Vertices run top-left, top-right,
// bottom-right, bottom-left.
static void sky_emit_quad(Gfx **gfx, Vtx *v, BackgroundGraphicsNode *node, s32 image_format, s32 image_size,
                          f32 xa, f32 ya, f32 za, f32 xb, f32 yb, f32 zb, f32 xc, f32 yc, f32 zc, f32 xd, f32 yd, f32 zd,
                          s32 texel0, f32 t_top, f32 t_bottom, s32 load_row0, s32 load_row1,
                          const u8 *shade, u8 alpha_top, u8 alpha_bottom) {
    const s32 texture_width = node->texture_width;
    const s32 texture_height = node->texture_height;
    s32 load_s0 = (texel0 > 0) ? (texel0 - 1) : 0;
    s32 load_s1 = texel0 + SKY_COLUMN_TEXELS;
    s32 load_t0 = (load_row0 > 0) ? (load_row0 - 1) : 0;
    s32 load_t1 = load_row1;
    if (load_s1 > texture_width - 1) {
        load_s1 = texture_width - 1;
    }
    if (load_t1 > texture_height - 1) {
        load_t1 = texture_height - 1;
    }

    if (image_size == G_IM_SIZ_8b) {
        gDPLoadTextureTile((*gfx)++, node->texture_data, image_format, G_IM_SIZ_8b,
            texture_width, texture_height, load_s0, load_t0, load_s1, load_t1, 0,
            G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    }
    else {
        gDPLoadTextureTile((*gfx)++, node->texture_data, G_IM_FMT_RGBA, G_IM_SIZ_16b,
            texture_width, texture_height, load_s0, load_t0, load_s1, load_t1, 0,
            G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOLOD);
    }

    sky_set_vertex(&v[0], xa, ya, za, (f32)texel0, t_top);
    sky_set_vertex(&v[1], xb, yb, zb, (f32)(texel0 + SKY_COLUMN_TEXELS), t_top);
    sky_set_vertex(&v[2], xc, yc, zc, (f32)(texel0 + SKY_COLUMN_TEXELS), t_bottom);
    sky_set_vertex(&v[3], xd, yd, zd, (f32)texel0, t_bottom);
    if (shade != NULL) {
        // The top cap's fade (sky_draw_3d): the shade colour, its alpha the
        // share of the texture.
        sky_set_vertex_colour(&v[0], shade, alpha_top);
        sky_set_vertex_colour(&v[1], shade, alpha_top);
        sky_set_vertex_colour(&v[2], shade, alpha_bottom);
        sky_set_vertex_colour(&v[3], shade, alpha_bottom);
    }
    sky_vertex(gfx, v, 4);
    sky_quad(gfx);
}

void func_80022EC0_23AC0(BackgroundGraphicsNode *node);

// The last combiner set in [start, end) of the frame's list, following calls
// into display lists at KSEG0 addresses (func_80022EC0_23AC0 sets the
// background's through one, 0x8006D0F0).
static int sky_find_combine(Gfx *start, Gfx *end, u32 *w0, u32 *w1) {
    Gfx *g;
    int found = 0;
    for (g = start; g < end; g++) {
        const u32 op = g->words.w0 >> 24;
        if (op == G_SETCOMBINE) {
            *w0 = g->words.w0;
            *w1 = g->words.w1;
            found = 1;
        }
        else if ((op == G_DL) && ((g->words.w1 & 0xFF800000) == 0x80000000)) {
            Gfx *c = (Gfx *)g->words.w1;
            int i;
            for (i = 0; i < 64; i++, c++) {
                const u32 cop = c->words.w0 >> 24;
                if (cop == G_SETCOMBINE) {
                    *w0 = c->words.w0;
                    *w1 = c->words.w1;
                    found = 1;
                }
                else if (cop == G_ENDDL) {
                    break;
                }
            }
        }
    }
    return found;
}

static void sky_draw_3d(BackgroundGraphicsNode *node) {
    const s32 texture_width = node->texture_width;
    const s32 texture_height = node->texture_height;
    const f32 K = s_sky_texels_per_radian;
    const u32 buffer = s_sky_buffer;
    Vtx *vertices = s_sky_vertices[buffer];
    Gfx *dl = s_sky_display_list[buffer];
    Gfx *gfx = dl;
    f32 *projection = s_sky_matrices[buffer][0];
    f32 *view = s_sky_matrices[buffer][1];
    f32 *identity = s_sky_matrices[buffer][2];
    u32 quad_count = 0;
    s32 image_format, image_size, column, row;
    f32 u_start;
    int i;
    const int cull_columns = (s_sky_forward[1] < SKY_CULL_PITCH_SIN) && (s_sky_forward[1] > -SKY_CULL_PITCH_SIN);
    u32 game_cc_w0 = 0, game_cc_w1 = 0;

    s_sky_buffer ^= 1;

    // guPerspective's matrix (row vectors), at the game's 4:3 - RT64 widens it
    // for widescreen exactly as it does the world's. Near and far bracket the
    // cylinder; the tops of the bands can reach ~1.7 radii away.
    {
        const f32 near_z = 8.0f;
        const f32 far_z = 8.0f * SKY_RADIUS;
        for (i = 0; i < 16; i++) {
            projection[i] = 0.0f;
            identity[i] = ((i % 5) == 0) ? 1.0f : 0.0f;
        }
        projection[0] = s_sky_cot / (4.0f / 3.0f);
        projection[5] = s_sky_cot;
        projection[10] = (near_z + far_z) / (near_z - far_z);
        projection[11] = -1.0f;
        projection[14] = (2.0f * near_z * far_z) / (near_z - far_z);
    }

    // guLookAt's matrix from the origin along the camera's forward vector: the
    // camera's rotation without its translation.
    {
        const f32 look[3] = { -s_sky_forward[0], -s_sky_forward[1], -s_sky_forward[2] };
        f32 right[3], up[3], length;
        // right = (0, 1, 0) x look
        right[0] = look[2];
        right[1] = 0.0f;
        right[2] = -look[0];
        length = sky_sqrt(right[0] * right[0] + right[2] * right[2]);
        right[0] /= length;
        right[2] /= length;
        // up = look x right
        up[0] = look[1] * right[2] - look[2] * right[1];
        up[1] = look[2] * right[0] - look[0] * right[2];
        up[2] = look[0] * right[1] - look[1] * right[0];
        for (i = 0; i < 16; i++) {
            view[i] = 0.0f;
        }
        view[0] = right[0]; view[4] = right[1]; view[8] = right[2];
        view[1] = up[0];    view[5] = up[1];    view[9] = up[2];
        view[2] = look[0];  view[6] = look[1];  view[10] = look[2];
        view[15] = 1.0f;
    }

    if (node->flags & (1 << 0)) {
        image_format = G_IM_FMT_CI;
    } else if (node->flags & (1 << 1)) {
        image_format = G_IM_FMT_IA;
    } else {
        image_format = G_IM_FMT_RGBA;
    }
    image_size = (node->flags & (1 << 3)) ? G_IM_SIZ_8b : G_IM_SIZ_16b;

    // The game's background state (palette, filtering, combiner), then what
    // triangles need on top: perspective-correct texturing, no depth (the sky
    // is drawn first, behind everything) and no lighting, culling or fog.
    {
        Gfx *state_start = D_8015C5CC_15D1CC;
        func_80022EC0_23AC0(node);
        // The game's combiner for the background, for the top cap's fade; if
        // it cannot be found, its usual one (G_CC_DECALRGBA in both cycles).
        if (!sky_find_combine(state_start, D_8015C5CC_15D1CC, &game_cc_w0, &game_cc_w1)) {
            game_cc_w0 = 0xFCFFFFFF;
            game_cc_w1 = 0xFFFCF279;
        }
    }
    gDPPipeSync(gfx++);
    gDPSetCycleType(gfx++, G_CYC_1CYCLE);
    gDPSetTexturePersp(gfx++, G_TP_PERSP);
    if (!(node->flags & ((1 << 4) | (1 << 5)))) {
        gDPSetRenderMode(gfx++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
    }
    gSPClearGeometryMode(gfx++, 0xFFFFFFFF);
    gSPTexture(gfx++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);

    // Columns of SKY_COLUMN_TEXELS, starting on a column boundary, around the
    // whole turn; only those near the view are drawn. Bearing falls as the
    // texel rises: u = u0 - phi * K.
    //
    // Every corner is computed ONCE per frame and shared by the quads that
    // meet there. Nothing is drawn behind the sky, so any gap between
    // neighbouring quads shows as a dark line - and quads that each computed
    // their own corners disagreed by a unit after rounding often enough to
    // show, at ~2 px per unit at this radius. Shared integer corners make the
    // mesh watertight by construction.
    {
        const s32 column_count = (SKY_WRAPS_PER_TURN * texture_width) / SKY_COLUMN_TEXELS;
        f32 dir_x[SKY_MAX_EDGES];
        f32 dir_z[SKY_MAX_EDGES];
        f32 bearing[SKY_MAX_EDGES];
        f32 band_y[SKY_MAX_BANDS];
        s32 band_row[SKY_MAX_BANDS];
        s32 band_count = 0;
        f32 cap_radius[2][4];
        f32 cap_y[2][4];
        s32 cap_rings[2];
        s32 k, b, cap, ring;
        // The top cap fades to the sky colour (SKY_CAP_FADE): drawn after the
        // rest, with its own combiner, for these columns.
        const int top_fade = s_sky_zenith_valid;
        s32 fade_column[SKY_MAX_EDGES];
        s32 fade_count = 0;

        u_start = (f32)SKY_COLUMN_TEXELS * sky_floor((s_sky_u_at_zero_bearing - SKY_PI * K) / (f32)SKY_COLUMN_TEXELS);
        for (k = 0; k <= column_count; k++) {
            const f32 wrapped = sky_wrap_signed((s_sky_u_at_zero_bearing - (u_start + (f32)(k * SKY_COLUMN_TEXELS))) / K, 2.0f * SKY_PI);
            bearing[k] = wrapped;
            dir_x[k] = sky_sin(wrapped);
            dir_z[k] = sky_cos(wrapped);
        }
        // The last edge closes the turn onto the first: the same corner, not a
        // recomputation of it that rounds differently.
        bearing[column_count] = bearing[0];
        dir_x[column_count] = dir_x[0];
        dir_z[column_count] = dir_z[0];

        for (row = 0; ; row += SKY_BAND_ROWS) {
            if (row > texture_height) {
                row = texture_height;
            }
            band_row[band_count] = row;
            band_y[band_count] = (f32)sky_round(sky_row_height((f32)row));
            band_count++;
            if (row == texture_height) {
                break;
            }
        }

        // The caps' rings: ring 0 is the cylinder's own edge (radius R at the
        // edge height), rings 1 and 2 step towards the pole on the sphere of the
        // same directions. With the fade, the top cap's ring 1 is SKY_CAP_FADE
        // above the edge and rings 2 and 3 step on to the pole.
        for (cap = 0; cap < 2; cap++) {
            const f32 edge_height = (cap == 0) ? band_y[0] : band_y[band_count - 1];
            const f32 edge_elevation = sky_atan2(edge_height, SKY_RADIUS);
            const f32 end_elevation = (cap == 0) ? SKY_CAP_ELEVATION : -SKY_CAP_ELEVATION;
            f32 fade_elevation = edge_elevation + SKY_CAP_FADE;
            if (fade_elevation > end_elevation) {
                fade_elevation = end_elevation;
            }
            cap_rings[cap] = ((cap == 0) && top_fade) ? 3 : 2;
            cap_radius[cap][0] = SKY_RADIUS;
            cap_y[cap][0] = edge_height;
            for (ring = 1; ring <= cap_rings[cap]; ring++) {
                f32 elevation = (cap_rings[cap] == 3) ?
                    ((ring == 1) ? fade_elevation : fade_elevation + (end_elevation - fade_elevation) * ((f32)(ring - 1) / 2.0f)) :
                    edge_elevation + (end_elevation - edge_elevation) * ((f32)ring / 2.0f);
                // A cylinder edge already past the pole direction has nothing
                // left to cap.
                if (((cap == 0) && (elevation < edge_elevation)) || ((cap == 1) && (elevation > edge_elevation))) {
                    elevation = edge_elevation;
                }
                cap_radius[cap][ring] = SKY_RADIUS * sky_cos(elevation);
                cap_y[cap][ring] = (f32)sky_round(SKY_RADIUS * sky_sin(elevation));
            }
        }

        for (column = 0; column < column_count; column++) {
            const f32 u0 = u_start + (f32)(column * SKY_COLUMN_TEXELS);
            const s32 texel0 = (s32)sky_wrap(u0, (f32)texture_width);
            const f32 mid = sky_wrap_signed((u_start + (f32)(column * SKY_COLUMN_TEXELS) + (f32)(SKY_COLUMN_TEXELS / 2) -
                s_sky_u_at_zero_bearing) / -K, 2.0f * SKY_PI);
            const f32 relative = sky_wrap_signed(mid - s_sky_yaw, 2.0f * SKY_PI);
            const f32 x0 = (f32)sky_round(SKY_RADIUS * dir_x[column]);
            const f32 z0 = (f32)sky_round(SKY_RADIUS * dir_z[column]);
            const f32 x1 = (f32)sky_round(SKY_RADIUS * dir_x[column + 1]);
            const f32 z1 = (f32)sky_round(SKY_RADIUS * dir_z[column + 1]);

            if (cull_columns && ((relative > SKY_VISIBLE_HALF_ANGLE) || (relative < -SKY_VISIBLE_HALF_ANGLE))) {
                continue;
            }

            // The panorama's rows, top to bottom, in bands that fit TMEM.
            for (b = 0; b + 1 < band_count; b++) {
                if (quad_count >= SKY_MAX_QUADS) {
                    break;
                }
                sky_emit_quad(&gfx, &vertices[quad_count * 4], node, image_format, image_size,
                    x0, band_y[b], z0, x1, band_y[b], z1, x1, band_y[b + 1], z1, x0, band_y[b + 1], z0,
                    texel0, (f32)band_row[b], (f32)band_row[b + 1], band_row[b], band_row[b + 1], NULL, 0xFF, 0xFF);
                quad_count++;
            }

            // The caps, each stretching its edge row: the same t the cylinder's
            // edge samples (0 at the top, the full height at the bottom), so the
            // colour carries straight across the join.
            for (cap = 0; cap < 2; cap++) {
                const f32 t_edge = (cap == 0) ? 0.0f : (f32)texture_height;
                const s32 edge_row = (cap == 0) ? 0 : (texture_height - 1);
                if ((cap == 0) && top_fade) {
                    fade_column[fade_count++] = column;
                    continue;
                }
                for (ring = 0; ring < cap_rings[cap]; ring++) {
                    const f32 ra = cap_radius[cap][ring];
                    const f32 rb = cap_radius[cap][ring + 1];
                    const f32 xa0 = (ring == 0) ? x0 : (f32)sky_round(ra * dir_x[column]);
                    const f32 za0 = (ring == 0) ? z0 : (f32)sky_round(ra * dir_z[column]);
                    const f32 xa1 = (ring == 0) ? x1 : (f32)sky_round(ra * dir_x[column + 1]);
                    const f32 za1 = (ring == 0) ? z1 : (f32)sky_round(ra * dir_z[column + 1]);
                    const f32 xb0 = (f32)sky_round(rb * dir_x[column]);
                    const f32 zb0 = (f32)sky_round(rb * dir_z[column]);
                    const f32 xb1 = (f32)sky_round(rb * dir_x[column + 1]);
                    const f32 zb1 = (f32)sky_round(rb * dir_z[column + 1]);
                    if (cap_y[cap][ring] == cap_y[cap][ring + 1]) {
                        continue;
                    }
                    if (quad_count >= SKY_MAX_QUADS) {
                        break;
                    }
                    sky_emit_quad(&gfx, &vertices[quad_count * 4], node, image_format, image_size,
                        xa0, cap_y[cap][ring], za0, xa1, cap_y[cap][ring], za1,
                        xb1, cap_y[cap][ring + 1], zb1, xb0, cap_y[cap][ring + 1], zb0,
                        texel0, t_edge, t_edge, edge_row, edge_row, NULL, 0xFF, 0xFF);
                    quad_count++;
                }
            }
        }

        // The top cap with the fade: the game's combiner with its colour made
        // (TEXEL0 - SHADE) * SHADE_ALPHA + SHADE in both cycles (its alpha, and
        // so its fades, kept), the shade the sky colour, its alpha 255 at the
        // panorama's edge and 0 from SKY_CAP_FADE above it.
        if (fade_count > 0) {
            gDPPipeSync(gfx++);
            gfx->words.w0 = (game_cc_w0 & ~((0xFu << 20) | (0x1Fu << 15) | (0xFu << 5) | 0x1Fu)) |
                (G_CCMUX_TEXEL0 << 20) | (G_CCMUX_SHADE_ALPHA << 15) | (G_CCMUX_TEXEL0 << 5) | G_CCMUX_SHADE_ALPHA;
            gfx->words.w1 = (game_cc_w1 & ~((0xFu << 28) | (0xFu << 24) | (0x7u << 15) | (0x7u << 6))) |
                ((u32)G_CCMUX_SHADE << 28) | (G_CCMUX_SHADE << 24) | (G_CCMUX_SHADE << 15) | (G_CCMUX_SHADE << 6);
            gfx++;
            gSPSetGeometryMode(gfx++, G_SHADE | G_SHADING_SMOOTH);
            for (k = 0; k < fade_count; k++) {
                const s32 c = fade_column[k];
                const f32 u0 = u_start + (f32)(c * SKY_COLUMN_TEXELS);
                const s32 texel0 = (s32)sky_wrap(u0, (f32)texture_width);
                for (ring = 0; ring < cap_rings[0]; ring++) {
                    const f32 ra = cap_radius[0][ring];
                    const f32 rb = cap_radius[0][ring + 1];
                    if (cap_y[0][ring] == cap_y[0][ring + 1]) {
                        continue;
                    }
                    if (quad_count >= SKY_MAX_QUADS) {
                        break;
                    }
                    sky_emit_quad(&gfx, &vertices[quad_count * 4], node, image_format, image_size,
                        (f32)sky_round(ra * dir_x[c]), cap_y[0][ring], (f32)sky_round(ra * dir_z[c]),
                        (f32)sky_round(ra * dir_x[c + 1]), cap_y[0][ring], (f32)sky_round(ra * dir_z[c + 1]),
                        (f32)sky_round(rb * dir_x[c + 1]), cap_y[0][ring + 1], (f32)sky_round(rb * dir_z[c + 1]),
                        (f32)sky_round(rb * dir_x[c]), cap_y[0][ring + 1], (f32)sky_round(rb * dir_z[c]),
                        texel0, 0.0f, 0.0f, 0, 0, s_sky_zenith, (ring == 0) ? 0xFF : 0x00, 0x00);
                    quad_count++;
                }
            }
        }
    }

    gSPEndDisplayList(gfx++);

    // In the game's list: save everything the sky changes, set its projection
    // (tagged as the skybox) and an identity model, run it, restore.
    gEXPushProjectionMatrix(D_8015C5CC_15D1CC++);
    gEXPushGeometryMode(D_8015C5CC_15D1CC++);
    gEXPushOtherMode(D_8015C5CC_15D1CC++);
    gEXPushCombineMode(D_8015C5CC_15D1CC++);
    gEXMatrixGroup(D_8015C5CC_15D1CC++, PROJECTION_SKYBOX_TRANSFORM_ID, G_EX_INTERPOLATE_SIMPLE, G_EX_PUSH, G_MTX_PROJECTION,
        G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_INTERPOLATE,
        G_EX_COMPONENT_INTERPOLATE, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_INTERPOLATE, G_EX_ORDER_LINEAR, G_EX_EDIT_NONE,
        G_EX_ASPECT_AUTO, G_EX_COMPONENT_SKIP, G_EX_COMPONENT_AUTO);
    gEXMatrixFloat(D_8015C5CC_15D1CC++, projection, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    // Multiplying an affine matrix onto a plain perspective is what RT64 takes
    // as the view, so this is interpolated and stereo-shifted as a camera.
    gEXMatrixFloat(D_8015C5CC_15D1CC++, view, G_MTX_PROJECTION | G_MTX_MUL | G_MTX_NOPUSH);
    gEXMatrixGroupNoInterpolate(D_8015C5CC_15D1CC++, G_EX_PUSH, G_MTX_MODELVIEW, G_EX_EDIT_NONE);
    gEXMatrixFloat(D_8015C5CC_15D1CC++, identity, G_MTX_MODELVIEW | G_MTX_LOAD | G_MTX_PUSH);
    gSPDisplayList(D_8015C5CC_15D1CC++, dl);
    gSPPopMatrix(D_8015C5CC_15D1CC++, G_MTX_MODELVIEW);
    gEXPopMatrixGroup(D_8015C5CC_15D1CC++, G_MTX_MODELVIEW);
    gEXPopMatrixGroup(D_8015C5CC_15D1CC++, G_MTX_PROJECTION);
    gEXPopCombineMode(D_8015C5CC_15D1CC++);
    gEXPopOtherMode(D_8015C5CC_15D1CC++);
    gEXPopGeometryMode(D_8015C5CC_15D1CC++);
    gEXPopProjectionMatrix(D_8015C5CC_15D1CC++);
}

RECOMP_PATCH void func_80021740_22340(BackgroundGraphicsNode* node)
{
	s32 image_size;
	int sky_tracking;
	int sky_is_backdrop;

	// @recomp Tag the texture rectangles of a BACKDROP as skybox rects so
	// RT64's stereo pipeline uses maximum positive parallax (infinity-like
	// depth) on them instead of the HUD depth shift the default rect path
	// applies. The flag is per-draw extended state (DrawExtendedFlags.skyboxRect),
	// set via the gEXSetSkyboxRect GBI command and carried onto each drawCall
	// by RT64's state loader. Matrix-group tags via G_MTX_PROJECTION don't
	// propagate here because texture rects hardcode transformsIndex=0.
	//
	// Not every node this dispatcher draws is a backdrop: the same nodes draw
	// 2D overlays such as the banner across the bottom of the screen (a 221x28
	// node at 40,190) and title-screen captions (180x32 at 70,192). Tagged as
	// sky, those sat at infinity behind the world; untagged, they take the
	// rect path's HUD depth like the rest of the 2D UI. A backdrop covers the
	// whole screen - within a few units of each edge, since the game's own
	// full-screen nodes are 319x239 - and the panorama path (bit 15) always
	// draws across the full background area.
	sky_is_backdrop = (node->flags & (1 << 15)) ||
		((node->upper_left_corner_x <= (f32)(D_8006D158_6DD58 + 8)) &&
		 (node->upper_left_corner_y <= (f32)(D_8006D15C_6DD5C + 8)) &&
		 ((node->upper_left_corner_x + node->rectangle_width) >= (f32)(D_8006D160_6DD60 - 8)) &&
		 ((node->upper_left_corner_y + node->rectangle_height) >= (f32)(D_8006D164_6DD64 - 8)));
	if (sky_is_backdrop) {
#ifdef GOEMON_3DS
		// @recomp rt64-3ds records the projection group id on rectangles too, so a backdrop
		// is tagged like the sky cylinder and a ProjKind::Rect rule puts it at infinity
		// (platform/3ds/main_3ds.cpp). Pushed, so the camera's id returns afterwards.
		gEXMatrixGroupSkipAll(D_8015C5CC_15D1CC++, PROJECTION_SKYBOX_TRANSFORM_ID, G_EX_PUSH, G_MTX_PROJECTION, G_EX_EDIT_NONE);
#else
		gEXSetSkyboxRect(D_8015C5CC_15D1CC++, 1);
#endif
	}

	if (!(node->flags & (1 << 13))) {
		gSPSegment(D_8015C5CC_15D1CC++, 8, func_800141C4_14DC4(node->overlay_file_id));
		node->texture_data = (u8 *)0x08000000;
	}

	if (node->texture_data == (u8 *)-1) {
		// Intentionally crash by writing to an invalid memory location, triggering the debugger.
		*((volatile s32 *)-1) = 0;
	}

	if (node->flags & (1 << 2)) {
		node->bits_per_pixel = 4;
	} else if (node->flags & (1 << 3)) {
		node->bits_per_pixel = 8;
	} else {
		node->bits_per_pixel = 16;
	}

	if (!(node->flags & (1 << 13))) {
		image_size = node->texture_width * node->texture_height * node->bits_per_pixel;
		if (image_size < 0) {
			image_size += 7;
		}

		node->palette_data = node->texture_data + (image_size >> 3);
	}

	// @recomp A sky that follows the camera's heading is drawn as a 3D
	// cylinder at infinity (sky_draw_3d) rather than the game's flat scroll.
	// Bit 14 draws a clipped sub-rect and 4-bit textures need even-aligned
	// loads the column tiles do not keep, so those stay with the game's path.
	sky_tracking = 0;
	if (sky_is_backdrop && !(node->flags & (1 << 14)) && !((node->flags & (1 << 2)) && !(node->flags & (1 << 3)))) {
		sky_tracking = sky_track(node);
	}

#ifdef GOEMON_3DS
	// @recomp The cylinder is up to 400 quads of the game's own CPU time a
	// frame: the 3DS settings menu's Sky row can keep the flat scroll instead
	// (the default on an Old 3DS).
	if (sky_tracking && !recomp_get_sky_3d()) {
		sky_tracking = 0;
	}
#endif

	if (sky_tracking) {
		sky_draw_3d(node);
	} else if (node->flags & (1 << 14)) {
		func_80022214_22E14(node);
	} else if (node->flags & (1 << 15)) {
		func_80021894_22494(node->texture_data, node->palette_data, node->start_x, node->start_y);
	} else {
		func_80022A74_23674(node);
	}


#ifdef GOEMON_3DS
	if (sky_is_backdrop) {
		gEXPopMatrixGroup(D_8015C5CC_15D1CC++, G_MTX_PROJECTION);
	}
#else
	// @recomp Clear the skybox-rect flag so subsequent rectangles (HUD,
	// dialog, text, etc.) revert to the default HUD-depth stereo shift.
	gEXSetSkyboxRect(D_8015C5CC_15D1CC++, 0);
#endif
}


void func_80021B98_22798(u8 *texture_data, f32 source_x, f32 source_y, s32 tile_width, s32 tile_height, u32 destination_x, u32 destination_y);

RECOMP_PATCH void func_80021894_22494(u8 *texture_data, u8 *palette_data, f32 start_x, f32 start_y)
{
	s32 source_x;
	s32 source_y;
	s32 destination_x;
	s32 destination_y;
	s32 tile_width;
	s32 tile_height;
	f32 x_remainder;

	x_remainder = start_x - (f32)(s32)start_x;

	if (start_x < 0.0) {
		start_x = start_x + (f32)(-(s32)start_x / 1280 + 1) * 1280.0 + x_remainder;
	}

	if (1280.0 <= start_x) {
		start_x = (f32)((s32)start_x % 1280) + x_remainder;
	}

	if (start_y > 240.0) {
		start_y = 240.0;
	} else if (start_y < 0.0) {
		start_y = 0.0;
	}

	gDPLoadTLUT_pal256(D_8015C5CC_15D1CC++, palette_data);

	start_x /= 2.0f;
	start_y /= 2.0f;

	// Loop over the screen vertically, drawing a row of tiles in each iteration.
	for (destination_y = D_8006D15C_6DD5C; destination_y < D_8006D164_6DD64; destination_y += 20) {
		source_y = (s32)start_y;

		// If we're near the bottom edge, calculate a partial tile height.
		if (D_8006D164_6DD64 - 24 < destination_y) {
			tile_height = (D_8006D164_6DD64 - destination_y + 1);
			if (tile_height < 0) {
				tile_height++;
			}
			tile_height /= 2;
		} else {
			// Otherwise, use the standard full height for a tile.
			tile_height = 12;
		}

		// Reset the horizontal source coordinate at the start of each new row.
		source_x = (s32)start_x;

		// Loop over the screen horizontally, drawing each tile in the current row.
		for (destination_x = D_8006D158_6DD58; destination_x < D_8006D160_6DD60; destination_x += 326) {
			// If we're near the right edge, calculate a partial tile width.
			if (D_8006D160_6DD60 - 328 < destination_x) {
				tile_width = (D_8006D160_6DD60 - destination_x + 1);
				if (tile_width < 0) {
					tile_width++;
				}
				tile_width /= 2;
			} else {
				// Otherwise, use the standard full width for a tile.
				tile_width = 164;
			}

			// Draw the calculated tile.
			func_80021B98_22798(texture_data, source_x, source_y, tile_width, tile_height, destination_x, destination_y);

			// Advance the source X coordinate for the next tile in this row.
			source_x += (tile_width - 1);
		}

		// Advance the source Y coordinate for the next row of tiles.
		start_y += 10.0f;
	}
}

RECOMP_PATCH void func_80021B98_22798(u8 *texture_data, f32 source_x, f32 source_y, s32 tile_width, s32 tile_height, u32 destination_x, u32 destination_y)
{
	// Check if the destination rectangle wraps around the screen edge
	if (640 < (destination_x + tile_width * 2)) {
		// First part: Draw the wrapped portion (right side of tile -> left side of screen).
		s32 wrap_width;
		s32 right_width;

		wrap_width = (destination_x + tile_width * 2) - 640;
		right_width = tile_width * 2 - wrap_width;

		// Load the right part of the source tile.
		gDPSetTextureImage(D_8015C5CC_15D1CC++, G_IM_FMT_CI, G_IM_SIZ_8b, 640, texture_data);
		gDPSetTile(D_8015C5CC_15D1CC++, G_IM_FMT_CI, G_IM_SIZ_8b, (right_width / 2 + 7) / 8, 0, G_TX_LOADTILE, 8, G_TX_CLAMP, 0, 0, G_TX_NOMIRROR, 0, 0);
		gDPLoadSync(D_8015C5CC_15D1CC++);
		gDPLoadTile(D_8015C5CC_15D1CC++, G_TX_LOADTILE, 
			(s32)source_x << 2, 
			(s32)source_y << 2, 
			((s32)source_x + right_width / 2 - 1) << 2, 
			((s32)source_y + tile_height - 1) << 2);
		gDPPipeSync(D_8015C5CC_15D1CC++);
		gDPSetTile(D_8015C5CC_15D1CC++, G_IM_FMT_CI, G_IM_SIZ_8b, (right_width / 2 + 7) / 8, 0, G_TX_RENDERTILE, 8, G_TX_CLAMP, 0, 0, G_TX_NOMIRROR, 0, 0);
		gDPSetTileSize(D_8015C5CC_15D1CC++, G_TX_RENDERTILE, 
			(s32)source_x << 2, 
			(s32)source_y << 2, 
			((s32)source_x + right_width / 2 - 1) << 2, 
			((s32)source_y + tile_height - 1) << 2);

		// Draw the rectangle on the right edge of the screen.
		gSPTextureRectangle(D_8015C5CC_15D1CC++, 
			destination_x << 2, 
			destination_y << 2, 
			(destination_x + right_width - 1) << 2, 
			(destination_y + tile_height * 2 - 1) << 2, 
			G_TX_RENDERTILE, 
			(s32)(source_x * 32.0f), 
			(s32)(source_y * 32.0f), 
			1 << 9, // dsdx = 0.5
			1 << 9  // dtdy = 0.5
		);

		// Second part: Draw the non-wrapped portion (left side of tile -> right side of screen).

		// Load the left part of the source tile.
		gDPSetTextureImage(D_8015C5CC_15D1CC++, G_IM_FMT_CI, G_IM_SIZ_8b, 640, texture_data);
		gDPSetTile(D_8015C5CC_15D1CC++, G_IM_FMT_CI, G_IM_SIZ_8b, (wrap_width / 2 + 7) / 8, 0, G_TX_LOADTILE, 8, G_TX_CLAMP, 0, 0, G_TX_NOMIRROR, 0, 0);
		gDPLoadSync(D_8015C5CC_15D1CC++);
		gDPLoadTile(D_8015C5CC_15D1CC++, G_TX_LOADTILE, 
			((s32)source_x + right_width / 2) << 2, 
			(s32)source_y << 2, 
			((s32)source_x + tile_width - 1) << 2, 
			((s32)source_y + tile_height - 1) << 2);
		gDPPipeSync(D_8015C5CC_15D1CC++);
		gDPSetTile(D_8015C5CC_15D1CC++, G_IM_FMT_CI, G_IM_SIZ_8b, (wrap_width / 2 + 7) / 8, 0, G_TX_RENDERTILE, 8, G_TX_CLAMP, 0, 0, G_TX_NOMIRROR, 0, 0);
		gDPSetTileSize(D_8015C5CC_15D1CC++, G_TX_RENDERTILE, 
			((s32)source_x + right_width / 2) << 2, 
			(s32)source_y << 2, 
			((s32)source_x + tile_width - 1) << 2, 
			((s32)source_y + tile_height - 1) << 2);

		// Draw the rectangle on the left edge of the screen.
		gSPTextureRectangle(D_8015C5CC_15D1CC++, 
			0, 
			destination_y << 2, 
			(wrap_width - 1) << 2, 
			(destination_y + tile_height * 2 - 1) << 2, 
			G_TX_RENDERTILE, 
			(s32)((source_x + right_width / 2) * 32.0f), 
			(s32)(source_y * 32.0f), 
			1 << 9, // dsdx = 0.5
			1 << 9  // dtdy = 0.5
		);
	} else {
		// Simple case: Draw the entire tile in one go.

		// Load the tile from the source texture.
		gDPLoadTextureTile(
			D_8015C5CC_15D1CC++, 
			texture_data, 
			G_IM_FMT_CI, 
			G_IM_SIZ_8b, 
			640, 0, 
			(s32)source_x, (s32)source_y, 
			((s32)source_x + tile_width - 1), ((s32)source_y + tile_height - 1), 
			0, 
			G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, 
			G_TX_NOMASK, G_TX_NOMASK, 
			G_TX_NOLOD, G_TX_NOLOD
		);

		// Draw the textured rectangle, scaled 2x.
		gSPTextureRectangle(D_8015C5CC_15D1CC++, 
			destination_x << 2, 
			destination_y << 2, 
			(destination_x + tile_width * 2 - 1) << 2, 
			(destination_y + tile_height * 2 - 1) << 2, 
			G_TX_RENDERTILE, 
			(s32)(source_x * 32.0f), 
			(s32)(source_y * 32.0f), 
			1 << 9, // dsdx = 0.5 (0x200 in S5.10)
			1 << 9  // dtdy = 0.5 (0x200 in S5.10)
		);
	}
}

void func_80022EC0_23AC0(BackgroundGraphicsNode *node);
void func_80022348_22F48(s32 destination_x, s32 destination_y, s32 source_x, s32 source_y, s32 width, s32 height, BackgroundGraphicsNode *node);

RECOMP_PATCH void func_80022214_22E14(BackgroundGraphicsNode *node) 
{
	s32 destination_x;
	s32 destination_y_start;
	s32 source_x;
	s32 source_y;
	s32 tile_width;
	s32 total_height;
	s32 bottom_edge;
	s32 maximum_tile_height;
	s32 current_destination_y;

	// Initialize rendering coordinates from the node properties.
	destination_x = (s32)node->upper_left_corner_x;
	destination_y_start = (s32)node->upper_left_corner_y;
	source_x = (s32)node->start_x;
	source_y = (s32)node->start_y;
	tile_width = (s32)node->rectangle_width;
	total_height = (s32)node->rectangle_height;

	bottom_edge = destination_y_start + total_height;

	// This calculates the maximum height of a tile that can be loaded at once,
	// based on its width.
	maximum_tile_height = (s32)(2048.0f / tile_width);

	// This function sets up texture and rendering modes.
	func_80022EC0_23AC0(node);

	// Loop vertically from the top of the rectangle to the bottom, drawing one tile per iteration.
	for (current_destination_y = destination_y_start; current_destination_y < bottom_edge; ) {
		s32 current_tile_height;

		// Determine the height of the tile for this iteration.
		current_tile_height = maximum_tile_height;

		// If drawing a full-height tile would go past the bottom edge,
		// clamp the tile height to the remaining vertical space.
		if (current_destination_y + current_tile_height > bottom_edge) {
			current_tile_height = bottom_edge - current_destination_y;
		}

		// Draw the calculated tile segment.
		func_80022348_22F48(destination_x, current_destination_y, source_x, source_y, tile_width, current_tile_height, node);

		// Advance the destination and source Y coordinates for the next tile.
		current_destination_y += current_tile_height;
		source_y += current_tile_height;
	}
}

void func_80022500_23100(u32 destination_x, u32 destination_y, s32 source_x, s32 source_y, s32 rectangle_width, s32 rectangle_height, BackgroundGraphicsNode *node);

RECOMP_PATCH void func_80022348_22F48(s32 destination_x, s32 destination_y, s32 source_x, s32 source_y, s32 width, s32 height, BackgroundGraphicsNode *node)
{
	s32 clip_x0;
	s32 clip_y0;
	s32 clip_x1;
	s32 clip_y1;

	// This ensures that source coordinates always fall within the texture's bounds.
	if (node->texture_width > 0) {
		source_x %= node->texture_width;
		if (source_x < 0) {
			source_x += node->texture_width;
		}
	}

	if (node->texture_height > 0) {
		source_y %= node->texture_height;
		if (source_y < 0) {
			source_y += node->texture_height;
		}
	}

	// Define the clipping rectangle (also known as a scissor rectangle).
	clip_x0 = node->unk_2e;
	clip_y0 = node->unk_30;
	clip_x1 = node->unk_32;
	clip_y1 = node->unk_34;

	// --- Perform Clipping ---

	// Clip against the left edge.
	if (destination_x < clip_x0) {
		s32 difference = clip_x0 - destination_x;
		width -= difference;
		source_x += difference;
		destination_x = clip_x0;
	}

	// Clip against the top edge.
	if (destination_y < clip_y0) {
		s32 difference = clip_y0 - destination_y;
		height -= difference;
		source_y += difference;
		destination_y = clip_y0;
	}

	// Clip against the right edge.
	if (destination_x + width > clip_x1) {
		width = clip_x1 - destination_x;
	}

	// Clip against the bottom edge.
	if (destination_y + height > clip_y1) {
		height = clip_y1 - destination_y;
	}

	// After clipping, if the tile has no width or height, there's nothing to draw.
	if (width > 0 && height > 0) {
		// Call the actual low-level drawing function with the wrapped and clipped parameters.
		func_80022500_23100(destination_x, destination_y, source_x, source_y, width, height, node);
	}
}

RECOMP_PATCH void func_80022500_23100(u32 destination_x, u32 destination_y, s32 source_x, s32 source_y, s32 rectangle_width, s32 rectangle_height, BackgroundGraphicsNode *node)
{
	s32 image_format;

	if (node->flags & (1 << 0)) {
		image_format = G_IM_FMT_CI;
	} else if (node->flags & (1 << 1)) {
		image_format = G_IM_FMT_IA;
	} else {
		image_format = G_IM_FMT_RGBA;
	}

	if (node->flags & (1 << 2)) {
		gDPLoadTextureTile_4b(
			D_8015C5CC_15D1CC++, 
			node->texture_data, 
			G_IM_FMT_CI,
			node->texture_width, node->texture_height, 
			source_x, source_y, 
			(source_x + rectangle_width - 1), (source_y + rectangle_height - 1), 
			0, 
			G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, 
			G_TX_NOMASK, G_TX_NOMASK, 
			G_TX_NOLOD, G_TX_NOLOD
		);
	} else if (node->flags & (1 << 3)) {
		gDPLoadTextureTile(
			D_8015C5CC_15D1CC++, 
			node->texture_data, 
			image_format, 
			G_IM_SIZ_8b, 
			node->texture_width, node->texture_height, 
			source_x, source_y, 
			(source_x + rectangle_width - 1), (source_y + rectangle_height - 1), 
			0, 
			G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, 
			G_TX_NOMASK, G_TX_NOMASK, 
			G_TX_NOLOD, G_TX_NOLOD
		);
	} else {
		gDPLoadTextureTile(
			D_8015C5CC_15D1CC++, 
			node->texture_data, 
			G_IM_FMT_RGBA, 
			G_IM_SIZ_16b, 
			node->texture_width, node->texture_height, 
			source_x, source_y, 
			(source_x + rectangle_width - 1), (source_y + rectangle_height - 1), 
			0, 
			G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, 
			G_TX_NOMASK, G_TX_NOMASK, 
			G_TX_NOLOD, G_TX_NOLOD
		);
	}

	gSPTextureRectangle(
		D_8015C5CC_15D1CC++, 
		destination_x << 2, destination_y << 2, 
		(destination_x + rectangle_width) << 2, (destination_y + rectangle_height) << 2, 
		G_TX_RENDERTILE, 
		source_x << 5, source_y << 5, 
		1 << 10, 1 << 10
	);
}

void func_80022DF4_239F4(u32 destination_x, u32 destination_y, s32 source_x, s32 source_y, s32 rectangle_width, s32 rectangle_height, BackgroundGraphicsNode *node);

RECOMP_PATCH void func_80022A74_23674(BackgroundGraphicsNode *node)
{
	s32 dest_x = (s32)node->upper_left_corner_x;
	s32 dest_y = (s32)node->upper_left_corner_y;
	s32 src_x = (s32)node->start_x;
	s32 src_y = (s32)node->start_y;
	s32 width = (s32)node->rectangle_width;
	s32 height = (s32)node->rectangle_height;
	s32 max_tile_height;
	s32 height_remaining;
	s32 current_dest_y;
	s32 current_src_y;

	// The original code would trap on division by zero. We prevent it.
	if (width <= 0 || node->texture_width <= 0 || node->texture_height <= 0) {
		return;
	}

	// Calculate the maximum height of a tile that can be loaded into TMEM (2KB in this case).
	max_tile_height = 2048 / width;
	if (max_tile_height <= 0) {
		max_tile_height = 1; // Ensure at least one row can be processed.
	}

	// Wrap source X coordinate for tiling textures.
	src_x %= node->texture_width;
	if (src_x < 0) {
		src_x += node->texture_width;
	}

	// Wrap source Y coordinate for tiling textures.
	src_y %= node->texture_height;
	if (src_y < 0) {
		src_y += node->texture_height;
	}

	// Set up the RDP rendering state.
	func_80022EC0_23AC0(node);

	height_remaining = height;
	current_dest_y = dest_y;
	current_src_y = src_y;

	// Loop until the entire rectangle height has been drawn.
	while (height_remaining > 0) {
		// Determine the height of the current tile. It's the minimum of:
		// 1. The total height left to draw.
		// 2. The maximum tile height allowed by TMEM.
		// 3. The vertical distance from the current source Y to the texture's bottom edge.
		s32 tile_height = height_remaining;
		if (tile_height > max_tile_height) {
			tile_height = max_tile_height;
		}

		s32 height_before_wrap = node->texture_height - current_src_y;
		if (tile_height > height_before_wrap) {
			tile_height = height_before_wrap;
		}

		// If we are at the texture edge and can't draw anything, wrap src_y and restart the loop.
		if (tile_height <= 0) {
			current_src_y = 0;
			continue;
		}

		// Draw the tile. func_80022DF4_239F4 handles horizontal (X-axis) wrapping.
		func_80022DF4_239F4(dest_x, current_dest_y, src_x, current_src_y, width, tile_height, node);

		// Update state for the next iteration.
		height_remaining -= tile_height;
		current_dest_y += tile_height;
		current_src_y += tile_height;

		// If we've reached the bottom of the source texture, wrap back to the top.
		if (current_src_y >= node->texture_height) {
			current_src_y = 0;
		}
	}
}


RECOMP_PATCH void func_80022DF4_239F4(u32 destination_x, u32 destination_y, s32 source_x, s32 source_y, s32 rectangle_width, s32 rectangle_height, BackgroundGraphicsNode *node)
{
	if (source_x + rectangle_width > node->texture_width) {
		s32 first_part_width;
		s32 second_part_width;

		first_part_width = node->texture_width - source_x;
		func_80022500_23100(destination_x, destination_y, source_x, source_y, first_part_width, rectangle_height, node);

		second_part_width = rectangle_width - first_part_width;
		func_80022500_23100(destination_x + first_part_width, destination_y, 0, source_y, second_part_width, rectangle_height, node);
	} else {
		func_80022500_23100(destination_x, destination_y, source_x, source_y, rectangle_width, rectangle_height, node);
	}
}

extern Gfx D_8006D0F0_6DCF0[];

#define G_CC_CUSTOM_1 0, 0, 0, TEXEL0, 0, 0, 0, PRIMITIVE
#define G_CC_CUSTOM_2 0, 0, 0, TEXEL0, TEXEL0, 0, PRIMITIVE, 0

RECOMP_PATCH void func_80022EC0_23AC0(BackgroundGraphicsNode *node)
{
	gSPDisplayList(D_8015C5CC_15D1CC++, &D_8006D0F0_6DCF0);

	if (node->flags & (1 << 4)) {
		gDPSetPrimColor(D_8015C5CC_15D1CC++, 0, 0, 255, 255, 255, node->alpha);
		gDPSetRenderMode(D_8015C5CC_15D1CC++, G_RM_XLU_SURF, G_RM_XLU_SURF2);

		if (node->flags & (1 << 5)) {
			gDPSetCombineMode(D_8015C5CC_15D1CC++, G_CC_CUSTOM_2, G_CC_CUSTOM_2);
			gDPSetAlphaCompare(D_8015C5CC_15D1CC++, G_AC_THRESHOLD);
			gDPSetBlendColor(D_8015C5CC_15D1CC++, 0, 0, 0, 1);
		} else {
			gDPSetCombineMode(D_8015C5CC_15D1CC++, G_CC_CUSTOM_1, G_CC_CUSTOM_1);
		}
	} else {
		if (node->flags & (1 << 5)) {
			gDPSetRenderMode(D_8015C5CC_15D1CC++, G_RM_TEX_EDGE, G_RM_TEX_EDGE2);
			gDPSetAlphaCompare(D_8015C5CC_15D1CC++, G_AC_THRESHOLD);
			gDPSetBlendColor(D_8015C5CC_15D1CC++, 0, 0, 0, 1);
		}
	}

	// @recomp Point filtering removed (looks better in basically all cases, especially when stretched). G_TF_BILERP is set in previous gSPDisplayList call.
	// gDPSetTextureFilter(D_8015C5CC_15D1CC++, G_TF_POINT);

	if (node->flags & (1 << 0)) {
		if (node->flags & (1 << 3)) {
			gDPSetTextureImage(D_8015C5CC_15D1CC++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, node->palette_data);
			gDPTileSync(D_8015C5CC_15D1CC++);
			gDPSetTile(D_8015C5CC_15D1CC++, G_IM_FMT_RGBA, G_IM_SIZ_4b, 0, 256, G_TX_LOADTILE, 0, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD);
			gDPLoadSync(D_8015C5CC_15D1CC++);
			gDPLoadTLUTCmd(D_8015C5CC_15D1CC++, G_TX_LOADTILE, 255);
			gDPPipeSync(D_8015C5CC_15D1CC++);
		} else {
			gDPSetTextureImage(D_8015C5CC_15D1CC++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, node->palette_data);
			gDPTileSync(D_8015C5CC_15D1CC++);
			gDPSetTile(D_8015C5CC_15D1CC++, G_IM_FMT_RGBA, G_IM_SIZ_4b, 0, 256, G_TX_LOADTILE, 0, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD, G_TX_NOMIRROR | G_TX_WRAP, G_TX_NOMASK, G_TX_NOLOD);
			gDPLoadSync(D_8015C5CC_15D1CC++);
			gDPLoadTLUTCmd(D_8015C5CC_15D1CC++, G_TX_LOADTILE, 15);
			gDPPipeSync(D_8015C5CC_15D1CC++);
		}
	} else {
		gDPSetTextureLUT(D_8015C5CC_15D1CC++, G_TT_NONE);
	}
}