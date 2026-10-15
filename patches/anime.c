#include "patches.h"
#include "graphics.h"
#include "sky_scroll.h"
#include "transform_ids.h"

// @recomp
Object *g_actor_skeleton_root_object = NULL;
u8 g_actor_skip_interpolation = FALSE;

Object *g_player_skeleton_root_object = NULL;
u8 g_player_skip_interpolation = FALSE;

Camera g_widescreen_camera;

// @recomp The usual camera scissor is (8,16)-(312,224), but some cameras use
// their own near-full-screen one (the Impact battles' camera: (10,14)-(310,240)).
// On the N64 such borders sat in the TV's overscan, so any edge within 16 pixels
// of the screen's goes to the edge and the scene fills a wide screen. Cutscenes'
// cinematic letterboxing uses deeper bars than that and is kept.
static void widen_camera_scissor(Camera *cam) {
    if ((cam->scissor_ulx > 0) && (cam->scissor_ulx <= 16)) {
        cam->scissor_ulx = 0;
    }

    if ((cam->scissor_uly > 0) && (cam->scissor_uly <= 16)) {
        cam->scissor_uly = 0;
    }

    if ((cam->scissor_lrx >= SCREEN_WIDTH - 16) && (cam->scissor_lrx < SCREEN_WIDTH)) {
        cam->scissor_lrx = SCREEN_WIDTH;
    }

    if ((cam->scissor_lry >= SCREEN_HEIGHT - 16) && (cam->scissor_lry < SCREEN_HEIGHT)) {
        cam->scissor_lry = SCREEN_HEIGHT;
    }
}

#ifdef GOEMON_3DS
// @recomp The projection-group id for a camera's projection, for the 3DS port: rt64-3ds
// classifies draws for stereo by projection-group id, the way the DK64 and Banjo ports tag
// theirs (platform/3ds/main_3ds.cpp). Goemon draws some HUD items as small 3D models under a
// private perspective camera parked near the origin and looking straight down -Z (measured at
// (0, 0, -21) with a rotation within a few degrees of identity); the world camera is wherever
// the scene is, pointed wherever the shot needs. Anything else is the world.
static u32 goemon_projection_id_for_camera(const Camera *cam) {
    f32 dx, dy, dz, limit;

    if (cam == NULL) {
        return PROJECTION_GAMEPLAY_TRANSFORM_ID;
    }

    if ((cam->position.x * cam->position.x + cam->position.y * cam->position.y + cam->position.z * cam->position.z) >= (200.0f * 200.0f)) {
        return PROJECTION_GAMEPLAY_TRANSFORM_ID;
    }

    dx = cam->look_at.x - cam->position.x;
    dy = cam->look_at.y - cam->position.y;
    dz = cam->look_at.z - cam->position.z;
    if (dz >= 0.0f) {
        return PROJECTION_GAMEPLAY_TRANSFORM_ID;
    }

    // Within about 8.5 degrees of -Z.
    limit = -dz * 0.15f;
    if (dx < 0.0f) dx = -dx;
    if (dy < 0.0f) dy = -dy;
    if (dx > limit || dy > limit) {
        return PROJECTION_GAMEPLAY_TRANSFORM_ID;
    }

    return PROJECTION_HUD_TRANSFORM_ID;
}
#endif

// @recomp widen_camera_scissor's rule for a scissor command (coordinates in 10.2
// fixed point): an edge within 16 pixels of the screen's goes to it.
void widen_overscan_scissor(Gfx *g) {
    u32 w0 = g->words.w0;
    u32 w1 = g->words.w1;
    u32 ulx, uly, lrx, lry;

    if ((w0 >> 24) != G_SETSCISSOR) {
        return;
    }

    ulx = (w0 >> 12) & 0xFFF;
    uly = w0 & 0xFFF;
    lrx = (w1 >> 12) & 0xFFF;
    lry = w1 & 0xFFF;

    if ((ulx > 0) && (ulx <= (16 << 2))) {
        ulx = 0;
    }

    if ((uly > 0) && (uly <= (16 << 2))) {
        uly = 0;
    }

    if ((lrx >= ((SCREEN_WIDTH - 16) << 2)) && (lrx < (SCREEN_WIDTH << 2))) {
        lrx = SCREEN_WIDTH << 2;
    }

    if ((lry >= ((SCREEN_HEIGHT - 16) << 2)) && (lry < (SCREEN_HEIGHT << 2))) {
        lry = SCREEN_HEIGHT << 2;
    }

    g->words.w0 = (w0 & 0xFF000000) | (ulx << 12) | uly;
    g->words.w1 = (w1 & 0xFF000000) | (lrx << 12) | lry;
}

