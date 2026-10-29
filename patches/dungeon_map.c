#include "patches.h"
#include "patch_helpers.h"
#include "variables.h"

#ifdef GOEMON_3DS
#include "rt64_3ds_gbi.h"

DECLARE_FUNC(s32, recomp_get_hud_on_bottom);

// @recomp The 3DS's touch-screen dungeon map. In play the game has no map in
// its castles: the pause screen's map page (the pause overlay, file 17) shows
// the floor the player is on, from file 0x4D2, which holds the five castles'
// floor plans as models. A floor is a list of rooms, each a tree of nodes
// whose display lists draw flat quads in a prim colour (blue rooms, yellow
// doors, red marks) and whose scale, Y turn and position are packed in 16
// bits (func_80019ED8_1AAD8 makes their matrices). Rooms the player has not
// been in are left out, and the one they are in pulses from blue to red.
// With the HUD on the touch screen this draws that floor there top-down,
// north up, in the column left of the touch buttons, when the pause screen
// would show it (in a castle whose map the player has) and the status bar
// is up.

#define FILE_DUNGEON_MAPS   0x4D2
#define DUNGEON_MAPS_SIZE   0x335B0                     // as loaded (the file table's size)
#define DUNGEONS            5
#define MAX_FLOORS          4
#define MAX_ROOMS           32
#define MAX_NODES           128                         // a floor has up to 80
#define MAX_DEPTH           8                           // ... nested up to 7 deep

#define MAP_KIND            (*(u8 *)0x800C7AA4)         // 0: the pause screen shows a dungeon's map
#define DUNGEON             (*(s32 *)0x800C7AA8)
#define DUNGEON_FLOOR       (*(u16 *)0x800C7AAC)
#define DUNGEON_ROOM        (*(u16 *)0x800C7AAE)
#define DUNGEON_HAS_MAP     ((s32 *)0x8015C874)         // by dungeon: its map can be seen
#define DUNGEON_MARKED      ((s32 *)0x8015C888)         // by dungeon: its marked room is shown
#define DUNGEON_VISITED(d, f) ((u8 *)0x8015C740 + 40 * (d) + 8 * (f))   // a bit per room
#define MAP_EVENT_A         (*(u8 *)0x800C7AE2)         // set: no map now (talking, cutscenes)
#define MAP_EVENT_B         (*(u16 *)0x800C7AD6)

// Where the map fits on the touch screen: left of the touch buttons' area
// (main_3ds.cpp's touch_area_hud, x 152 on) and above the status bar (y 185 on).
#define MAP_LEFT            8.0f
#define MAP_TOP             8.0f
#define MAP_RIGHT           148.0f
#define MAP_BOTTOM          180.0f

// F3DEX's commands in the file's lists.
#define OP_MTX              0x01
#define OP_MOVEMEM          0x03
#define OP_VTX              0x04
#define OP_DL               0x06
#define OP_ENDDL            0xB8
#define OP_SETGEOMETRYMODE  0xB7
#define OP_SETPRIMCOLOR     0xFA
#define OP_SETTIMG          0xFD
#define MAX_LIST            512                         // commands walked in one list

#define ROOM_BLUE           0x0000FFFF

// The pause overlay's table (at 0x80217020): per dungeon, its floors' room
// lists in file 0x4D2 and their room counts, and the room it marks.
typedef struct {
    u8 floors;
    u8 marked_floor;
    u8 marked_room;
    struct {
        u16 offset;
        u8 rooms;
    } floor[MAX_FLOORS];
} DungeonMaps;

static const DungeonMaps dungeon_maps[DUNGEONS] = {
    { 3, 2, 0, { { 0x0324, 10 }, { 0x070C, 12 }, { 0x080C, 3 } } },
    { 4, 3, 0, { { 0x1010, 1 }, { 0x0C04, 13 }, { 0x0F88, 11 }, { 0x0FD8, 1 } } },
    { 4, 3, 2, { { 0x1300, 6 }, { 0x1710, 9 }, { 0x1A60, 7 }, { 0x1B68, 3 } } },
    { 4, 3, 1, { { 0x1BB4, 1 }, { 0x1E3C, 9 }, { 0x2534, 14 }, { 0x25EC, 2 } } },
    { 2, 0, 4, { { 0x3404, 5 }, { 0x3210, 21 } } },
};

