#include "patches.h"
#include "input.h"
#include "variables.h"

// @recomp The analog camera (the 3DS's C-Stick; off on the desktop, whose
// recomp_get_analog_cam_enabled says so). The game turns its field camera 45
// degrees at each press of C-Left/Right with R held; here the C-Stick's left
// and right turn it smoothly instead (rt64-3ds leaves that axis to the game:
// GameDesc::cstick_camera_analog_x), while its up and down still hold R and
// press C-Up/C-Down, the game's own zoom. Both turns go through the game's
// own: a target yaw and its direction, which the camera modes follow at up to
// 0x400 a frame (func_801D7900_593810), so the stick turns the camera wherever
// R + C-Left/Right does.
//
// The game swings the camera back behind the player once they stand still for
// 21 frames (the camera update's idle count; while they move it keeps its
// yaw), which undid each turn as soon as the stick let go. Once the stick has
// turned the camera it stays where it was left (the count held at 0), as with
// sirdankz's BetterCam, until a tap of R (pressed and let go with no C button
// in between) turns it back behind the player, as Killklli's Camera Snap does
// while R is held (CC0); the game's own swing back then returns.

extern u16 D_8020D1D6_5C90E6;       // the camera's yaw (0x10000 a turn)
extern u8 D_8020D1A2_5C90B2;
extern void func_801D8F94_594EA4(void *camera, void *state);
extern void func_801D896C_59487C(void *camera, void *state);
extern void func_801D90D8_594FE8(void *camera);

// The camera object's fields the turn uses.
#define CAM_BEHIND_YAW(c) (*(u16 *)((u8 *)(c) + 0x66))  // the yaw behind the player
#define CAM_TURNING(c)    (*(u8 *)((u8 *)(c) + 0xA3))   // turning toward CAM_TURN_TO: 1 up, 2 down
#define CAM_EASE_BACK(c)  (*(u8 *)((u8 *)(c) + 0xAB))   // easing back behind the player
#define CAM_TURN_TO(c)    (*(u16 *)((u8 *)(c) + 0xCE))
#define CAM_IDLE(c)       (*(u8 *)((u8 *)(c) + 0xE8))   // frames the player has stood still (func_801D48E0_5907F0)
// The buttons the camera reads (func_801D896C_59487C), held.
#define CAM_HELD (*(u16 *)((u8 *)&D_8008CCC0_8D8C0 + 0x3B152))

// The yaw a frame of the stick pushed all the way turns.
#define TURN_MAX 0x280
// The longest press of R that is a tap, in camera updates (the game's frames).
#define RECENTER_TAP_FRAMES 10

static u8 s_r_frames;       // updates R has been held for (0: up)
static u8 s_r_with_c;       // a C button came while R was down
static u8 s_left_turned;    // the stick turned the camera: it stays put until R
static void *s_camera;      // the camera object it did so for
static u8 s_turn_dir;       // the turn this set (1 up, 2 down; 0: none), toward s_turn_to
static u16 s_turn_to;
static u8 s_turn_by_stick;  // ... for the stick (else R's turn back behind the player)

static void turn_camera_to(void *camera, u16 yaw, u8 by_stick) {
    s16 d = (s16)(yaw - D_8020D1D6_5C90E6);
    if (d == 0) {
        return;
    }
    s_turn_dir = (d > 0) ? 1 : 2;
    s_turn_to = yaw;
    s_turn_by_stick = by_stick;
    CAM_TURN_TO(camera) = yaw;
    CAM_TURNING(camera) = s_turn_dir;
    CAM_EASE_BACK(camera) = 0;
}

// Ends a turn this set once it is over. The game's turn stops only within
// 0x20 of its target, so a yaw something else moved past it went on round, a
// whole turn at 0x400 a frame (two seconds) after the stick let go. A turn
// for the stick ends as the stick lets go (its target is one frame's step
// ahead at most), R's when the yaw reaches or passes its target.
static void end_own_turn(void *camera, u8 stick_released) {
    s16 left;

    if (s_turn_dir == 0) {
        return;
    }
    if ((CAM_TURNING(camera) != s_turn_dir) || (CAM_TURN_TO(camera) != s_turn_to)) {
        s_turn_dir = 0;     // over, or the game's own turn took its place
        return;
    }
    left = (s16)(s_turn_to - D_8020D1D6_5C90E6);
    if ((s_turn_by_stick && stick_released) || ((s_turn_dir == 1) ? (left <= 0) : (left >= 0))) {
        CAM_TURNING(camera) = 0;
        s_turn_dir = 0;
    }
}

static void analog_camera(void *camera) {
    float x, y;
    s32 inv_x, inv_y;
    u16 held = CAM_HELD;

    if (!recomp_get_analog_cam_enabled()) {
        s_r_frames = 0;
        s_left_turned = FALSE;
        s_turn_dir = 0;
        return;
    }
    // Another camera (another area): the game's own behaviour again.
    if (camera != s_camera) {
        s_camera = camera;
        s_left_turned = FALSE;
        s_turn_dir = 0;
    }

    recomp_get_camera_inputs(&x, &y);
    recomp_get_analog_inverted_axes(&inv_x, &inv_y);
    if (inv_x) {
        x = -x;
    }
    end_own_turn(camera, x == 0.0f);
    // The stick right turns the camera as C-Right does: the yaw goes down.
    if (x != 0.0f) {
        turn_camera_to(camera, (u16)(D_8020D1D6_5C90E6 - (s32)(x * TURN_MAX)), TRUE);
        s_left_turned = TRUE;
    }
    if (s_left_turned) {
        CAM_IDLE(camera) = 0;
    }

    if (held & R_TRIG) {
        if (s_r_frames < 0xFF) {
            s_r_frames++;
        }
        if (held & (U_CBUTTONS | D_CBUTTONS | L_CBUTTONS | R_CBUTTONS)) {
            s_r_with_c = TRUE;
        }
    }
    else {
        if ((s_r_frames != 0) && (s_r_frames <= RECENTER_TAP_FRAMES) && !s_r_with_c) {
            turn_camera_to(camera, CAM_BEHIND_YAW(camera), FALSE);
            s_left_turned = FALSE;
        }
        s_r_frames = 0;
        s_r_with_c = FALSE;
    }
}

// @recomp The camera's per-frame input step, as the game has it, with the
// analog camera's turn after the game's own (R + C) input.
RECOMP_PATCH void func_801D85B0_5944C0(void *camera, void *arg1) {
    void *state = (void *)(*(u32 *)((u8 *)arg1 + 0x2C) & 0x8FFFFFFE);

    func_801D8F94_594EA4(camera, state);
    if (*((u8 *)arg1 + 0x64) != 0) {
        return;
    }
    func_801D896C_59487C(camera, state);
    analog_camera(camera);
    if (*(u16 *)((u8 *)camera + 0x5C) == 0) {
        return;
    }
    *((u8 *)camera + 0x98) = D_8020D1A2_5C90B2;
    *((u8 *)camera + 0x73) = D_8020D1A2_5C90B2;
    func_801D90D8_594FE8(camera);
}