#ifdef GOEMON_3DS
// @recomp The same overscan rule for scissors that reach the frame's display list
// without a camera: the Impact battles' overlay builds two small display lists of
// its own every frame, a scissor and a fill rectangle clamped to (10,14)-(310,224)
// (the world under a cinematic letterbox: (10,54)-(310,184)), and object nodes
// copy them into the frame's list after the camera's widened scissor.
static void widen_overscan_scissors(Gfx *start, Gfx *end) {
    Gfx *g;

    // The list only grows while an object is drawn; anything else is not ours to touch.
    if ((end <= start) || ((end - start) > 0x4000)) {
        return;
    }

    for (g = start; g < end; g++) {
        widen_overscan_scissor(g);
    }
}

static s32 draw_object_node(Object *object);

RECOMP_PATCH s32 func_80016C44_17844(Object *object)
{
    const f32 original_aspect_ratio = (f32)SCREEN_WIDTH / (f32)SCREEN_HEIGHT;
    Gfx *start = D_8015C5CC_15D1CC;
    s32 ret = draw_object_node(object);

    if (recomp_get_target_aspect_ratio(original_aspect_ratio) != original_aspect_ratio) {
        widen_overscan_scissors(start, D_8015C5CC_15D1CC);
    }

    return ret;
}

static s32 draw_object_node(Object *object)
#else
RECOMP_PATCH s32 func_80016C44_17844(Object *object)
#endif
{
    u32 a;
    u32 *aptr;
    u32 object_type;
    AnimatedSkeleton *animated_skeleton;
    u32 animated_skeleton_flags;
    Skeleton **skeletons;
    u32 b;
    u32 c;
    u32 d;
    u32 e;
    u16 *g;
    Camera *camera;
    MyLight *light;
    void *object_type_5;
    AnimatedSkeleton *object_type_6;
    u32 h;
    u16 *i;
    const f32 original_aspect_ratio = (f32)SCREEN_WIDTH / (f32)SCREEN_HEIGHT;

    // @recomp Flag that is set whenever we should skip interpolation on this frame. (The variable used for this is seemingly unused most of the time.)
    u8 skip_interpolation = TAGGING_OBJECT_IS_SKIP_INTERPOLATION(object);
    TAGGING_OBJECT_CLEAR_SKIP_INTERPOLATION(object);

    u8 interpolate_vertices = TAGGING_OBJECT_IS_INTERPOLATE_VERTICES(object);
    TAGGING_OBJECT_CLEAR_INTERPOLATE_VERTICES(object);

    u8 skip_only_position = TAGGING_OBJECT_IS_SKIP_ONLY_POSITION(object);
    TAGGING_OBJECT_CLEAR_SKIP_ONLY_POSITION(object);

    if ((object->unknown_64 & 1) == 0) {
        if (object->unknown_65 < 0) {
            return 0;
        }

        D_801684A0_1690A0 = object;

        if (object->rotation.x != -32768) {
            object->rotation.x &= 0x3FF;
        }

        if (object->rotation.y != -32768) {
            object->rotation.y &= 0x3FF;
        }

        if (object->rotation.z != -32768) {
            object->rotation.z &= 0x3FF;
        }

        a = object->graphics_1;
        aptr = (u32 *)(a & 0x8FFFFFFE);

        if (a == 0 || aptr == NULL) {
            if (object->left != NULL || object->right != NULL) {
                func_800364FC_370FC(object);
            }

            return 0;
        }

        object_type = a & 0x70000000;

        D_80168524_169124 = 0;
        D_8016852D_16912D = (a & 1) == 0;
        D_8016852C_16912C = a & 1;

        gEXMatrixGroupSkipAll(D_8015C5CC_15D1CC++, (u32)object, G_EX_PUSH, G_MTX_MODELVIEW, G_EX_EDIT_ALLOW);

        switch (object_type) {
        case 0x00000000:
        case 0x10000000:
            // @recomp Used when hashing an identifier for the matrix.
            g_actor_skeleton_root_object = object;
            g_actor_skip_interpolation = skip_interpolation;

            if (aptr == NULL) {
                animated_skeleton = NULL;
                // Should return here but the original code continues. Huh?
                // Will crash when trying to access flags.
            } else {
                animated_skeleton = (AnimatedSkeleton *)aptr;

                if (!((s32)aptr & 0x80000000)) {
                    if (aptr < (u32 *)0x08000001) {
                        animated_skeleton = (AnimatedSkeleton *)(D_801684A0_1690A0->overlay_info[0].vram_addr + (a & 0x80FFFFFE));
                    } else {
                        animated_skeleton = (AnimatedSkeleton *)(D_801684A0_1690A0->overlay_info[(((u32)aptr >> 24) & 0xF) - 8].vram_addr + (a & 0x80FFFFFE));
                    }
                }
            }

            animated_skeleton_flags = animated_skeleton->flags;
            if (animated_skeleton_flags == 0) {
                D_801684F0_1690F0 = animated_skeleton;
                D_80168524_169124 = 0;
                return 0;
            }

            // Odd way of checking if the most significant bit is set.
            D_8016851C_16911C = animated_skeleton_flags / 0x80000000;
            D_80168518_169118 = (f32)(animated_skeleton_flags & 0xFF);
            D_80168510_169110 = D_801684A0_1690A0->animation_progress;
            D_801684F0_1690F0 = animated_skeleton->animation;

            if (D_801684F0_1690F0 == NULL) {
                D_80168520_169120 = NULL;
            } else {
                D_80168520_169120 = D_801684F0_1690F0;

                if (!((s32)D_801684F0_1690F0 & 0x80000000)) {
                    if (D_801684F0_1690F0 < (void *)0x08000001) {
                        D_80168520_169120 = (void *)((u32)D_801684A0_1690A0->overlay_info[0].vram_addr + ((u32)D_801684F0_1690F0 & 0x80FFFFFF));
                    } else {
                        D_80168520_169120 = (void *)((u32)D_801684A0_1690A0->overlay_info[(((u32)D_801684F0_1690F0 >> 24) & 0xF) - 8].vram_addr + ((u32)D_801684F0_1690F0 & 0x80FFFFFF));
                    }
                }
            }

            skeletons = &animated_skeleton->skeletons[0];

            b = (animated_skeleton_flags >> 0x1C) & 0x7;

            if (b) {
                func_8001E380_1EF80(&D_801684B0_1690B0, D_801684A0_1690A0);

                while (b) {
                    b--;

                    D_801684F0_1690F0 = *skeletons;
                    c = 0;

                    if (D_801684F0_1690F0) {
                        c = (u32)D_801684F0_1690F0;

                        if (!((s32)D_801684F0_1690F0 & 0x80000000)) {
                            if (D_801684F0_1690F0 < (void *)0x08000001) {
                                c = *(s32 *)((u32)D_801684A0_1690A0->overlay_info[0].vram_addr + ((u32)D_801684F0_1690F0 & 0x80FFFFFF));
                            } else {
                                c = *(s32 *)((u32)D_801684A0_1690A0->overlay_info[(((u32)D_801684F0_1690F0 >> 24) & 0xF) - 8].vram_addr + ((u32)D_801684F0_1690F0 & 0x80FFFFFF));
                            }
                        }
                    }

                    skeletons++;

                    if (!c) {
                        return 0;
                    }

                    func_800176F0_182F0((Skeleton *)c, &D_801684B0_1690B0);
                }
            }

            d = (animated_skeleton_flags >> 24) & 0xF;
        
            if (d) {
                while (d != 0) {
                    d--;

                    D_801684F0_1690F0 = *skeletons;
                    e = 0;

                    if (D_801684F0_1690F0) {
                        e = (u32)D_801684F0_1690F0;

                        if (!((s32)D_801684F0_1690F0 & 0x80000000)) {
                            if (D_801684F0_1690F0 < (void *)0x08000001) {
                                e = *(s32 *)((u32)D_801684A0_1690A0->overlay_info[0].vram_addr + ((u32)D_801684F0_1690F0 & 0x80FFFFFF));
                            } else {
                                e = *(s32 *)((u32)D_801684A0_1690A0->overlay_info[(((u32)D_801684F0_1690F0 >> 24) & 0xF) - 8].vram_addr + ((u32)D_801684F0_1690F0 & 0x80FFFFFF));
                            }
                        }
                    }

                    skeletons++;

                    if (!e) {
                        return 0;
                    }

                    func_800182DC_18EDC((Skeleton *)e);
                }
            }

            animated_skeleton_flags = (animated_skeleton_flags >> 8) & 0x7FFF;
            if (animated_skeleton_flags == 0) {
                return 1;
            }

            func_80016AA4_176A4();
            func_80019D40_1A940(D_801684A0_1690A0);

            while (animated_skeleton_flags != 0) {
                animated_skeleton_flags--;

                D_801684F0_1690F0 = *skeletons;

                g = 0;

                if (D_801684F0_1690F0) {
                    g = (u16 *)D_801684F0_1690F0;

                    if (!((s32)D_801684F0_1690F0 & 0x80000000)) {
                        if (D_801684F0_1690F0 < (void *)0x08000001) {
                            g = (u16 *)((u32)D_801684A0_1690A0->overlay_info[0].vram_addr + ((u32)D_801684F0_1690F0 & 0x80FFFFFF));
                        } else {
                            g = (u16 *)((u32)D_801684A0_1690A0->overlay_info[(((u32)D_801684F0_1690F0 >> 24) & 0xF) - 8].vram_addr + ((u32)D_801684F0_1690F0 & 0x80FFFFFF));
                        }
                    }
                }

                skeletons++;

                if (g) {
                    func_80018718_19318((Skeleton *)g, object_type);
                }
            }

            gSPPopMatrix(D_8015C5CC_15D1CC++, G_MTX_MODELVIEW);
            return 1;

        case 0x20000000:
            // @recomp Fix the scissor so that RT64 can render in widescreen.
            if (recomp_get_target_aspect_ratio(original_aspect_ratio) != original_aspect_ratio) {
                memcpy(&g_widescreen_camera, (Camera *)aptr, sizeof(Camera));
                widen_camera_scissor(&g_widescreen_camera);
                aptr = (u32 *)&g_widescreen_camera;
            }

            func_8001E380_1EF80(&D_801684B0_1690B0, D_801684A0_1690A0);

            // @recomp Resolve the camera before its projection is built: the 3DS port tags
            // the projection by what the camera is, and the sky capture below reads it.
            if (aptr == NULL) {
                camera = NULL;
            } else {
                camera = (Camera *)aptr;

                if (!((s32)aptr & 0x80000000)) {
                    if (aptr < (u32 *)0x08000001) {
                        camera = (Camera *)(D_801684A0_1690A0->overlay_info[0].vram_addr + (a & 0x80FFFFFE));
                    } else {
                        camera = (Camera *)(D_801684A0_1690A0->overlay_info[(((u32)aptr >> 24) & 0xF) - 8].vram_addr + (a & 0x80FFFFFE));
                    }
                }
            }

#ifdef GOEMON_3DS
            // @recomp Tag the projection this camera is about to load (see
            // goemon_projection_id_for_camera). As in the DK64 and Banjo ports the id is set,
            // not pushed: it stays in force for the draws under this camera until the next
            // camera's, and the interpreter clears it at the start of every task. The
            // interpolation components are what RT64 gives the desktop's untagged camera
            // (simple interpolation of everything), for rt64-3ds's interpolated frame rate.
            gEXMatrixGroupSimpleNormal(D_8015C5CC_15D1CC++, goemon_projection_id_for_camera(camera), G_EX_NOPUSH, G_MTX_PROJECTION, G_EX_EDIT_NONE);
#endif
            func_80017D8C_1898C((Camera *)aptr, &D_801684B0_1690B0);

            D_801684F0_1690F0 = aptr;
            func_8001B6D4_1C2D4(camera);

            // @recomp Capture the world view for the 3D sky in background.c,
            // which needs the camera's true heading and FoV.
            if (camera != NULL) {
                g_sky_view.position = camera->position;
                g_sky_view.look_at = camera->look_at;
                g_sky_view.half_fovy = (u16)camera->unknown_1a;
                g_sky_view.perspective = !(((u8 *)camera)[0x3E] & 1);
                g_sky_view.step = g_game_step;
            }
            return 1;

        case 0x30000000:
            if (aptr == NULL) {
                light = NULL;
            } else {
                light = (MyLight *)aptr;

                if (!((s32)aptr & 0x80000000)) {
                    if (aptr < (u32 *)0x08000001) {
                        light = (void *)(D_801684A0_1690A0->overlay_info[0].vram_addr + (a & 0x80FFFFFE));
                    } else {
                        light = (void *)(D_801684A0_1690A0->overlay_info[(((u32)aptr >> 24) & 0xF) - 8].vram_addr + (a & 0x80FFFFFE));
                    }
                }
            }

            D_801684F0_1690F0 = aptr;
            func_800185C8_191C8(light);
            return 1;

        case 0x40000000:
            // @recomp Skip interpolation if needed.
            if (!skip_interpolation) {
                if (interpolate_vertices) {
                    gEXMatrixGroupDecomposedVerts(D_8015C5CC_15D1CC++, (u32)object, G_EX_PUSH, G_MTX_MODELVIEW, G_EX_EDIT_ALLOW);
                } else {
                    if (skip_only_position) {
                        gEXMatrixGroupDecomposedSkipPos(D_8015C5CC_15D1CC++, (u32)object, G_EX_PUSH, G_MTX_MODELVIEW, G_EX_EDIT_ALLOW);
                    } else {
                        gEXMatrixGroupDecomposedNormal(D_8015C5CC_15D1CC++, (u32)object, G_EX_PUSH, G_MTX_MODELVIEW, G_EX_EDIT_ALLOW);
                    }
                }
            }

            func_80016AA4_176A4();
            func_80019D40_1A940(D_801684A0_1690A0);
            func_800196F0_1A2F0((Gfx *)((u32)(D_801684A0_1690A0->graphics_1) & 0x8FFFFFFE));

            gSPPopMatrix(D_8015C5CC_15D1CC++, G_MTX_MODELVIEW);

            // @recomp Skip interpolation if needed.
            if (!skip_interpolation) {
                gEXPopMatrixGroup(D_8015C5CC_15D1CC++, G_MTX_MODELVIEW);
            }
            return 1;

        case 0x50000000:
            if (aptr == NULL) {
                object_type_5 = NULL;
            } else {
                object_type_5 = (void *)aptr;

                if (!((s32)aptr & 0x80000000)) {
                    if (aptr < (u32 *)0x08000001) {
                        object_type_5 = (void *)(D_801684A0_1690A0->overlay_info[0].vram_addr + (a & 0x80FFFFFE));
                    } else {
                        object_type_5 = (void *)(D_801684A0_1690A0->overlay_info[(((u32)aptr >> 24) & 0xF) - 8].vram_addr + (a & 0x80FFFFFE));
                    }
                }
            }

            D_801684F0_1690F0 = aptr;
            func_800184B4_190B4(object_type_5);
            return 1;

        case 0x60000000:
            // @recomp Used when hashing an identifier for the matrix.
            g_player_skeleton_root_object = object;
            g_player_skip_interpolation = skip_interpolation;
            
            if (aptr == NULL) {
                object_type_6 = NULL;
            } else {
                object_type_6 = (AnimatedSkeleton *)aptr;

                if (!((s32)aptr & 0x80000000)) {
                    if (aptr < (u32 *)0x08000001) {
                        object_type_6 = (void *)(D_801684A0_1690A0->overlay_info[0].vram_addr + (a & 0x80FFFFFE));
                    } else {
                        object_type_6 = (void *)(D_801684A0_1690A0->overlay_info[(((u32)aptr >> 24) & 0xF) - 8].vram_addr + (a & 0x80FFFFFE));
                    }
                }
            }

            h = object_type_6->flags;
            if (h == 0) {
                D_801684F0_1690F0 = aptr;
                D_80168524_169124 = 0;
                return 0;
            }

            D_8016851C_16911C = h / 0x80000000;
            D_80168518_169118 = (f32)(h & 0xFF);
            D_80168510_169110 = D_801684A0_1690A0->animation_progress;
            D_80168514_169114 = D_801684A0_1690A0->unknown_70;

            D_801684F0_1690F0 = object_type_6->animation;

            if (D_801684F0_1690F0 == NULL) {
                D_80168520_169120 = NULL;
            } else {
                D_80168520_169120 = D_801684F0_1690F0;

                if (!((s32)D_801684F0_1690F0 & 0x80000000)) {
                    if (D_801684F0_1690F0 < (void *)0x08000001) {
                        D_80168520_169120 = (u16 *)((u32)D_801684A0_1690A0->overlay_info[0].vram_addr + ((u32)D_801684F0_1690F0 & 0x80FFFFFF));
                    } else {
                        D_80168520_169120 = (u16 *)((u32)D_801684A0_1690A0->overlay_info[(((u32)D_801684F0_1690F0 >> 24) & 0xF) - 8].vram_addr + ((u32)D_801684F0_1690F0 & 0x80FFFFFF));
                    }
                }
            }

            func_80016AA4_176A4();
            func_80019D40_1A940(D_801684A0_1690A0);

            if (D_8016851C_16911C) {
                D_801684F0_1690F0 = object_type_6->skeletons[0];
                i = NULL;

                if (D_801684F0_1690F0) {
                    i = (u16 *)D_801684F0_1690F0;

                    if (!((s32)D_801684F0_1690F0 & 0x80000000)) {
                        if (D_801684F0_1690F0 < (void *)0x08000001) {
                            i = (u16 *)((u32)D_801684A0_1690A0->overlay_info[0].vram_addr + ((u32)D_801684F0_1690F0 & 0x80FFFFFF));
                        } else {
                            i = (u16 *)((u32)D_801684A0_1690A0->overlay_info[(((u32)D_801684F0_1690F0 >> 24) & 0xF) - 8].vram_addr + ((u32)D_801684F0_1690F0 & 0x80FFFFFF));
                        }
                    }
                }

                // Constant instead of using object_type for some reason.
                func_80018B28_19728((Skeleton *)i, 0x60000000, D_801684A0_1690A0->right);
            }

            if (D_801684A0_1690A0->unknown_70 != 0.0f) {
                D_801684A0_1690A0->unknown_70 -= 1.0f;
            }

            gSPPopMatrix(D_8015C5CC_15D1CC++, G_MTX_MODELVIEW);
            return 1;

        default:
            D_80168524_169124 = 0;
            return 1;
        }
    } else {
        return 0;
    }

    return 1;
}

