// The Goemon-specific host functions (patches/syms.ld) for the 3DS. The
// desktop versions in src/game/recomp_api.cpp read the launcher's
// configuration; here the values are fixed or come from the 3DS settings
// (main_3ds.cpp, goemon_settings.h).
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

#include "recomp.h"
#include "librecomp/helpers.hpp"
#include "librecomp/overlays.hpp"
#include "ultramodern/ultramodern.hpp"
#include "recomp3ds.h"
#include "goemon_settings.h"

namespace recomp3ds {
    void input_get_right_stick(float* x, float* y);   // input_hid.cpp
}

namespace {

// The top screen, the "window" the desktop's aspect ratio options see.
constexpr int kScreenWidth = 400;
constexpr int kScreenHeight = 240;
constexpr float kScreenAspect = (float)kScreenWidth / (float)kScreenHeight;

// goemon64::CameraInvertMode: 0 none, 1 X, 2 Y, 3 both.
void return_axes(uint8_t* rdram, recomp_context* ctx, int invert_mode) {
    s32* x_out = _arg<0, s32*>(rdram, ctx);
    s32* y_out = _arg<1, s32*>(rdram, ctx);
    *x_out = (invert_mode & 1) != 0;
    *y_out = (invert_mode & 2) != 0;
}

}   // namespace

// The desktop's options, in its units, set from the 3DS settings menu.
// Defaults as on the desktop (src/game/config.cpp, src/ui/ui_config.cpp).
int g_goemon_bgm_volume = 100;
int g_goemon_se_volume = 100;
int g_goemon_sky_3d = 1;
// Read by the recompiler hook in mnsg.toml at the audio manager's creation.
int g_goemon_audio_voices = 16;

// The host functions below that only the Majora's Mask template used
// (targeting, autosave, the analog camera, the camera inversions): no Goemon
// patch calls them. They stay for the symbol table (patches/syms.ld) and answer
// with the desktop's defaults.
constexpr s32 kTargetingSwitch = 0;
constexpr int kInvertY = 2, kInvertNone = 0;

// The desktop polls its input devices here; the 3DS reads the buttons when
// the game reads the controller.
extern "C" void recomp_update_inputs(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram; (void)ctx;
}

extern "C" void recomp_puts(uint8_t* rdram, recomp_context* ctx) {
    PTR(char) cur_str = _arg<0, PTR(char)>(rdram, ctx);
    u32 length = _arg<1, u32>(rdram, ctx);
    for (u32 i = 0; i < length; i++) {
        fputc(MEM_B(i, (gpr)cur_str), stderr);
    }
}

extern "C" void recomp_exit(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram; (void)ctx;
    ultramodern::quit();
}

extern "C" void recomp_get_gyro_deltas(uint8_t* rdram, recomp_context* ctx) {
    float* x_out = _arg<0, float*>(rdram, ctx);
    float* y_out = _arg<1, float*>(rdram, ctx);
    *x_out = 0.0f; *y_out = 0.0f;
}

extern "C" void recomp_get_mouse_deltas(uint8_t* rdram, recomp_context* ctx) {
    float* x_out = _arg<0, float*>(rdram, ctx);
    float* y_out = _arg<1, float*>(rdram, ctx);
    *x_out = 0.0f; *y_out = 0.0f;
}

extern "C" void recomp_powf(uint8_t* rdram, recomp_context* ctx) {
    float a = _arg<0, float>(rdram, ctx);
    float b = ctx->f14.fl;
    _return(ctx, std::pow(a, b));
}

extern "C" void recomp_get_target_framerate(uint8_t* rdram, recomp_context* ctx) {
    int frame_divisor = _arg<0, u32>(rdram, ctx);
    _return(ctx, ultramodern::get_target_framerate(60 / frame_divisor));
}

