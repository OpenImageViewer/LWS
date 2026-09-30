#pragma once
#include <LWSUI/UIHost.hpp>
#include <LWS/Platform.hpp>
#include <functional>
#include <memory>
#include <span>
#include <string_view>

namespace LWSUI::demo
{
    inline constexpr ThemePreset DefaultThemePreset = ThemePreset::Light;
    class SplitPreview;

    int RunShowcase(int argc, char** argv);

    struct Sample
    {
        std::string_view type;
        Control* control;
    };
    // Demo-only application model. Native windows outlive their hosts and all subscriptions.
    class Showcase
    {
      public:

        Showcase(LWS::PlatformContext&, UIHost& menuHost);
        ~Showcase();
        Showcase(const Showcase&) = delete;
        Showcase& operator=(const Showcase&) = delete;
        void Command(std::string name);
        void SetTheme(ThemePreset preset);
        void Reset();
        void Snapshot();
        bool Verify();
        std::span<const Sample> Samples() const;
        LWS::Window& MainWindow();
        SplitPreview& PreviewContainers();
        std::function<void(ThemePreset)> OnThemeChanged;

      private:

        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}  // namespace LWSUI::demo
