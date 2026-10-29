#include "patches.h"
#include "patch_helpers.h"
#include "variables.h"

#ifdef GOEMON_3DS
#include "rt64_3ds_gbi.h"

DECLARE_FUNC(s32, recomp_get_hud_on_bottom);

// @recomp The 3DS's touch-screen HUD. The game draws its 2D objects in 12
// layers (func_80000AD8_16D8 calls this for each); layer 10 holds the field
// HUD: the status bar (weapon, health, ryo, lives, item box) at sub-priorities
// 1..4, and the map with its two markers at 16, 32 and 48. Dialogue, menus and
// the Impact battles draw elsewhere and stay on the top screen. This is the
// layer's draw as the game has it, with the bar and the map marked for the
// touch screen where they are, the map at the top left, the right left to the
// touch buttons (rt64-3ds draws marked spans there while the Game page's HUD
// row says so, on the top screen otherwise).

#define HUD_LAYER 10
#define LAYERS_OFFSET 0xC3394                       // the layers' groups in SYS_W
#define LAYER_SIZE 0x374

#define LAYERS_OFF (*(u8 *)0x801529C4)               // 1: no layer is drawn
#define SPRITE_SETUP_FN (*(u32 *)0x80167A60)         // the setup a sprite draw re-sends (func_800086F0_92F0)
#define SPRITE_CACHE_A (*(u16 *)0x80167A68)          // cached sprite state, 0xFFFF: none
#define SPRITE_CACHE_B (*(u16 *)0x80167A72)

extern void func_800086F0_92F0(void);
extern void func_80008920_9520(CLS_W *object);
extern s32 func_80016C44_17844(CLS_W *object);
extern void func_800215D0_221D0(void);
extern void func_80021740_22340(CLS_W *object);

enum { SPAN_NONE, SPAN_BAR, SPAN_MAP };

// The map fades in to an alpha of 160, see-through over the scene; on the
// touch screen, with nothing behind it, it is drawn opaque (its fades scaled
// to match).
#define MAP_ALPHA_MAX 0xA0
#define CLS_ALPHA(o) (((CLS_2D_W *)(o))->unk_10)

static s32 hud_span(CLS_W *object) {
    if (object->kind != 1) {
        return SPAN_NONE;
    }
    if (object->s_pri >= 1 && object->s_pri <= 4) {
        return SPAN_BAR;
    }
    if (object->s_pri == 16 || object->s_pri == 32 || object->s_pri == 48) {
        return SPAN_MAP;
    }
    return SPAN_NONE;
}

RECOMP_PATCH void func_800087C4_93C4(CLS_GROUP_W *group) {
    s32 hud = (u8 *)group == (u8 *)D_8015C5C8_15D1C8 + LAYERS_OFFSET + HUD_LAYER * LAYER_SIZE;
    s32 span = SPAN_NONE;
    s32 i;

    if (LAYERS_OFF == 1) {
        return;
    }
    for (i = 0; i < group->size; i++) {
        CLS_W *object = group->elements[i];
        u8 kind = object->kind;

        if (kind & 0x80) {
            continue;
        }
        if (hud) {
            s32 want = hud_span(object);
            if (want != span) {
                if (want == SPAN_BAR) {
                    gEX3DSBottomScreen(D_8015C5CC_15D1CC++, 0, 0);
                }
                else if (want == SPAN_MAP) {
                    gEX3DSBottomScreen(D_8015C5CC_15D1CC++, 0, 0);
                }
                else {
                    gEX3DSTopScreen(D_8015C5CC_15D1CC++);
                }
                span = want;
            }
        }
        switch (kind) {
            case 1:     // a sprite
                func_800086F0_92F0();
                if (span == SPAN_MAP && recomp_get_hud_on_bottom()) {
                    u8 alpha = CLS_ALPHA(object);
                    CLS_ALPHA(object) = (alpha >= MAP_ALPHA_MAX) ? 0xFF : (u8)(alpha * 0xFF / MAP_ALPHA_MAX);
                    func_80008920_9520(object);
                    CLS_ALPHA(object) = alpha;
                }
                else {
                    func_80008920_9520(object);
                }
                break;
            case 2:     // a model
                func_80016C44_17844(object);
                SPRITE_SETUP_FN = 0x800086F0;
                SPRITE_CACHE_A = 0xFFFF;
                break;
            case 4:
                SPRITE_SETUP_FN = 0x800086F0;
                SPRITE_CACHE_A = 0xFFFF;
                SPRITE_CACHE_B = 0xFFFF;
                break;
            case 5:     // a background
                func_800215D0_221D0();
                func_80021740_22340(object);
                SPRITE_SETUP_FN = 0x800086F0;
                SPRITE_CACHE_B = 0xFFFF;
                SPRITE_CACHE_A = 0xFFFF;
                break;
            default:    // 3, and anything past 5: nothing
                break;
        }
    }
    if (span != SPAN_NONE) {
        gEX3DSTopScreen(D_8015C5CC_15D1CC++);
    }
}