extern "C" void recomp_get_window_resolution(uint8_t* rdram, recomp_context* ctx) {
    gpr width_out = _arg<0, PTR(u32)>(rdram, ctx);
    gpr height_out = _arg<1, PTR(u32)>(rdram, ctx);
    MEM_W(0, width_out) = kScreenWidth;
    MEM_W(0, height_out) = kScreenHeight;
}

// The desktop's Expand option (its default): the window's shape, never
// narrower than the game's.
extern "C" void recomp_get_target_aspect_ratio(uint8_t* rdram, recomp_context* ctx) {
    float original = _arg<0, float>(rdram, ctx);
    _return(ctx, std::max(kScreenAspect, original));
}

extern "C" void recomp_get_targeting_mode(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    _return<s32>(ctx, kTargetingSwitch);
}

// The game's volumes are scaled by these factors (1.0 = unchanged).
extern "C" void recomp_get_bgm_volume(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    _return(ctx, (float)g_goemon_bgm_volume / 100.0f);
}

// The sky cylinder or the game's flat scroll (patches/background.c, the Sky row).
extern "C" void recomp_get_sky_3d(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    _return(ctx, static_cast<s32>(g_goemon_sky_3d != 0 ? 1 : 0));
}

extern "C" void recomp_get_se_volume(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    _return(ctx, (float)g_goemon_se_volume / 100.0f);
}

extern "C" void recomp_time_us(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    _return(ctx, static_cast<u32>(std::chrono::duration_cast<std::chrono::microseconds>(ultramodern::time_since_start()).count()));
}

extern "C" void recomp_get_autosave_enabled(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    _return<s32>(ctx, 1);   // on
}

extern "C" void recomp_load_overlays(uint8_t* rdram, recomp_context* ctx) {
    u32 rom = _arg<0, u32>(rdram, ctx);
    PTR(void) ram = _arg<1, PTR(void)>(rdram, ctx);
    u32 size = _arg<2, u32>(rdram, ctx);
    load_overlays(rom, ram, size);
}

extern "C" void recomp_unload_overlays(uint8_t* rdram, recomp_context* ctx) {
    PTR(void) vram_addr = _arg<0, PTR(void)>(rdram, ctx);
    u32 size = _arg<1, u32>(rdram, ctx);
    unload_overlays(vram_addr, size);
}

extern "C" void recomp_high_precision_fb_enabled(uint8_t* rdram, recomp_context* ctx) { (void)rdram; _return(ctx, static_cast<s32>(0)); }
extern "C" void recomp_get_resolution_scale(uint8_t* rdram, recomp_context* ctx) { (void)rdram; _return(ctx, 1.0f); }

extern "C" void recomp_get_inverted_axes(uint8_t* rdram, recomp_context* ctx) { return_axes(rdram, ctx, kInvertY); }
extern "C" void recomp_get_analog_inverted_axes(uint8_t* rdram, recomp_context* ctx) { return_axes(rdram, ctx, kInvertNone); }

extern "C" void recomp_get_analog_cam_enabled(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram;
    _return<s32>(ctx, 0);   // off
}

// The analog camera's stick, were a patch to read it: the C-Stick, with the
// C-Stick deadzone of the Controls page.
extern "C" void recomp_get_camera_inputs(uint8_t* rdram, recomp_context* ctx) {
    float* x_out = _arg<0, float*>(rdram, ctx);
    float* y_out = _arg<1, float*>(rdram, ctx);
    recomp3ds::input_get_right_stick(x_out, y_out);
}

// Set by the game while the analog camera may turn: the C-Stick then stops
// pressing C buttons (the desktop drops the right stick's button bindings).
extern "C" void recomp_set_right_analog_suppressed(uint8_t* rdram, recomp_context* ctx) {
    recomp3ds::input_set_cstick_buttons(_arg<0, s32>(rdram, ctx) == 0);
}

// The desktop runs its UI's queued callbacks here (patches/main.c, every
// frame); the 3DS has none.
extern "C" void recomp_run_ui_callbacks(uint8_t* rdram, recomp_context* ctx) {
    (void)rdram; (void)ctx;
}