/* Not needed.
RECOMP_PATCH void func_80018718_19318(Skeleton *skeleton, u32 object_type)
{
    u32 display_list_vram_addr;
    u32 ptr;
    Skeleton *masked_ptr;

    if (object_type == 0) {
        func_800192D0_19ED0(skeleton, NULL);
    }

    display_list_vram_addr = skeleton->display_list_vram_addr;

    if ((s32)(display_list_vram_addr << 3) < 0) {
        if (display_list_vram_addr == NULL) {
            ptr = NULL;
        } else {
            ptr = (u32)display_list_vram_addr;

            if (!((s32)display_list_vram_addr & 0x80000000)) {
                if (display_list_vram_addr <= 0x08000000) {
                    ptr = (u32)((u32)D_801684A0_1690A0->overlay_info[0].vram_addr + ((u32)display_list_vram_addr & 0x80FFFFFF));
                } else {
                    ptr = (u32)((u32)D_801684A0_1690A0->overlay_info[(((u32)display_list_vram_addr >> 24) & 0xF) - 8].vram_addr + ((u32)display_list_vram_addr & 0x80FFFFFF));
                }
            }
        }

        masked_ptr = (Skeleton *)(ptr & 0x8FFFFFFF);
        D_801684F0_1690F0 = (void *)display_list_vram_addr;

        if (masked_ptr->display_list_vram_addr != NULL) {
            func_800196F0_1A2F0((Gfx *)masked_ptr->display_list_vram_addr);
            func_800196A4_1A2A4();
        }

        if (masked_ptr->offset_right != 0) {
            func_80018908_19508(masked_ptr + masked_ptr->offset_right);
        }

        if (masked_ptr->offset_left != 0) {
            func_80018908_19508(masked_ptr + masked_ptr->offset_left);
        }
    } else if ((display_list_vram_addr & 0x8FFFFFFF) != 0) {
        func_800196F0_1A2F0((Gfx *)(display_list_vram_addr & 0x8FFFFFFF));
    }

    if ((object_type == 0) && ((skeleton->rotation.x | skeleton->rotation.y | skeleton->rotation.z) & 0x4000) != 0) {
        func_800196A4_1A2A4();
    }

    if (skeleton->offset_right != 0) {
        func_80018908_19508(skeleton + skeleton->offset_right);
    }

    if (skeleton->offset_left != 0) {
        func_80018908_19508(skeleton + skeleton->offset_left);
    }

    if (object_type == 0) {
        func_800196A4_1A2A4();
    }
}
*/