// @recomp The map's own task (one per field area that has a map; dungeons
// have none). Its states: 0 makes its three objects (the map and two
// markers), 1 hidden, 2 shown (fading in), 3 fading out. Hidden, it waits for
// C-Right (with R up) or the "map on" flag C-Right left set, and for no event
// (MAP_EVENT_A/B: talking, cutscenes); shown, C-Right or an event hides it.
// As the game has it, except that with the HUD on the touch screen the map
// is always on: C-Right neither shows nor hides it, and an event still hides
// it until the event ends.

#define MAP_ON          (*(u8 *)0x8015C574)          // C-Right switched the map on
#define MAP_EVENT_A     (*(u8 *)0x800C7AE2)          // set: no map now
#define MAP_EVENT_B     (*(u16 *)0x800C7AD6)
#define AREA_SET        (*(u8 *)0x800C7AA4)          // which of the two area tables
#define AREA_ID         (*(u16 *)0x800C7AB4)
#define AREA_KIND       (*(u16 *)0x800C7AB2)
#define MAP_ORIGIN_X    (*(s32 *)0x800C7CC0)
#define MAP_ORIGIN_Z    (*(s32 *)0x800C7CC4)
#define MAP_ORIGIN_ROT  (*(s32 *)0x800C7CC8)
#define MAP_DETAILED_A  ((u8 *)0x802379A0)           // by area: the detailed map sprite (1) or the plain one
#define MAP_DETAILED_B  ((u8 *)0x802379CC)
#define ROOM_TABLE      ((u32 *)0x80236984)          // by room: its data, whose first word leads to its file id
#define MAP_FILE_PLAIN  (*(u16 *)0x80209CBC)         // the file the map's picture comes from, per sprite
#define MAP_FILE_DETAIL (*(u16 *)0x80209CCC)
#define PAD_HELD        (*(u16 *)((u8 *)D_8015C5C8_15D1C8 + 0x3B07A))
#define PAD_PRESSED     (*(u16 *)((u8 *)D_8015C5C8_15D1C8 + 0x3B07C))
#define PAD_REOPEN      (*(u16 *)((u8 *)D_8015C5C8_15D1C8 + 0x3B092))

#define MAP_TASK_ROOM(t)    (*(u16 *)((u8 *)(t) + 0xD0))
#define MAP_TASK_D8(t)      (*(s32 *)((u8 *)(t) + 0xD8))
#define MAP_TASK_STATE(t)   (*(s32 *)((u8 *)(t) + 0xDC))
#define MAP_TASK_MARKER(t)  (*(CLS_W **)((u8 *)(t) + 0xE0))   // its next object is the map
#define MAP_TASK_DETAIL(t)  (*(s32 *)((u8 *)(t) + 0xE4))
#define MAP_TASK_MARKER2(t) (*(CLS_W **)((u8 *)(t) + 0xE8))
#define TASK_NEW_OBJECT(t)  (*(CLS_W **)((u8 *)(t) + 0x1C))   // what func_80035EEC_36AEC made

extern void func_80036260_36E60(void *task);
extern void func_80035044_35C44(void *task);
extern void func_800086C4_92C4(CLS_W *object, void *sprite, void *palette);
extern void func_8021617C_5D164C(CLS_W *object, s32 a, s32 b);
extern void func_80216228_5D16F8(void *task, CLS_W *marker);

static u16 room_file_id(u16 room) {
    return **(u16 **)ROOM_TABLE[room];
}

// Makes one of the map's objects: false (the task ended) when out of them.
static s32 map_new_object(void *task, CLS_W **out) {
    if (func_80035EEC_36AEC((Task *)task, 1, 1) == NULL) {
        func_80035044_35C44(task);
        return FALSE;
    }
    *out = TASK_NEW_OBJECT(task);
    return TRUE;
}