// A floor: its room count (bits 8..15), its curves (segment 8, 0: none) and its rooms' nodes.
typedef struct {
    u32 flags;
    u32 curves;
    u32 rooms[1];
} MapFloor;

// A node. Its values: scale x, y, z (/256), turn x, y, z (1024 a turn), position x, y, z (/32).
typedef struct {
    u32 dl;                 // segment 8
    s8 child;               // its first child, in nodes from it (0: none)
    s8 next;                // its next sibling, likewise
    u16 value[9];
} MapNode;

// A node's place on the floor: x' = a x + c z + x0, z' = b x + d z + z0.
typedef struct {
    f32 a, b, c, d, x0, z0;
} Affine;

typedef struct {
    Gfx *dl;
    Affine at;
    u8 room;
    u8 child;               // drawn after the rooms (doors over the gaps)
} FloorNode;

extern f32 func_80003F30_4B30(s32 angle, f32 *cos_out);    // sine and cosine, 1024 a turn

static u8 maps_file[DUNGEON_MAPS_SIZE] __attribute__((aligned(16)));
static s32 maps_state;      // 0: not loaded, 1: loaded and its lists made drawable here, -1: unusable

static FloorNode floor_nodes[MAX_NODES];
static s32 floor_node_count;
static u32 *room_blue[MAX_ROOMS];                           // each room's blue, where it pulses
static f32 floor_scale, floor_x0, floor_z0;                 // the floor fitted to the map's area

static Gfx map_lists[2][40 + 2 * MAX_NODES];
static f32 map_matrices[2][MAX_NODES][16];
static s32 map_buffer;
static Gfx *map_list;
static Vp map_viewport = { { { SCREEN_WIDTH * 2, SCREEN_HEIGHT * 2, G_MAXZ / 2, 0 }, { SCREEN_WIDTH * 2, SCREEN_HEIGHT * 2, G_MAXZ / 2, 0 } } };
// The touch screen's pixels to clip space (the node's height as a sliver of depth).
static f32 map_projection[16] = {
    2.0f / SCREEN_WIDTH, 0.0f, 0.0f, 0.0f,
    0.0f, -2.0f / SCREEN_HEIGHT, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f / 1024.0f, 0.0f,
    -1.0f, 1.0f, 0.0f, 1.0f,
};

static s32 shown_dungeon = -1;
static s32 shown_floor = -1;
static s32 shown_marked;
static u8 shown_visited[8];
static u32 *pulse_word;
static s32 pulse_step;
static s32 pulse_dir = 1;

static void *maps_addr(u32 segmented) {
    return maps_file + (segmented & 0xFFFFFF);
}

static s32 maps_holds(const void *p, u32 size) {
    return ((const u8 *)p >= maps_file) && ((const u8 *)p + size <= maps_file + DUNGEON_MAPS_SIZE);
}

// func_8001B2C8_1BEC8 at frame 0: a packed curve's first value.
static s32 curve_first(const u8 *c) {
    u8 b = c[0];
    u8 hi = 0;

    if (b >= 0xF0) {
        return (s16)((c[1] << 8) | c[2]);
    }
    if (b < 0x78) {
        if (b >= 0x60) {
            hi = *++c;
        }
        else if (b >= 0x40) {
            hi = 1;
        }
        else if (b >= 0x20) {
            hi = 0xFF;
        }
    }
    else {
        if (b >= 0xD8) {
            hi = *++c;
        }
        else if (b >= 0xB8) {
            hi = 1;
        }
        else if (b >= 0x98) {
            hi = 0xFF;
        }
    }
    return (s16)((hi << 8) + c[1]);
}

// func_8001AABC_1B6BC and its kin: a value in the code (0xA001..0xBFFF,
// 0xB000 being 0) or a curve's (0x8000 | i: curve i held still; i: curve i
// played, of which frame 0 here).
static s32 node_value(u16 code, const u8 *curves) {
    u16 v = code & 0xBFFF;

    if (v >= 0xA001) {
        return (s16)(v - 0xB000);
    }
    if ((curves == NULL) || !maps_holds(curves + (v & 0x7FFF), 4)) {
        return 0;
    }
    return curve_first(curves + (v & 0x7FFF));
}