RECOMP_PATCH void func_80018908_19508(Skeleton *skeleton)
{
    u32 display_list_vram_addr;
    u32 ptr;
    Skeleton *masked_ptr;

    while (TRUE) {

        // @recomp Tag the current limb matrix.
        u8 skip_interpolation = g_actor_skip_interpolation;

        u64 seed = ((u64)g_actor_skeleton_root_object) << 32 | (u32)skeleton->display_list_vram_addr;
        u32 id = TAGGING_GENERATE_ID(seed);

        if (!skip_interpolation) {
            gEXMatrixGroupDecomposedNormal(D_8015C5CC_15D1CC++, id, G_EX_PUSH, G_MTX_MODELVIEW, G_EX_EDIT_ALLOW);
        }

        func_800192D0_19ED0(skeleton, NULL);

        display_list_vram_addr = skeleton->display_list_vram_addr;

        if ((s32)(display_list_vram_addr << 3) < 0) {
            if (display_list_vram_addr == NULL) {
                ptr = NULL;
            } else {
                ptr = display_list_vram_addr;

                if (!((s32)display_list_vram_addr & 0x80000000)) {
                    if (display_list_vram_addr <= 0x08000000) {
                        ptr = (u32)((u32)D_801684A0_1690A0->overlay_info[0].vram_addr + ((u32)display_list_vram_addr & 0x80FFFFFF));
                    } else {
                        ptr = (u32)((u32)D_801684A0_1690A0->overlay_info[(((u32)display_list_vram_addr >> 24) & 0xF) - 8].vram_addr + ((u32)display_list_vram_addr & 0x80FFFFFF));
                    }
                }
            }

            masked_ptr = (Skeleton *)(ptr & 0x8FFFFFFF);
            D_801684F0_1690F0 = (void *)display_list_vram_addr;

            if (masked_ptr->display_list_vram_addr != NULL) {
                func_800196F0_1A2F0((Gfx *)masked_ptr->display_list_vram_addr);
            }

            if (masked_ptr->offset_right != 0) {
                func_80018908_19508(masked_ptr + masked_ptr->offset_right);
            }

            if (masked_ptr->offset_left != 0) {
                func_80018908_19508(masked_ptr + masked_ptr->offset_left);
            }
        } else if ((display_list_vram_addr & 0x8FFFFFFF) != 0) {
            func_800196F0_1A2F0((Gfx *)(display_list_vram_addr & 0x8FFFFFFF));
        }

        if (((skeleton->rotation.x | skeleton->rotation.y | skeleton->rotation.z) & 0x4000) != 0) {
            func_800196A4_1A2A4();

            // @recomp Pop (untag) the current limb matrix.
            if (!skip_interpolation) {
                gEXPopMatrixGroup(D_8015C5CC_15D1CC++, G_MTX_MODELVIEW);
            }
        }

        if (D_8016851C_16911C == 0) {
            func_800196A4_1A2A4();

            // @recomp Pop (untag) the current limb matrix.
            if (!skip_interpolation) {
                gEXPopMatrixGroup(D_8015C5CC_15D1CC++, G_MTX_MODELVIEW);
            }

            if (skeleton->offset_right != 0) {
                func_80018908_19508(skeleton + skeleton->offset_right);
            }
        } else {
            if (skeleton->offset_right != 0) {
                func_80018908_19508(skeleton + skeleton->offset_right);
            }

            func_800196A4_1A2A4();
            
            // @recomp Pop (untag) the current limb matrix.
            if (!skip_interpolation) {
                gEXPopMatrixGroup(D_8015C5CC_15D1CC++, G_MTX_MODELVIEW);
            }
        }

        if (skeleton->offset_left != 0) {
            skeleton = skeleton + skeleton->offset_left;
        } else {
            break;
        }
    }
}

