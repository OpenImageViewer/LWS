#pragma once
#include <LWSUI/TextStyle.hpp>
namespace LWSUI
{
    enum class ThemePreset
    {
        Dark,
        Light,
        Warm
    };
    inline constexpr ThemePreset DefaultThemePreset = ThemePreset::Light;
    inline const char* ThemePresetName(ThemePreset preset)
    {
        switch (preset)
        {
            case ThemePreset::Light:
                return "light";
            case ThemePreset::Warm:
                return "warm";
            default:
                return "dark";
        }
    }
    struct Theme
    {
        LLUtils::Color background{uint32_t{0xf4f6f8ff}}, foreground{uint32_t{0x20252bff}};
        LLUtils::Color surface{uint32_t{0xffffffff}}, accent{uint32_t{0x005fccff}};
        LLUtils::Color changed{uint32_t{0x20252bff}}, error{uint32_t{0xb42318ff}};
        LLUtils::Color hoverSurface{uint32_t{0xdce6f0ff}};
        LLUtils::Color closeHoverBackground{uint32_t{0xe81123ff}}, closePressedBackground{uint32_t{0xc50f1fff}};
        LLUtils::Color closeActiveForeground{uint32_t{0xffffffff}};
        LLUtils::Color line{uint32_t{0xcbd5dfff}}, muted{uint32_t{0x546170ff}};
        LLUtils::Color selectedRow{uint32_t{0xdceafaff}}, hoveredRow{uint32_t{0xe8eef5ff}};
        LLUtils::Color selection{uint32_t{0xb8d8faff}}, selectedOption{uint32_t{0xdceafaff}};
        LLUtils::Color popupSurface{uint32_t{0xffffffff}};
        LLUtils::Color checkerDark{uint32_t{0x999999ff}}, checkerLight{uint32_t{0xddddddff}};
        LLUtils::Color modalOverlay{uint32_t{0x000000a0}};
        FontSpec font{16}, smallFont{96.f / 7}, headingFont{120.f / 7}, titleFont{184.f / 7}, glyphFont{80.f / 7};
        LLUtils::Color searchForeground{uint32_t{0x292315ff}}, searchBackground{uint32_t{0xffeda3ff}};
        LLUtils::Color selectionForeground{uint32_t{0x20252bff}};
        // All geometry is in logical pixels, independent of typography.
        float spacing = 0, rowHeight = 240.f / 7, padding = 8;
        float labelPadding = 4, textPadding = 6, borderWidth = 1, focusWidth = 2;
        float checkboxSize = 160.f / 7, choiceGap = 80.f / 7, radioHeight = 216.f / 7;
        float rockerWidth = 200.f / 7, browseWidth = 40, swatchWidth = 34, swatchGap = 4, checkerSize = 8;
        float sliderThickness = 4, sliderThumbWidth = 8, scrollbarWidth = 128.f / 7;
        float scrollbarMargin = 48.f / 7, scrollbarInset = 16.f / 7, scrollbarMinThumb = 192.f / 7;
        float scrollStep = 352.f / 7;
        float treeRowHeight = 352.f / 7, treeIndent = 144.f / 7, treeLabelInset = 208.f / 7;
        float treeRowInset = 96.f / 7, treeLineInset = 176.f / 7, treeRowPadding = 48.f / 7;
        float editorMinLeft = 1600.f / 7, editorFraction = .47f, resetWidth = 544.f / 7;
        float menuRowHeight = 224.f / 7, menuMaxRows = 8, menuPadding = 40.f / 7;
        float menuBarHeight = 320.f / 7, menuBarPadding = 96.f / 7, menuArrowWidth = 128.f / 7;
        int menuHoverDelayMs = 400;
        float popupWidth = 720, popupHeight = 600, popupPadding = 160.f / 7;
        float popupGap = 96.f / 7, popupPreviewHeight = 384.f / 7;
        float popupChannelHeight = 192.f / 7, popupButtonWidth = 640.f / 7;
        int repeatDelayMs = 400, repeatIntervalMs = 65, accelerationMs = 1500;
        // Match the settings form's outer horizontal inset; independent of column padding.
        float resetRightMargin = 160.f / 7;
    };
    inline Theme MakeTheme(ThemePreset preset = DefaultThemePreset)
    {
        Theme t;
        if (preset == ThemePreset::Light)
            return t;
        if (preset == ThemePreset::Dark)
        {
            t.background = LLUtils::Color{uint32_t{0x171c25ff}};
            t.foreground = LLUtils::Color{uint32_t{0xe5eaf2ff}};
            t.surface = LLUtils::Color{uint32_t{0x111720ff}};
            t.accent = LLUtils::Color{uint32_t{0x66b7ffff}};
            t.changed = LLUtils::Color{uint32_t{0xfbbf24ff}};
            t.error = LLUtils::Color{uint32_t{0xff8080ff}};
            t.hoverSurface = LLUtils::Color{uint32_t{0x354761ff}};
            t.line = LLUtils::Color{uint32_t{0x303b4dff}};
            t.muted = LLUtils::Color{uint32_t{0x929fb4ff}};
            t.selectedRow = LLUtils::Color{uint32_t{0x26394eff}};
            t.hoveredRow = LLUtils::Color{uint32_t{0x202b3aff}};
            t.selection = LLUtils::Color{uint32_t{0x426389ff}};
            t.selectedOption = LLUtils::Color{uint32_t{0x2c4058ff}};
            t.popupSurface = LLUtils::Color{uint32_t{0x263344ff}};
            t.searchForeground = LLUtils::Color{uint32_t{0x171c25ff}};
            t.searchBackground = LLUtils::Color{uint32_t{0xffd166ff}};
            t.selectionForeground = LLUtils::Color{uint32_t{0xffffffff}};
            return t;
        }
        const bool warm = preset == ThemePreset::Warm;
        const auto color = [](uint32_t rgb) { return LLUtils::Color{(rgb << 8) | 255}; };
        t.background = color(warm ? 0xf4ede2 : 0xf4f6f8);
        t.surface = color(warm ? 0xfffaf2 : 0xffffff);
        t.foreground = color(warm ? 0x352e27 : 0x20252b);
        t.accent = color(warm ? 0x854510 : 0x005fcc);
        t.changed = t.foreground;
        t.error = color(0xb42318);
        t.muted = color(warm ? 0x6d6255 : 0x546170);
        t.hoverSurface = color(warm ? 0xe5d5be : 0xdce6f0);
        t.line = color(warm ? 0xd8c8b2 : 0xcbd5df);
        t.selectedRow = color(warm ? 0xeadac2 : 0xdceafa);
        t.hoveredRow = color(warm ? 0xeee2d0 : 0xe8eef5);
        t.selection = color(warm ? 0xd9b98d : 0xb8d8fa);
        t.selectedOption = t.selectedRow;
        t.selectionForeground = t.foreground;
        t.popupSurface = t.surface;
        t.searchForeground = color(0x292315);
        t.searchBackground = color(warm ? 0xf2d79c : 0xffeda3);
        return t;
    }
}  // namespace LWSUI