// func_80019ED8_1AAD8's matrix for a node turned about Y alone (no node here
// turns about X or Z) and left flat (its height dropped).
static void node_affine(const MapNode *node, const u8 *curves, Affine *out) {
    const f32 sx = (f32)(u16)node_value(node->value[0], curves) / 256.0f;
    const f32 sz = (f32)(u16)node_value(node->value[2], curves) / 256.0f;
    f32 c;
    const f32 s = func_80003F30_4B30(node_value(node->value[4], curves) & 0x3FF, &c);

    out->a = sx * c;
    out->b = -sx * s;
    out->c = sz * s;
    out->d = sz * c;
    out->x0 = (f32)node_value(node->value[6], curves) / 32.0f;
    out->z0 = (f32)node_value(node->value[8], curves) / 32.0f;
}

// m, then p.
static void affine_then(const Affine *m, const Affine *p, Affine *out) {
    Affine r;

    r.a = m->a * p->a + m->b * p->c;
    r.b = m->a * p->b + m->b * p->d;
    r.c = m->c * p->a + m->d * p->c;
    r.d = m->c * p->b + m->d * p->d;
    r.x0 = m->x0 * p->a + m->z0 * p->c + p->x0;
    r.z0 = m->x0 * p->b + m->z0 * p->d + p->z0;
    *out = r;
}

// Once, after loading: the lists' segment 8 addresses made the file's own, and
// no culling (seen from above, top-down, whichever way they wind).
static void relocate_list(Gfx *g, s32 depth) {
    s32 i;

    for (i = 0; (i < MAX_LIST) && maps_holds(g, sizeof(Gfx)); i++, g++) {
        const u32 op = g->words.w0 >> 24;

        if (op == OP_ENDDL) {
            return;
        }
        if (((op == OP_VTX) || (op == OP_DL) || (op == OP_SETTIMG) || (op == OP_MOVEMEM) || (op == OP_MTX)) &&
            ((g->words.w1 >> 24) == 0x08)) {
            g->words.w1 = (u32)maps_addr(g->words.w1);
        }
        if (op == OP_SETGEOMETRYMODE) {
            g->words.w1 &= ~G_CULL_BOTH;
        }
        if (op == OP_DL) {
            if ((depth < MAX_DEPTH) && maps_holds((void *)g->words.w1, sizeof(Gfx))) {
                relocate_list((Gfx *)g->words.w1, depth + 1);
            }
            if (((g->words.w0 >> 16) & 0xFF) == G_DL_NOPUSH) {
                return;
            }
        }
    }
}

static MapNode *node_at(MapNode *node, s32 step) {
    MapNode *n = node + step;
    return maps_holds(n, sizeof(MapNode)) ? n : NULL;
}

static void relocate_node(MapNode *node, s32 depth) {
    MapNode *child;

    if ((depth >= MAX_DEPTH) || ((node->dl >> 24) != 0x08)) {
        return;
    }
    relocate_list((Gfx *)maps_addr(node->dl), 0);
    for (child = (node->child != 0) ? node_at(node, node->child) : NULL; child != NULL;
         child = (child->next != 0) ? node_at(child, child->next) : NULL) {
        relocate_node(child, depth + 1);
    }
}

static MapFloor *floor_at(s32 dungeon, s32 floor) {
    return (MapFloor *)(maps_file + dungeon_maps[dungeon].floor[floor].offset);
}

static MapNode *room_node(MapFloor *f, s32 room) {
    MapNode *node = (MapNode *)maps_addr(f->rooms[room]);
    return ((f->rooms[room] >> 24) == 0x08) && maps_holds(node, sizeof(MapNode)) ? node : NULL;
}

// The file into patch RAM (the game's loader, as the pause screen loads it),
// its lists made drawable from there. False until it is.
static s32 maps_load(void) {
    s32 d, f, r;

    if (maps_state != 0) {
        return maps_state > 0;
    }
    maps_state = -1;
    func_80001C00_2800(FILE_DUNGEON_MAPS, maps_file);
    for (d = 0; d < DUNGEONS; d++) {
        for (f = 0; f < dungeon_maps[d].floors; f++) {
            MapFloor *floor = floor_at(d, f);
            if (((floor->flags >> 8) & 0xFF) != dungeon_maps[d].floor[f].rooms) {
                recomp_printf("Dungeon map: file 0x%X is not the one expected (dungeon %d floor %d)\n", FILE_DUNGEON_MAPS, d, f);
                return FALSE;
            }
        }
    }
    for (d = 0; d < DUNGEONS; d++) {
        for (f = 0; f < dungeon_maps[d].floors; f++) {
            MapFloor *floor = floor_at(d, f);
            for (r = 0; r < dungeon_maps[d].floor[f].rooms; r++) {
                MapNode *node = room_node(floor, r);
                if (node != NULL) {
                    relocate_node(node, 0);
                }
            }
        }
    }
    maps_state = 1;
    return TRUE;
}