RECOMP_PATCH void func_80018CA0_198A0(Skeleton *skeleton, Object *object)
{
    u32 graphics_2_vram_addr;
    Object *root_object;
    u32 display_list_vram_addr;
    u32 ptr;
    Skeleton *masked_ptr;
    Object *returned_val;

    graphics_2_vram_addr = (u32)object->graphics_2;

    while (TRUE) {
        root_object = D_801684A0_1690A0;

        if (graphics_2_vram_addr != NULL) {
            D_80168524_169124 = 0;
            D_801684A0_1690A0 = object;
        }

        // @recomp Tag the current limb matrix.
        u8 skip_interpolation = g_player_skip_interpolation;

        u64 seed = ((u64)g_player_skeleton_root_object) << 32 | (u32)skeleton->display_list_vram_addr;
        u32 id = TAGGING_GENERATE_ID(seed);

        if (!skip_interpolation) {
            gEXMatrixGroupDecomposedNormal(D_8015C5CC_15D1CC++, id, G_EX_PUSH, G_MTX_MODELVIEW, G_EX_EDIT_ALLOW);
        }

        func_800192D0_19ED0(skeleton, object);

        display_list_vram_addr = skeleton->display_list_vram_addr;

        if ((s32)(display_list_vram_addr << 3) < 0) {
            if (display_list_vram_addr == NULL) {
                ptr = NULL;
            } else {
                ptr = display_list_vram_addr;

                if (!((s32)display_list_vram_addr & 0x80000000)) {
                    if (display_list_vram_addr <= 0x08000000) {
                        ptr = (u32)((u32)D_801684A0_1690A0->overlay_info[0].vram_addr + ((u32)display_list_vram_addr & 0x80FFFFFF));
                    } else {
                        ptr = (u32)((u32)D_801684A0_1690A0->overlay_info[(((u32)display_list_vram_addr >> 24) & 0xF) - 8].vram_addr + ((u32)display_list_vram_addr & 0x80FFFFFF));
                    }
                }
            }

            masked_ptr = (Skeleton *)(ptr & 0x8FFFFFFF);
            D_801684F0_1690F0 = (void *)display_list_vram_addr;

            if (masked_ptr->display_list_vram_addr != NULL) {
                func_800196F0_1A2F0((Gfx *)masked_ptr->display_list_vram_addr);
            }

            if (masked_ptr->offset_right != 0) {
                func_80018CA0_198A0(masked_ptr + masked_ptr->offset_right, object);
            }

            if (masked_ptr->offset_left != 0) {
                func_80018CA0_198A0(masked_ptr + masked_ptr->offset_left, object);
            }
        } else if ((display_list_vram_addr & 0x8FFFFFFF) != 0) {
            func_800196F0_1A2F0((Gfx *)(display_list_vram_addr & 0x8FFFFFFF));
        }

        if (((skeleton->rotation.x | skeleton->rotation.y | skeleton->rotation.z) & 0x4000) != 0) {
            func_800196A4_1A2A4();

            // @recomp Pop (untag) the current limb matrix.
            if (!skip_interpolation) {
                gEXPopMatrixGroup(D_8015C5CC_15D1CC++, G_MTX_MODELVIEW);
            }
        }

        if (root_object != D_801684A0_1690A0) {
            D_80168524_169124 = 0;
            D_801684A0_1690A0 = root_object;
        } 

        if (skeleton->offset_right != 0) {
            returned_val = func_80018F3C_19B3C(skeleton, object);
            if (returned_val != NULL) {
                func_80018CA0_198A0(skeleton + skeleton->offset_right, returned_val);
            }
        } else {
            if (object->right != NULL) {
                func_8003674C_3734C(object);
            }
        }

        func_800196A4_1A2A4();

        // @recomp Pop (untag) the current limb matrix.
        if (!skip_interpolation) {
            gEXPopMatrixGroup(D_8015C5CC_15D1CC++, G_MTX_MODELVIEW);
        }

        if (skeleton->offset_left != 0) {
            object = func_8001904C_19C4C(skeleton, object);
            if (object == NULL) {
                break;
            }

            skeleton = skeleton + skeleton->offset_left;
            graphics_2_vram_addr = (u32)object->graphics_2;
        } else {
            if (object->left != NULL) {
                func_80036798_37398(object);
            }

            break;
        }
    }
}

