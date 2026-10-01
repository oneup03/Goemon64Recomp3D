#ifndef __GOEMON_CONFIG_H__
#define __GOEMON_CONFIG_H__

#include <cstdint>
#include <filesystem>
#include <string_view>
#include "common/rt64_user_configuration.h"
#include "ultramodern/config.hpp"
#include "recomp_input.h"

namespace goemon64 {
    constexpr std::u8string_view program_id = u8"Goemon64Recompiled";
    constexpr std::string_view program_name = "Goemon 64: Recompiled";

    // TODO: Move loading configs to the runtime once we have a way to allow per-project customization.
    void load_config();
    void save_config();
    
    void reset_input_bindings();
    void reset_cont_input_bindings();
    void reset_kb_input_bindings();
    void reset_single_input_binding(recomp::InputDevice device, recomp::GameInput input);

    std::filesystem::path get_app_folder_path();
    
    bool get_debug_mode_enabled();
    void set_debug_mode_enabled(bool enabled);
    
    enum class AutosaveMode {
        On,
        Off,
        OptionCount
    };

    NLOHMANN_JSON_SERIALIZE_ENUM(goemon64::AutosaveMode, {
        {goemon64::AutosaveMode::On, "On"},
        {goemon64::AutosaveMode::Off, "Off"}
    });

    enum class TargetingMode {
        Switch,
        Hold,
        OptionCount
    };

    NLOHMANN_JSON_SERIALIZE_ENUM(goemon64::TargetingMode, {
        {goemon64::TargetingMode::Switch, "Switch"},
        {goemon64::TargetingMode::Hold, "Hold"}
    });

    TargetingMode get_targeting_mode();
    void set_targeting_mode(TargetingMode mode);

    enum class CameraInvertMode {
        InvertNone,
        InvertX,
        InvertY,
        InvertBoth,
        OptionCount
    };

    NLOHMANN_JSON_SERIALIZE_ENUM(goemon64::CameraInvertMode, {
        {goemon64::CameraInvertMode::InvertNone, "InvertNone"},
        {goemon64::CameraInvertMode::InvertX, "InvertX"},
        {goemon64::CameraInvertMode::InvertY, "InvertY"},
        {goemon64::CameraInvertMode::InvertBoth, "InvertBoth"}
    });

    CameraInvertMode get_camera_invert_mode();
    void set_camera_invert_mode(CameraInvertMode mode);

    CameraInvertMode get_analog_camera_invert_mode();
    void set_analog_camera_invert_mode(CameraInvertMode mode);

    enum class AnalogCamMode {
        On,
        Off,
		OptionCount
    };

    NLOHMANN_JSON_SERIALIZE_ENUM(goemon64::AnalogCamMode, {
        {goemon64::AnalogCamMode::On, "On"},
        {goemon64::AnalogCamMode::Off, "Off"}
    });

    AutosaveMode get_autosave_mode();
    void set_autosave_mode(AutosaveMode mode);

    AnalogCamMode get_analog_cam_mode();
    void set_analog_cam_mode(AnalogCamMode mode);

    void open_quit_game_prompt();

    // Stereoscopic 3D settings.
    struct StereoSettings {
        RT64::UserConfiguration::StereoMode mode = RT64::UserConfiguration::StereoMode::Off;
        // Clip-space separation, 0..50. Each point is 0.2% of screen width of
        // background disparity, so the range is 0..10% and the default is a
        // comfortable 2%. This is the whole 3D-strength knob: it carries no FoV
        // or convergence term, which is why the same number means the same
        // thing on any display and at any aspect ratio.
        uint32_t separation = 10;
        // Zero-parallax distance in GAME UNITS, 1..200. Goemon's cameras sit
        // close to the action, so the useful band is roughly 10..100.
        uint32_t convergence = 20;
        // 50 = screen plane. Below pushes the HUD behind, above pops it out.
        uint32_t hudDepth = 35;
        // Depth-driven auto-convergence: samples the depth buffer and pulls the
        // screen plane in when the nearest on-screen object would pop out past
        // the comfort target. The convergence slider stays the ceiling.
        bool autoConvergence = true;
        // Permitted pop-out before auto-convergence intervenes, in thousandths
        // of screen width, biased by +50 so it survives the unsigned config and
        // renderer bridge. 50 == 0, i.e. the screen plane lands exactly on the
        // nearest object; below 50 puts it in front of that, so the whole scene
        // sits behind the screen.
        uint32_t comfortTarget = 55;
        // Ghost reduction (anti-crosstalk). 100 / 0 are the exact no-ops.
        uint32_t ghostContrast = 100;
        uint32_t ghostBlackFloor = 0;
    };
    StereoSettings get_stereo_settings();
    void set_stereo_settings(const StereoSettings &settings);
};

#endif