static void add_node(MapNode *node, const u8 *curves, const Affine *parent, s32 room, s32 depth) {
    MapNode *child;
    FloorNode *out;

    if ((depth >= MAX_DEPTH) || (floor_node_count >= MAX_NODES)) {
        return;
    }
    if (((node->dl >> 24) != 0x08) || !maps_holds(maps_addr(node->dl), sizeof(Gfx))) {
        return;
    }
    out = &floor_nodes[floor_node_count++];
    node_affine(node, curves, &out->at);
    if (parent != NULL) {
        affine_then(&out->at, parent, &out->at);
    }
    out->dl = (Gfx *)maps_addr(node->dl);
    out->room = room;
    out->child = depth > 0;
    for (child = (node->child != 0) ? node_at(node, node->child) : NULL; child != NULL;
         child = (child->next != 0) ? node_at(child, child->next) : NULL) {
        add_node(child, curves, &out->at, room, depth + 1);
    }
}

// Every vertex a node's list loads, on the floor, into the bounds.
static void list_bounds(const Gfx *g, const Affine *at, f32 *bounds, s32 depth) {
    s32 i;

    for (i = 0; (i < MAX_LIST) && maps_holds(g, sizeof(Gfx)); i++, g++) {
        const u32 op = g->words.w0 >> 24;

        if (op == OP_ENDDL) {
            return;
        }
        if (op == OP_VTX) {
            const Vtx *v = (const Vtx *)g->words.w1;
            s32 n = (g->words.w0 >> 10) & 0x3F;
            s32 k;
            if (!maps_holds(v, n * sizeof(Vtx))) {
                continue;
            }
            for (k = 0; k < n; k++) {
                const f32 x = v[k].v.ob[0];
                const f32 z = v[k].v.ob[2];
                const f32 fx = at->a * x + at->c * z + at->x0;
                const f32 fz = at->b * x + at->d * z + at->z0;
                if (fx < bounds[0]) bounds[0] = fx;
                if (fx > bounds[1]) bounds[1] = fx;
                if (fz < bounds[2]) bounds[2] = fz;
                if (fz > bounds[3]) bounds[3] = fz;
            }
        }
        else if (op == OP_DL) {
            if ((depth < MAX_DEPTH) && maps_holds((void *)g->words.w1, sizeof(Gfx))) {
                list_bounds((const Gfx *)g->words.w1, at, bounds, depth + 1);
            }
            if (((g->words.w0 >> 16) & 0xFF) == G_DL_NOPUSH) {
                return;
            }
        }
    }
}

// The first {G_SETPRIMCOLOR, blue} of the room's list (func_80216794_675744).
static u32 *find_blue(const Gfx *g) {
    s32 i;

    for (i = 0; (i < MAX_LIST) && maps_holds(g, sizeof(Gfx)); i++, g++) {
        if ((g->words.w0 == ((u32)OP_SETPRIMCOLOR << 24)) && (g->words.w1 == ROOM_BLUE)) {
            return (u32 *)&g->words.w1;
        }
        if ((g->words.w0 >> 24) == OP_ENDDL) {
            break;
        }
    }
    return NULL;
}

static void pulse_stop(void) {
    if (pulse_word != NULL) {
        *pulse_word = ROOM_BLUE;
        pulse_word = NULL;
    }
}

