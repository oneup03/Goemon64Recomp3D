// Goemon 64: Recompiled, Nintendo 3DS entry point.
#include <cinttypes>
#include <cstdio>
#include <string>

#include <3ds.h>

#include "recomp3ds.h"
#include "ovl_patches.hpp"
#include "goemon_settings.h"
#include "librecomp/rsp.hpp"
#include "librecomp/game.hpp"
#include "ultramodern/ultra64.h"
#include "../../patches/transform_ids.h"

extern "C" void recomp_entrypoint(uint8_t* rdram, recomp_context* ctx);
gpr get_entrypoint_address();
extern RspUcodeFunc aspMain;

namespace {

// A=A  B=B  L=Z  R=R  START=START; the C buttons on X (up), Y (down),
// ZL (left), ZR (right) and the D-pad. Goemon never reads N64 L or the D-pad.
// R with the C buttons moves the camera; the C-Stick does that by itself
// (GameDesc::cstick_camera_hold below).
using namespace recomp3ds;
const ButtonMap kButtons[] = {
    { KEY_A, N64_A }, { KEY_B, N64_B }, { KEY_L, N64_Z }, { KEY_R, N64_R }, { KEY_START, N64_START },
    { KEY_X | KEY_DUP, N64_CUP }, { KEY_Y | KEY_DDOWN, N64_CDOWN },
    { KEY_ZL | KEY_DLEFT, N64_CLEFT }, { KEY_ZR | KEY_DRIGHT, N64_CRIGHT },
};

// The desktop's options on the 3DS menu's pages, with the desktop's defaults
// (recomp_api_3ds.cpp).
const char* const kSkyNames[] = { "Flat", "3D" };
const MenuOption kOptions[] = {
    { "bgm_volume", "Music volume", 0, 100, &g_goemon_bgm_volume, nullptr, nullptr, PageGame, 5, "%" },
    { "se_volume", "Sound volume", 0, 100, &g_goemon_se_volume, nullptr, nullptr, PageGame, 5, "%" },
    // The sky that follows the camera: the 3D cylinder (patches/background.c)
    // or the game's flat scroll, which costs the game's CPU far less.
    { "sky_3d", "Sky", 0, 1, &g_goemon_sky_3d, kSkyNames, nullptr, PageGame, 1, nullptr },
};

// Defaults for the library's rows, the owner's tuning on the console
// (2026-10-02); settings.ini, once written, keeps what the player chose.
const MenuDefault kDefaults[] = {
    { "separation", 50 },               // 3D depth
    { "convergence_tenths", 20 },       // 2.0 = 40 game units
    { "hud_depth", 50 },                // the screen plane
    { "auto_convergence", 1 },
    { "comfort_target", 0 },
    { "stereo_ghost_contrast", 100 },   // percent: 100 with the 0% floor is ghost reduction off
    { "stereo_ghost_black_floor", 0 },  // percent
    { "stick_deadzone", 15 },           // percent
    { "cstick_deadzone", 20 },          // percent
    { "frame_cap", 2 },                 // 60 interpolated (RenderDesc::frame_interpolation)
};

RspUcodeFunc* get_rsp_microcode(const OSTask* task) {
    switch (task->t.type) {
        case M_AUDTASK:
            return aspMain;
        default:
            fprintf(stderr, "Unknown task: %" PRIu32 "\n", task->t.type);
            return nullptr;
    }
}

// goemon64::get_game_thread_name (src/main/main.cpp, not built here).
std::string get_game_thread_name(const OSThread* t) {
    std::string name = "[Game] ";
    switch (t->id) {
        case 0:  name += (t->priority == 150) ? "PIMGR" : (t->priority == 254) ? "VIMGR" : std::to_string(t->id); break;
        case 1:  name += "IDLE"; break;
        case 3:  name += (t->priority == 10) ? "MAIN" : (t->priority == 12) ? "AUDIO" : std::to_string(t->id); break;
        case 4:  name += (t->priority == 9) ? "GAME" : (t->priority == 13) ? "SCHED" : std::to_string(t->id); break;
        default: name += std::to_string(t->id); break;
    }
    return name;
}

// The projection groups the patches tag (patches/transform_ids.h; anime.c
// and background.c under GOEMON_3DS) and how each is placed in depth.
using rt64_3ds::ProjKind;
using rt64_3ds::StereoClass;
const rt64_3ds::StereoRule goemon_stereo_rules[] = {
    { PROJECTION_GAMEPLAY_TRANSFORM_ID, PROJECTION_GAMEPLAY_TRANSFORM_ID, ProjKind::Perspective, StereoClass::World },
    // The sky cylinder that follows the camera's yaw.
    { PROJECTION_SKYBOX_TRANSFORM_ID,   PROJECTION_SKYBOX_TRANSFORM_ID,   ProjKind::Perspective, StereoClass::Infinity },
    // The flat full-screen backdrops (rectangles).
    { PROJECTION_SKYBOX_TRANSFORM_ID,   PROJECTION_SKYBOX_TRANSFORM_ID,   ProjKind::Rect,        StereoClass::Infinity },
    // The 3D HUD items' own camera near the origin.
    { PROJECTION_HUD_TRANSFORM_ID,      PROJECTION_HUD_TRANSFORM_ID,      ProjKind::Any,         StereoClass::Hud },
    // Untagged perspective, and G_EX_ID_AUTO: the desktop treats them as the world.
    { 0, 0, ProjKind::Perspective, StereoClass::World },
    { 0xFFFFFFFF, 0xFFFFFFFF, ProjKind::Perspective, StereoClass::World },
};

}   // namespace

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    recomp3ds::GameDesc desc{};
    desc.game_id = u8"mnsg.us";
    desc.sd_dir = "Goemon64";               // the ROM: sdmc:/3ds/Goemon64/mnsg.us.z64
    // The desktop's version (src/main/main.cpp).
    recomp::Version::from_string("0.2.0-dev", desc.project_version);
    desc.entry = recomp::GameEntry{
        // The retail ROM, as the desktop: the game decompresses its files itself
        // and patches/required.c registers their overlays by file id.
        .rom_hash = 0xDB1BC7EE0E6BEBA1ULL,
        .internal_name = "MYSTICAL NINJA",
        .display_name = "Mystical Ninja Starring Goemon",
        .game_id = u8"mnsg.us",
        .mod_game_id = "",                  // no mods on the 3DS
        .save_type = recomp::SaveType::AllowAll,
        .thumbnail_bytes = {},
        .is_enabled = false,
        .decompression_routine = nullptr,  // only mod hooking needs it
        .has_compressed_code = true,
        .entrypoint_address = get_entrypoint_address(),
        .entrypoint = recomp_entrypoint,
    };
    desc.rsp.get_rsp_microcode = get_rsp_microcode;
    desc.register_overlays = goemon64::register_overlays;
    desc.register_patches = goemon64::register_patches;
    desc.get_game_thread_name = get_game_thread_name;
    // The ROM in memory on an Old 3DS too. rom_new3ds and rom_old3ds stay at
    // RomPlace::Auto on purpose (not Memory): the ROM is held when the heap
    // has ROM + RDRAM + this room, else streamed from the SD card (an Old 3DS
    // reads 128 KB in about 100 ms, and the game rereads its files as it
    // plays), so a smaller heap (a .3dsx under the Homebrew Launcher) streams
    // instead of failing allocations. The room is what the rest of the port
    // takes: the owner's console at the Old clock peaked at 36-37 MB of arena
    // with the ROM held and a 16 MB RDRAM, so about 5 MB beside ROM and
    // RDRAM; 8 keeps a 3 MB margin. With the 12 MB RDRAM (CMakeLists.txt)
    // the test asks 16 + 12 + 8 = 36 MB of an Old 3DS CIA's about 37 MB heap
    // (a New 3DS has 86), and the ROM line of the log prints the figures.
    desc.rom_room_mb = 8;
    // Goemon saves to the Controller Pak and only starts a game when a pak is
    // reported at boot; the desktop reports a Rumble Pak (src/game/input.cpp).
    desc.controller_pak = ultramodern::input::Pak::RumblePak;
    // The C-Stick turns the camera: Goemon's camera turns 45 degrees for each
    // C-Left/Right pressed while R is held (C-Up/Down zoom), easing back
    // behind the player once the presses stop, and a C press that comes in
    // the same frame as R is ignored. The C-Stick holds R and, 70 ms later,
    // presses its way's C button for 100 ms every 267 ms (8 game frames, one
    // turn's length, measured in Azahar: the turns then run into each other).
    // The Controls page's "C-Stick" row gives the plain C buttons back.
    desc.cstick_camera_hold = N64_R;
    desc.cstick_camera_lead_ms = 70;
    desc.cstick_camera_press_ms = 100;
    desc.cstick_camera_period_ms = 267;

    desc.render.game_name = "Mystical Ninja Starring Goemon";
    desc.render.gbi = rt64_3ds::Gbi::F3DEX;     // F3DEX 1.23
    desc.render.vi_width = 320;
    desc.render.vi_height = 240;
    desc.render.rules = goemon_stereo_rules;
    desc.render.rule_count = sizeof(goemon_stereo_rules) / sizeof(goemon_stereo_rules[0]);
    desc.render.frame_head_proj_id = PROJECTION_GAMEPLAY_TRANSFORM_ID;
    desc.render.unclassified_persp = StereoClass::None;
    desc.render.unclassified_ortho = StereoClass::Hud;
    // What the desktop renderer (lib/rt64, the Goemon3D branch) does for this
    // game, where rt64-3ds keeps DK64's and Banjo's behaviour by default.
    //
    // Backdrops end at x = 319 (background.c: the panorama rows, the 319-wide
    // strips, the strips split where their texture wraps). RT64 extends such
    // an edge to the scissor's 320 (rect.fixRectLR) and merges a row's slices
    // before deciding a rectangle covers the width; then it is stretched over
    // the 400-pixel screen instead of leaving the 40-pixel margins empty.
    desc.render.fix_rect_lr = true;
    desc.render.merge_rect_slices = true;
    // Files 22 and 23 set a (0,0)-(319,239) scissor, one pixel short of the
    // frame: an edge within 5 pixels of the frame's counts as the frame's, so
    // the scissor covers the width and the scene fills the wide screen.
    desc.render.scissor_edge_slack = 5;
    // The Impact battles' flashes (damage, transitions) are one untextured
    // quad in front of the camera, sized to the 4:3 frame: stretched to cover
    // the wide screen, on the screen plane.
    desc.render.stretch_tint_quads = true;
    // Backdrops at infinity shift by the full separation per eye; scaled by
    // 1 + that shift about the frame's centre they still cover the screen
    // (the desktop's skybox-rect cover zoom) instead of baring a strip.
    desc.render.infinity_rect_cover_zoom = true;
    // The menu boxes (Controller Pak question, Select Adventure Diary) copy
    // strips of the curtain just drawn into an 8-wide and a 256-wide image
    // with texture rectangles and draw their rims with them. The desktop
    // flushes its framebuffer pair at every colour image change for this
    // (lib/rt64 fb1dc22); rt64-3ds keeps a copy of each eye's frame for the
    // frames that do it.
    desc.render.sample_drawn_frame = true;
    // One HUD depth for every kind of HUD draw (desktop cf2eadf): the shift is
    // -c * separation per eye, c = (hud_depth - 50) / 50, clamped to +0.10
    // (behind) / -0.30 (in front) NDC, as stereoHudNdcOffset has it. rt64-3ds
    // scales the eye-independent term -c * 0.4 * separation by hud_k_*, so
    // 2.5 puts orthographic draws and the HUD camera's perspective items on
    // exactly -c * separation; rectangles (text, panels) follow the
    // orthographic HUD rather than their fixed slider-scaled offset.
    desc.render.hud_k_ortho = 2.5f;
    desc.render.hud_k_persp = 2.5f;
    desc.render.hud_k_bubble = 2.5f;
    desc.render.rect_hud_follows_separation = true;
    // HUD shifts on whole pixels, so text samples its texels the same way in
    // both eyes (the desktop rounds to whole native pixels; 320x240 is drawn
    // 1:1 on the top screen, so those are screen pixels here).
    desc.render.snap_hud_shift = true;
    // anime.c tags a camera near the origin looking down -Z as the HUD's.
    // The desktop never takes the frame's main world projection (the
    // full-width perspective with the most draws) for a HUD camera: the
    // opening hawk scene and the title's fly-in are shot through such a
    // camera, and would otherwise sit flat at the HUD's depth.
    desc.render.hud_camera_id = PROJECTION_HUD_TRANSFORM_ID;
    // ... and only one camera at a time: the desktop takes one projection
    // instance per frame as the world (the full-width one with the most
    // draws; rt64-3ds counts triangles); the HUD camera's other instances
    // stay HUD. On the Controller Pak and Adventure Diary screens the curtain
    // and every panel's 3D rim are drawn by HUD-looking cameras; without this
    // all of them became the world and the rims sat at another depth than
    // their fills and text. Now the curtain is the world, the rims HUD.
    desc.render.primary_world_instance = true;
    // The closest camera zoom draws Goemon under a projection of his own,
    // near plane 15 against the world's 35 (so he is not cut away), and the
    // ground just ahead, drawn first at the world's depth scale, then hid him
    // below a straight line across his hair. The world's draws take the depth
    // scale of the nearest of its projections, from their distance.
    desc.render.unify_world_depth = true;
    // Every rectangle but the backdrops and whole-frame tints takes the HUD
    // shift, as on the desktop, including panels filling their own scissor.
    desc.render.covering_rects_hud = true;
    // Goemon's world units are small (the useful convergence band is about
    // 10..100): the desktop's depth sampler floors convergence at 2, not 4.
    desc.render.autoconv.near_clamp = 2.0f;
    // The sky cylinder (background.c, sky_draw_3d) loads a 32-texel column of
    // its panorama for each of up to 400 quads; sampled from one texture of
    // the whole panorama they merge into one draw (280 draws fewer in the
    // heaviest opening frame).
    desc.render.merge_image_tiles = true;
    // The patches tag objects, skeletons, the sky and the cameras for RT64's
    // high frame rate (anime.c, background.c, tagging.c): offer "60
    // interpolated" on the Frame rate row, an interpolated frame between each
    // two of the game's while it runs at 30.
    desc.render.frame_interpolation = true;
    // GPU time per eye (four eye images per game frame with 3D and "60
    // interp."): 16-bit depth (from a gameplay capture, D16 resolves 0.7
    // world units at 200 units and 4.6 at 500, finer than the N64's own
    // 14-bit compressed z there: 1.3 and 8.4) and I/IA textures as LA8 (the
    // same texels in half the bytes).
    desc.render.eye_depth16 = true;
    desc.render.intensity_textures16 = true;
    // Every frame starts with a full-screen fill rectangle (and then the sky
    // covers it): the eyes are cleared to its colour instead of drawing it.
    desc.render.fill_clear = true;
    // "60 interp.": the in-betweens draw into eye targets of their own
    // (1125 KB of VRAM) and the game's frame of each pair is recorded while
    // the GPU still draws its in-between, instead of after (citro3d's frame
    // begin waits for all of it: an eye pair of GPU time a list on the
    // game's path). settings.ini's gpu_double_eyes switches it.
    desc.render.double_eyes = true;
    // The game's scheduler sends a graphics task's done message at the RDP's
    // completion: it hears that once the renderer has read the list, so the
    // renderer's interpretation and its waits for the screen (core 2) stay
    // off the game's frame time (nmr patch 0028).
    desc.render.early_drawn = true;
    // The audio task is libaudio's ABI 1 aspMain (rsp/aspMain.cpp), not the
    // naudio family: rt64-3ds interprets it on the CPU (aspmain_hle.cpp,
    // bit-exact with the recompiled microcode, which AUDIO_RSP.TXT selects).
    desc.audio_hle = true;
    desc.audio_hle_family = recomp3ds::AudioHle::Aspmain;
    // Goemon's audio manager sizes each buffer from what is still queued and
    // keeps about one frame ahead. With the one-frame margin the DSP ran dry
    // between buffers on hardware (43,800 frames/s submitted against 44,095
    // played: a constant crackle at full speed), so keep four frames in reserve.
    desc.audio_margin_frames = 4;
    // The audio manager is the SDK's (one task a retrace, the next built
    // after the previous one's completion message): at the Old 3DS clock the
    // interpreter's 10-17 ms a task made it miss every other retrace and the
    // music ran at half speed. Its samples reach the host through
    // osAiSetNextBuffer alone, so the tasks can complete asynchronously.
    desc.audio_async = true;
    // The game drives libaudio's synthesizer directly (its own sequencer, no
    // libaudio sequence player): mnsg.toml hooks alSyn* and alAudioFrame so
    // the Audio engine row can play its voices on the DSP instead.
    desc.synth = recomp3ds::SynthFlavor::Libaudio;
    desc.button_map = kButtons;
    desc.button_map_count = sizeof(kButtons) / sizeof(kButtons[0]);
    desc.menu_options = kOptions;
    desc.menu_option_count = sizeof(kOptions) / sizeof(kOptions[0]);
    desc.menu_defaults = kDefaults;
    desc.menu_default_count = sizeof(kDefaults) / sizeof(kDefaults[0]);
#ifdef GOEMON_NULL_RENDERER
    desc.null_renderer = true;
#endif

    // The sky cylinder's game-side work (up to 400 quads a frame) is more
    // than an Old 3DS's CPU can spare: there the Sky row starts on the flat
    // scroll. settings.ini, once written, keeps what the player chose.
    {
        bool new3ds = false;
        APT_CheckNew3DS(&new3ds);
        g_goemon_sky_3d = new3ds ? 1 : 0;
    }

    return recomp3ds::run(desc);
}