RECOMP_PATCH void func_80215CA4_5D1174(void *task, void *spawn) {
    s32 always = recomp_get_hud_on_bottom();
    CLS_W *object;
    CLS_W *map;
    s32 detailed;

    if (MAP_TASK_STATE(task) == 0) {
        MAP_ORIGIN_X = (s32)*(f32 *)((u8 *)spawn + 0x8);
        MAP_ORIGIN_Z = (s32)*(f32 *)((u8 *)spawn + 0xC);
        MAP_ORIGIN_ROT = *(u16 *)((u8 *)spawn + 0x14);
        if (AREA_KIND == 0x132) {
            MAP_ORIGIN_ROT = 0x1AF4;
        }
    }
    switch (MAP_TASK_STATE(task)) {
        case 0:
            func_80036260_36E60(task);
            func_800141C4_14DC4(room_file_id(MAP_TASK_ROOM(task)));
            detailed = (AREA_SET == 1) ? MAP_DETAILED_A[AREA_ID] : MAP_DETAILED_B[AREA_ID];
            MAP_TASK_DETAIL(task) = detailed;
            if (detailed == 0) {
                MAP_FILE_PLAIN = room_file_id(MAP_TASK_ROOM(task));
            }
            else {
                MAP_FILE_DETAIL = room_file_id(MAP_TASK_ROOM(task));
            }
            if (!map_new_object(task, &object)) {
                return;
            }
            object->s_pri = 0x20;
            MAP_TASK_MARKER2(task) = object;
            func_800086C4_92C4(object, (void *)0x80209BD4, (void *)0x80209CA0);
            func_8021617C_5D164C(object, 1, 1);
            ((CLS_2D_W *)object)->unk_1c = 0x18;
            ((CLS_2D_W *)object)->unk_20 = 0x18;
            if (!map_new_object(task, &object)) {
                return;
            }
            object->s_pri = 0x30;
            MAP_TASK_MARKER(task) = object;
            func_800086C4_92C4(object, (void *)0x80209BC8, (void *)0x80209CA0);
            func_8021617C_5D164C(object, 1, 1);
            ((CLS_2D_W *)object)->unk_1c = 0x30;
            ((CLS_2D_W *)object)->unk_20 = 0x30;
            if (!map_new_object(task, &object)) {
                return;
            }
            object->s_pri = 0x10;
            if (detailed == 0) {
                func_800086C4_92C4(object, (void *)0x80209BB0, (void *)0x80209CA0);
            }
            else {
                func_800086C4_92C4(object, (void *)0x80209BBC, (void *)0x80209CA0);
                ((CLS_2D_W *)object)->unk_38 = 0x08000800;
                ((CLS_2D_W *)object)->unk_c = 0xF;
            }
            func_8021617C_5D164C(object, 2, 2);
            MAP_TASK_STATE(task)++;
            return;

        case 1:
            if (!MAP_ON && !always) {
                if (PAD_HELD & R_TRIG) {
                    return;
                }
                if (!(PAD_PRESSED & R_CBUTTONS)) {
                    return;
                }
            }
            if (MAP_EVENT_A != 0 || MAP_EVENT_B != 0) {
                return;
            }
            MAP_ON = 1;
            MAP_TASK_STATE(task)++;
            return;

        case 2:
            func_80216228_5D16F8(task, MAP_TASK_MARKER(task));
            map = MAP_TASK_MARKER(task)->next;
            if (CLS_ALPHA(map) <= 0x90) {
                CLS_ALPHA(map) += 0x10;
            }
            else {
                CLS_ALPHA(map) = MAP_ALPHA_MAX;
            }
            CLS_ALPHA(MAP_TASK_MARKER2(task)) = CLS_ALPHA(map);
            if (!always && !(PAD_HELD & R_TRIG) && (PAD_PRESSED & R_CBUTTONS)) {
                // C-Right: hide
            }
            else if (MAP_EVENT_A == 0 && MAP_EVENT_B == 0) {
                return;
            }
            MAP_ON = 0;
            MAP_TASK_STATE(task)++;
            return;

        case 3:
            func_80216228_5D16F8(task, MAP_TASK_MARKER(task));
            map = MAP_TASK_MARKER(task)->next;
            if (CLS_ALPHA(map) >= 0x10) {
                CLS_ALPHA(map) -= 0x10;
            }
            else {
                CLS_ALPHA(map) = 0;
                MAP_TASK_STATE(task) = 1;
                MAP_TASK_D8(task) = 0;
                CLS_ALPHA(MAP_TASK_MARKER(task)) = 0;
            }
            CLS_ALPHA(MAP_TASK_MARKER2(task)) = CLS_ALPHA(map);
            if (PAD_REOPEN & 0x8000) {
                MAP_TASK_STATE(task) = 2;
            }
            return;

        default:
            return;
    }
}

#endif