// The floor's nodes, where they are, and the scale that fits them all
// (visited or not, so the map keeps its size as rooms are found).
static void floor_setup(s32 dungeon, s32 floor) {
    MapFloor *f = floor_at(dungeon, floor);
    const u8 *curves = ((f->curves >> 24) == 0x08) ? (const u8 *)maps_addr(f->curves) : NULL;
    const s32 rooms = dungeon_maps[dungeon].floor[floor].rooms;
    f32 bounds[4] = { 1e9f, -1e9f, 1e9f, -1e9f };
    f32 sx, sz;
    s32 r, i;

    pulse_stop();
    floor_node_count = 0;
    for (r = 0; r < MAX_ROOMS; r++) {
        room_blue[r] = NULL;
    }
    for (r = 0; (r < rooms) && (r < MAX_ROOMS); r++) {
        MapNode *node = room_node(f, r);
        if (node == NULL) {
            continue;
        }
        add_node(node, curves, NULL, r, 0);
        if ((node->dl >> 24) == 0x08) {
            room_blue[r] = find_blue((const Gfx *)maps_addr(node->dl));
        }
    }
    for (i = 0; i < floor_node_count; i++) {
        list_bounds(floor_nodes[i].dl, &floor_nodes[i].at, bounds, 0);
    }
    if ((bounds[1] <= bounds[0]) || (bounds[3] <= bounds[2])) {
        floor_node_count = 0;
        return;
    }
    sx = (MAP_RIGHT - MAP_LEFT) / (bounds[1] - bounds[0]);
    sz = (MAP_BOTTOM - MAP_TOP) / (bounds[3] - bounds[2]);
    floor_scale = (sx < sz) ? sx : sz;
    floor_x0 = (MAP_LEFT + MAP_RIGHT) * 0.5f - (bounds[0] + bounds[1]) * 0.5f * floor_scale;
    floor_z0 = (MAP_TOP + MAP_BOTTOM) * 0.5f - (bounds[2] + bounds[3]) * 0.5f * floor_scale;
}

static s32 room_shown(s32 dungeon, s32 floor, s32 room) {
    const DungeonMaps *dm = &dungeon_maps[dungeon];

    if (DUNGEON_VISITED(dungeon, floor)[room >> 3] & (1 << (room & 7))) {
        return TRUE;
    }
    // The pause screen marks this room visited when it opens (func_802160A0_675050).
    return DUNGEON_MARKED[dungeon] && (floor == dm->marked_floor) && (room == dm->marked_room);
}

// The floor's shown rooms, top-down on the touch screen, in a list of its own
// (rebuilt when the rooms shown change), every state it changes put back.
static void build_list(s32 dungeon, s32 floor) {
    Gfx *gfx;
    s32 pass, i, n = 0;

    map_buffer ^= 1;
    gfx = map_lists[map_buffer];
    gDPPipeSync(gfx++);
    gEXPushViewport(gfx++);
    gEXPushScissor(gfx++);
    gEXPushProjectionMatrix(gfx++);
    gEXPushGeometryMode(gfx++);
    gEXPushOtherMode(gfx++);
    gEXPushCombineMode(gfx++);
    gEXPushPrimColor(gfx++);
    gSPViewport(gfx++, &map_viewport);
    gDPSetScissor(gfx++, G_SC_NON_INTERLACE, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    gDPSetCycleType(gfx++, G_CYC_1CYCLE);
    gDPSetRenderMode(gfx++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
    gDPSetAlphaCompare(gfx++, G_AC_NONE);
    gSPClearGeometryMode(gfx++, G_ZBUFFER | G_CULL_BOTH | G_FOG | G_LIGHTING | G_TEXTURE_GEN | G_TEXTURE_GEN_LINEAR);
    gSPSetGeometryMode(gfx++, G_SHADE);
    gSPTexture(gfx++, 0, 0, 0, G_TX_RENDERTILE, G_OFF);
    gEXMatrixGroupNoInterpolate(gfx++, G_EX_PUSH, G_MTX_PROJECTION, G_EX_EDIT_NONE);
    gEXMatrixFloat(gfx++, map_projection, G_MTX_PROJECTION | G_MTX_LOAD | G_MTX_NOPUSH);
    gEXMatrixGroupNoInterpolate(gfx++, G_EX_PUSH, G_MTX_MODELVIEW, G_EX_EDIT_NONE);
    // The rooms, then what hangs off them (doors over the gaps between rooms).
    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i < floor_node_count; i++) {
            const FloorNode *node = &floor_nodes[i];
            f32 *m = map_matrices[map_buffer][i];
            if ((node->child != pass) || !room_shown(dungeon, floor, node->room)) {
                continue;
            }
            m[0] = floor_scale * node->at.a;  m[1] = floor_scale * node->at.b;  m[2] = 0.0f;  m[3] = 0.0f;
            m[4] = 0.0f;                      m[5] = 0.0f;                      m[6] = 1.0f;  m[7] = 0.0f;
            m[8] = floor_scale * node->at.c;  m[9] = floor_scale * node->at.d;  m[10] = 0.0f; m[11] = 0.0f;
            m[12] = floor_x0 + floor_scale * node->at.x0;
            m[13] = floor_z0 + floor_scale * node->at.z0;
            m[14] = 0.0f;
            m[15] = 1.0f;
            gEXMatrixFloat(gfx++, m, G_MTX_MODELVIEW | G_MTX_LOAD | ((n == 0) ? G_MTX_PUSH : G_MTX_NOPUSH));
            gSPDisplayList(gfx++, node->dl);
            n++;
        }
    }
    if (n != 0) {
        gSPPopMatrix(gfx++, G_MTX_MODELVIEW);
    }
    gEXPopMatrixGroup(gfx++, G_MTX_MODELVIEW);
    gEXPopMatrixGroup(gfx++, G_MTX_PROJECTION);
    gEXPopPrimColor(gfx++);
    gEXPopCombineMode(gfx++);
    gEXPopOtherMode(gfx++);
    gEXPopGeometryMode(gfx++);
    gEXPopProjectionMatrix(gfx++);
    gEXPopScissor(gfx++);
    gEXPopViewport(gfx++);
    gSPEndDisplayList(gfx++);
    map_list = (n != 0) ? map_lists[map_buffer] : NULL;
}

