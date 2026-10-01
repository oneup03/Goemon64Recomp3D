#ifndef __GOEMON_RENDER_H__
#define __GOEMON_RENDER_H__

#include <unordered_set>
#include <filesystem>

#include "common/rt64_user_configuration.h"
#include "ultramodern/renderer_context.hpp"
#include "librecomp/mods.hpp"

namespace RT64 {
    struct Application;
}

namespace goemon64 {
    namespace renderer {
        inline const std::string special_option_texture_pack_enabled = "_recomp_texture_pack_enabled";

        class RT64Context final : public ultramodern::renderer::RendererContext {
        public:
            ~RT64Context() override;
            RT64Context(uint8_t *rdram, ultramodern::renderer::WindowHandle window_handle, bool developer_mode);

            bool valid() override { return static_cast<bool>(app); }

            bool update_config(const ultramodern::renderer::GraphicsConfig &old_config, const ultramodern::renderer::GraphicsConfig &new_config) override;

            void enable_instant_present() override;
            void send_dl(const OSTask *task) override;
            void update_screen() override;
            void shutdown() override;
            uint32_t get_display_framerate() const override;
            float get_resolution_scale() const override;

        private:
            std::unique_ptr<RT64::Application> app;
            std::unordered_set<std::string> enabled_texture_packs;
            std::unordered_set<std::string> secondary_disabled_texture_packs;

            void check_texture_pack_actions();
        };

        std::unique_ptr<ultramodern::renderer::RendererContext> create_render_context(uint8_t *rdram, ultramodern::renderer::WindowHandle window_handle, bool developer_mode);

        RT64::UserConfiguration::Antialiasing RT64MaxMSAA();
        bool RT64SamplePositionsSupported();
        bool RT64HighPrecisionFBEnabled();

        void trigger_texture_pack_update();
        void enable_texture_pack(const recomp::mods::ModContext& context, const recomp::mods::ModHandle& mod);
        void disable_texture_pack(const recomp::mods::ModHandle& mod);
        void secondary_enable_texture_pack(const std::string& mod_id);
        void secondary_disable_texture_pack(const std::string& mod_id);

        // Push stereoscopic 3D settings from the UI/config thread to the RT64
        // application thread. Values are clamped to valid slider ranges
        // (separation: 0..50 clip-space points, convergence: 1..200 game units,
        // hudDepth: 0..100, comfortTarget: 0..100 biased by +50, ghostContrast
        // and ghostBlackFloor: 0..100 percent). The change is applied atomically
        // once per RT64 frame by an internal apply_pending_stereo_config helper.
        void set_stereo_config(RT64::UserConfiguration::StereoMode mode, uint32_t separation, uint32_t convergence,
            uint32_t hudDepth, bool autoConvergence, uint32_t comfortTarget, uint32_t ghostContrast, uint32_t ghostBlackFloor);

        // Per-frame runtime signal from the game (via the recomp export
        // recomp_stereo_set_low_convergence_scene) indicating whether the
        // current scene frames things close — FMVs, file select, first-person,
        // minigames. Under the depth-driven convergence loop this no longer
        // scales convergence directly; it tightens the loop's comfort budget,
        // so the two mechanisms compose instead of fighting.
        void set_stereo_runtime_low_convergence(bool active);

        // Texture pack enable option. Must be an enum with two options.
        // The first option is treated as disabled and the second option is treated as enabled.
        bool is_texture_pack_enable_config_option(const recomp::mods::ConfigOption& option, bool show_errors);
    }
}

#endif
