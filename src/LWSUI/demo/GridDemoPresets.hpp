#pragma once
#include <LWSUI/GridView.hpp>
namespace LWSUI::demo::grid
{
    struct Preset
    {
        Choice choice;
        std::string description;
        GridOptions options;
        bool pinnedColumns = false, list = false, externalModel = false, randomModel = false, wideModel = false;
        std::string headerStyle = "default";
    };
    inline Preset MakePreset(Choice choice, std::string description, std::function<void(Preset&)> configure = {})
    {
        Preset result{std::move(choice), std::move(description)};
        result.options.multipleSelection = true;
        result.options.rowHeight = 28;
        result.options.headerHeight = 30;
        if (configure)
            configure(result);
        return result;
    }
    inline std::vector<Preset> Presets()
    {
        return {
            MakePreset({"Comfortable", "comfortable"},
                       "Balanced spacing for everyday browsing and editing, with full-row selection."),
            MakePreset({"Compact", "compact"}, "Denser rows show more records when screen space is limited.",
                       [](Preset& p)
                       {
                           p.options.rowHeight = 24;
                           p.options.headerHeight = 26;
                       }),
            MakePreset({"Striped", "striped"}, "Alternating backgrounds help you follow each row across a wide table.",
                       [](Preset& p)
                       {
                           p.options.striped = true;
                           p.options.verticalLines = false;
                       }),
            MakePreset({"Styled headers", "headers"},
                       "Custom header colors and alignment make column categories stand out.",
                       [](Preset& p) { p.headerStyle = "blue"; }),
            MakePreset({"Read-only", "readonly"}, "Browse, select, sort, and copy data while preventing user edits.",
                       [](Preset& p) { p.options.readOnly = true; }),
            MakePreset({"Mixed editors", "editors"},
                       "Click checkboxes to toggle and drag progress bars to adjust values. Text, numbers, choices, "
                       "and Tags "
                       "use in-place editors. Empty "
                       "names are rejected.",
                       [](Preset& p)
                       {
                           p.options.fullRowSelect = false;
                           p.options.striped = true;
                           p.options.singleClickEdit = true;
                       }),
            MakePreset({"Sticky columns", "sticky"},
                       "Keep identifying and status columns visible while scrolling horizontally through a wide table.",
                       [](Preset& p) { p.pinnedColumns = true; }),
            MakePreset({"Single-column list", "list"},
                       "The same selection and editing behavior in a simple list without column headers.",
                       [](Preset& p)
                       {
                           p.list = true;
                           p.options.headerVisible = false;
                           p.options.verticalLines = false;
                       }),
            MakePreset({"Large external model", "large"},
                       "Explore a million-row external dataset with virtual scrolling, without creating a control for "
                       "every row.",
                       [](Preset& p) { p.externalModel = true; }),
            MakePreset({"Random 100 million rows", "random100m"},
                       "100,000,000 virtual random rows. Drag the scrollbar inward for finer control. "
                       "Horizontal row mode estimates its scroll unit using row height.",
                       [](Preset& p)
                       {
                           p.randomModel = true;
                           p.options.dynamicResolution = true;
                           p.options.lineNumbers = true;
                           p.options.readOnly = true;
                           p.options.multipleSelection = false;
                           p.options.editing = p.options.sorting = p.options.selectAll = p.options.resizing = false;
                       }),
            MakePreset({"Wide columns", "wide"}, "1,000 virtual rows and 1,000 columns. Drag the bottom scrollbar upward for finer horizontal scrolling.",
                       [](Preset& p)
                       {
                           p.wideModel = true;
                           p.options.dynamicResolution = p.options.lineNumbers = p.options.readOnly = true;
                           p.options.multipleSelection = false;
                           p.options.editing = p.options.sorting = p.options.selectAll = p.options.resizing = false;
                       })};
    }
    inline Theme CompactTheme(ThemePreset preset)
    {
        auto theme = MakeTheme(preset);
        theme.font.size = 14;
        theme.headingFont.size = 16;
        theme.smallFont.size = 12;
        theme.glyphFont.size = 9;
        theme.rowHeight = 28;
        theme.checkboxSize = 16;
        theme.padding = 4;
        theme.textPadding = 3;
        theme.labelPadding = 3;
        theme.choiceGap = 8;
        theme.scrollbarWidth = 14;
        theme.scrollbarMargin = 4;
        return theme;
    }
    inline Theme ConfigurationTheme(const Theme& base)
    {
        auto theme = base;
        theme.font.size = 12;
        theme.headingFont.size = 14;
        theme.glyphFont.size = 8;
        theme.rowHeight = 24;
        theme.checkboxSize = 14;
        theme.choiceGap = 6;
        theme.labelPadding = 2;
        theme.scrollbarWidth = 12;
        return theme;
    }
}  // namespace LWSUI::demo::grid