// The pause screen's pulse (func_80214A34_6739E4): blue to red and back, 17
// steps each way.
static void pulse_room(s32 room) {
    u32 *word = ((room >= 0) && (room < MAX_ROOMS)) ? room_blue[room] : NULL;

    if (word != pulse_word) {
        pulse_stop();
        pulse_word = word;
        pulse_step = 0;
        pulse_dir = 1;
    }
    if (pulse_word == NULL) {
        return;
    }
    *pulse_word = ((u32)(pulse_step * 15) << 24) | ((u32)(0xFF - pulse_step * 15) << 8) | 0xFF;
    pulse_step += pulse_dir;
    if (pulse_step < 0) {
        pulse_step = 0;
        pulse_dir = 1;
    }
    else if (pulse_step >= 0x12) {
        pulse_step = 0x11;
        pulse_dir = -1;
    }
}

// From the HUD layer's draw (hud_screen.c), while the status bar is up: the
// map in a touch-screen span. True when it drew.
s32 dungeon_map_draw(void) {
    const s32 dungeon = DUNGEON;
    const s32 floor = DUNGEON_FLOOR;
    const s32 room = DUNGEON_ROOM;
    u8 *visited;
    s32 marked;
    s32 changed;
    s32 i;

    if (!recomp_get_hud_on_bottom() || (MAP_KIND != 0) || (dungeon < 0) || (dungeon >= DUNGEONS) ||
        (floor >= dungeon_maps[dungeon].floors) || !DUNGEON_HAS_MAP[dungeon] || (MAP_EVENT_A != 0) || (MAP_EVENT_B != 0) ||
        !maps_load()) {
        pulse_stop();
        return FALSE;
    }
    visited = DUNGEON_VISITED(dungeon, floor);
    marked = DUNGEON_MARKED[dungeon];
    if ((dungeon != shown_dungeon) || (floor != shown_floor)) {
        floor_setup(dungeon, floor);
        shown_dungeon = dungeon;
        shown_floor = floor;
        map_list = NULL;
    }
    changed = (map_list == NULL) || (marked != shown_marked);
    for (i = 0; i < (s32)sizeof(shown_visited); i++) {
        if (visited[i] != shown_visited[i]) {
            shown_visited[i] = visited[i];
            changed = TRUE;
        }
    }
    if (changed) {
        shown_marked = marked;
        build_list(dungeon, floor);
    }
    if (map_list == NULL) {
        pulse_stop();
        return FALSE;
    }
    pulse_room(room_shown(dungeon, floor, room) ? room : -1);
    gEX3DSBottomScreen(D_8015C5CC_15D1CC++, 0, 0);
    gSPDisplayList(D_8015C5CC_15D1CC++, map_list);
    gEX3DSTopScreen(D_8015C5CC_15D1CC++);
    return TRUE;
}

#endif
