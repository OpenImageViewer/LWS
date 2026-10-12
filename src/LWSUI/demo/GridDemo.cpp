#include <LWS/Platform.hpp>
#include <LWS/Timer.hpp>
#ifdef LWS_HAS_WIN32_BACKEND
    #include <LWS/Win32/Platform.hpp>
    #include <LWS/Win32/WindowExtensions.hpp>
#endif
#include <LWSUI/GridView.hpp>
#include "GridDemoPresets.hpp"
#include "ScrollFeedback.hpp"
#include <LWSUI/Composites.hpp>
#include <LWSUI/UIHost.hpp>
#include <fstream>
#include <cmath>
#include <iostream>
#include <sstream>
#include <deque>
#include <unordered_map>
using namespace LWSUI;
namespace
{
    void ReportError(const std::string& message, bool interactive)
    {
        std::cerr << "LWSUI grid demo: " << message << '\n';
#ifdef LWS_HAS_WIN32_BACKEND
        OutputDebugStringA(("LWSUI grid demo: " + message + "\n").c_str());
        if (interactive)
            MessageBoxW(nullptr, NativeText(message).c_str(), L"LWSUI GridView", MB_OK | MB_ICONERROR);
#else
        (void) interactive;
#endif
    }
    constexpr float ProgressPadding = 6;
    constexpr double ProgressStep = .05;
    std::vector<GridModelColumn> Definitions()
    {
        std::vector<GridModelColumn> columns{{"name"},
                                             {"team", GridValueType::Choice},
                                             {"count", GridValueType::Integer},
                                             {"cost", GridValueType::Decimal},
                                             {"progress", GridValueType::Decimal},
                                             {"tag"},
                                             {"enabled", GridValueType::Boolean}};
        columns[1].choices = {{"Design", "design"}, {"Engineering", "engineering"}, {"Support", "support"}};
        columns[2].integer = {0, 1000000, 1};
        columns[3].decimal = {0., 1000000., .25};
        columns[4].decimal = {0., 1., ProgressStep};
        return columns;
    }
    std::vector<GridValue> Values(GridRowId row)
    {
        const auto i = row - 1;
        return {std::string("Item ") + std::to_string(row),
                std::string(i % 3 == 0   ? "design"
                            : i % 3 == 1 ? "engineering"
                                         : "support"),
                int64_t(i * 7 % 900),
                double(i * 13 % 15000) / 4.,
                double(i % 101) / 100.,
                std::string("DEMO"),
                bool(i % 4 != 0)};
    }
    std::vector<GridValue> RandomValues(GridRowId row)
    {
        auto values = Values(row);
        uint64_t seed = row ^ 0x9e3779b97f4a7c15ULL;
        const auto next = [&]()
        {
            seed += 0x9e3779b97f4a7c15ULL;
            auto value = seed;
            value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
            value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
            return value ^ (value >> 31);
        };
        values[0] = "Random " + std::to_string(next() % 1000000000);
        values[1] = std::string(next() % 3 == 0 ? "design" : next() % 2 == 0 ? "engineering" : "support");
        values[2] = int64_t(next() % 1000001);
        values[3] = double(next() % 4000001) / 4;
        values[4] = double(next() % 101) / 100;
        values[5] = "R" + std::to_string(next() % 10000);
        values[6] = bool(next() & 1);
        return values;
    }
    class SpeedProgress final : public Control
    {
      public:
        void SetSpeed(double value) { speed_ = value; Invalidate(); }
      protected:
        Size OnMeasure(Size available) override { return {std::min(100.f, available.width), 16}; }
        Size OnMeasureContent(Size available) override { return OnMeasure(available); }
        void OnRender(Canvas& canvas) override
        {
            const auto b = Bounds();
            canvas.Fill(b.x, b.y, b.width, b.height, Style().surface);
            canvas.Fill(b.x, b.y, b.width * float(std::clamp(speed_ / 100., 0., 1.)), b.height, Style().accent);
        }
      private:
        double speed_ = 100;
    };
    class LargeModel final : public GridModel
    {
      public:

        explicit LargeModel(bool random = false) : random_(random) {}
        std::span<const GridModelColumn> Columns() const override { return columns_; }
        size_t RowCount() const override { return random_ ? 100000000 : 1000000; }
        GridRowId RowId(size_t i) const override
        {
            if (i >= RowCount())
                throw std::out_of_range("Row index");
            return i + 1;
        }
        std::optional<size_t> IndexOf(GridRowId id) const override
        {
            return id && id <= RowCount() ? std::optional<size_t>(id - 1) : std::nullopt;
        }
        GridValue Value(const GridCell& cell) const override
        {
            ++reads;
            if (!IndexOf(cell.row))
                throw std::out_of_range("Row identity");
            auto it = std::ranges::find(columns_, cell.column, &GridModelColumn::id);
            if (it == columns_.end())
                throw std::out_of_range("Column identity");
            const auto col = size_t(it - columns_.begin());
            auto changed = overrides_.find(cell.row);
            return changed != overrides_.end() ? changed->second[col] : (random_ ? RandomValues(cell.row) : Values(cell.row))[col];
        }
        bool Editable(const GridCell& cell) const override { return !random_ && IndexOf(cell.row).has_value(); }
        GridStatus SetValue(const GridCell& cell, const GridValue& value) override
        {
            if (random_) return std::unexpected("Read-only random model");
            auto it = std::ranges::find(columns_, cell.column, &GridModelColumn::id);
            if (it == columns_.end() || !IndexOf(cell.row))
                return std::unexpected("Unknown cell");
            if (auto status = ValidateGridValue(*it, value); !status)
                return status;
            if (Value(cell) == value)
                return {};
            auto [row, inserted] = overrides_.try_emplace(cell.row, Values(cell.row));
            row->second[size_t(it - columns_.begin())] = value;
            OnChange.Raise({GridModelChange::Kind::Value, cell});
            return {};
        }
        mutable size_t reads = 0;

      private:

        bool random_ = false;
        std::vector<GridModelColumn> columns_ = Definitions();
        std::unordered_map<GridRowId, std::vector<GridValue>> overrides_;
    };
    class WideModel final : public GridModel
    {
      public:
        WideModel()
        {
            for (size_t i = 0; i < 1000; ++i) columns_.push_back({"c" + std::to_string(i + 1), GridValueType::Integer});
        }
        std::span<const GridModelColumn> Columns() const override { return columns_; }
        size_t RowCount() const override { return 1000; }
        GridRowId RowId(size_t index) const override { return index + 1; }
        std::optional<size_t> IndexOf(GridRowId id) const override { return id && id <= RowCount() ? std::optional<size_t>(id - 1) : std::nullopt; }
        GridValue Value(const GridCell& cell) const override
        {
            ++reads;
            const auto column = std::ranges::find(columns_, cell.column, &GridModelColumn::id);
            if (!IndexOf(cell.row) || column == columns_.end()) throw std::out_of_range("Unknown wide cell");
            const uint64_t seed = cell.row * 6364136223846793005ULL + uint64_t(column - columns_.begin()) * 1442695040888963407ULL;
            return int64_t((seed ^ (seed >> 32)) % 1000000);
        }
        bool Editable(const GridCell&) const override { return false; }
        GridStatus SetValue(const GridCell&, const GridValue&) override { return std::unexpected("Read-only wide model"); }
        mutable size_t reads = 0;
      private:
        std::vector<GridModelColumn> columns_;
    };
    class UppercaseEditor final : public GridCellEditor
    {
      public:

        explicit UppercaseEditor(const GridValue& value)
        {
            text_ = &Emplace<TextBox>(std::get<std::string>(value));
            connection_ = text_->OnEdit.Connect([this](const std::string&, EditPhase phase) { OnEdit.Raise(phase); });
        }
        std::expected<GridValue, std::string> Draft() const override
        {
            auto text = text_->Text();
            for (auto& c : text)
                if (c >= 'a' && c <= 'z')
                    c -= 'a' - 'A';
            return GridValue{std::move(text)};
        }
        void SetValidation(std::string error) override { text_->SetValidation(std::move(error)); }
        Control& FocusTarget() override { return *text_; }

      protected:

        Size OnMeasure(Size available) override
        {
            text_->Measure(available);
            return available;
        }
        void OnArrange() override { text_->Arrange(Bounds()); }

      private:

        TextBox* text_;
        Event<void(const std::string&, EditPhase)>::Connection connection_;
    };
    std::vector<GridColumn> Columns()
    {
        std::vector<GridColumn> columns{{"name", "Name"},      {"team", "Team"},         {"count", "Quantity"},
                                        {"cost", "Cost"},      {"progress", "Progress"}, {"tag", "Tag (uppercase)"},
                                        {"enabled", "Enabled"}};
        columns[0].width = 210;
        columns[0].compare = [](const GridValue& a, const GridValue& b)
        {
            auto x = FoldSearchText(std::get<std::string>(a)), y = FoldSearchText(std::get<std::string>(b));
            return x < y ? -1 : y < x ? 1 : 0;
        };
        columns[1].width = 180;
        columns[2].width = 110;
        columns[3].width = 120;
        columns[2].alignment = columns[3].alignment = Alignment::End;
        columns[4].format = [](const GridValue& v)
        { return std::to_string(std::lround(std::get<double>(v) * 100)) + "%"; };
        columns[4].dragValue = true;
        columns[4].clickValue = [](const GridCellPresentation& cell, const Input& input) -> std::optional<GridValue>
        {
            const double fraction = std::clamp(double(input.x - cell.bounds.x - ProgressPadding) /
                                                   std::max(1.f, cell.bounds.width - 2 * ProgressPadding),
                                               0., 1.);
            return GridValue{std::clamp(std::round(fraction / ProgressStep) * ProgressStep, 0., 1.)};
        };
        columns[4].render = [](Canvas& canvas, const GridCellPresentation& cell)
        {
            const float padding = ProgressPadding;
            canvas.Fill(cell.bounds.x + padding, cell.bounds.y + 4, cell.bounds.width - 2 * padding,
                        cell.bounds.height - 8, LLUtils::Color{uint32_t{0x1c3046ff}});
            canvas.Fill(cell.bounds.x + padding, cell.bounds.y + 8,
                        (cell.bounds.width - 2 * padding) * float(std::get<double>(cell.value)),
                        cell.bounds.height - 16, LLUtils::Color{uint32_t{0x386faaff}});
            canvas.Text(cell.text, cell.bounds.x + padding + 2, cell.bounds.y + 6, cell.bounds.width - 2 * padding,
                        cell.bounds.height - 8, LLUtils::Color{uint32_t{0xffffffff}}, FontSpec{14});
        };
        columns[5].editor = [](const GridModelColumn&, const GridValue& v)
        { return std::make_unique<UppercaseEditor>(v); };
        columns[6].width = 100;
        return columns;
    }
    void Snapshot(UIHost& host, const std::string& filename)
    {
        host.Update();
        const auto size = host.Window().GetClientAreaMetrics().logical;
        Canvas canvas;
        canvas.Begin(size.x, size.y);
        canvas.Fill(0, 0, float(size.x), float(size.y), host.Style().background);
        host.Root()->Render(canvas);
        const auto buffer = canvas.End();
        std::ofstream out(filename, std::ios::binary);
        out << "P6\n" << size.x << ' ' << size.y << "\n255\n";
        for (int y = 0; y < size.y; ++y)
            for (int x = 0; x < size.x; ++x)
            {
                const auto* pixel = buffer.pixels.data() + y * buffer.rowPitch + x * 4;
                const char rgb[]{char(pixel[2]), char(pixel[1]), char(pixel[0])};
                out.write(rgb, 3);
            }
        if (!out)
            throw std::runtime_error("Cannot write grid snapshot");
    }
}  // namespace
int main(int argc, char** argv)
{
    bool smoke = false, snapshot = false, narrow = false;
    try
    {
        for (int i = 1; i < argc; ++i)
        {
            const std::string arg = argv[i];
            if (arg == "--smoke")
                smoke = true;
            else if (arg == "--snapshot")
                snapshot = smoke = true;
            else if (arg == "--snapshot-narrow")
                snapshot = smoke = narrow = true;
            else
                throw std::invalid_argument("Usage: LWSUIGridDemo [--smoke] [--snapshot] [--snapshot-narrow]");
        }
        const auto check = [](LWS::Result result, const char* message)
        {
            if (result != LWS::Result::Success)
                throw std::runtime_error(message);
        };
#ifdef LWS_HAS_WIN32_BACKEND
        check(LWS::Win32::BootstrapProcess(), "Cannot initialize Windows process");
#endif
        LWS::PlatformContext platform;
        LWS::PlatformConfig platformConfig;
#ifdef LWS_HAS_WIN32_BACKEND
        platformConfig.backend = LWS::BackendId::Win32;
#else
        platformConfig.backend = LWS::BackendId::Wayland;
#endif
        check(platform.Init(platformConfig), "Cannot initialize platform");
        LWS::Window window(platform);
        LWS::WindowConfig windowConfig;
        windowConfig.title = NativeText("LWSUI GridView / Features and layouts");
        windowConfig.clientSize = {narrow ? 900 : 1180, narrow ? 600 : 740};
        windowConfig.clientSizeLimits.minimum = {900, 600};
        windowConfig.styles = LWS::WindowStyle::Caption | LWS::WindowStyle::CloseButton |
                              LWS::WindowStyle::ResizableBorder;
        windowConfig.backgroundColor = demo::grid::CompactTheme(LWSUI::DefaultThemePreset).background;
        windowConfig.eraseBackground = false;
        check(window.Create(windowConfig), "Cannot create demo window");
        UIHost host(window, demo::grid::CompactTheme(LWSUI::DefaultThemePreset));
        host.SetRedrawOnResize(true);
        auto model = std::make_shared<MemoryGridModel>(Definitions());
        for (GridRowId row = 1; row <= 160; ++row)
            model->AddRow(Values(row));
        model->validate = [](const GridCell& cell, const GridValue& value) -> GridStatus
        {
            if (cell.column == "name" && std::get<std::string>(value).empty())
                return std::unexpected("Name cannot be empty");
            return {};
        };
        auto large = std::make_shared<LargeModel>();
        auto random = std::make_shared<LargeModel>(true);
        auto wide = std::make_shared<WideModel>();
        const auto presets = demo::grid::Presets();
        std::vector<Choice> presetChoices;
        for (const auto& item : presets)
            presetChoices.push_back(item.choice);
        auto root = std::make_unique<DockPanel>();
        root->gap = 8;
        auto top = std::make_unique<StackPanel>();
        top->spacing = 6;
        auto& title = top->Emplace<Label>("GridView: layouts, editing, selection and sticky columns");
        title.fontRole = Control::FontRole::Heading;
        auto toolbar = std::make_unique<FlowPanel>();
        toolbar->gap = 8;
        auto presetControl = std::make_unique<ComboBox>(presetChoices);
        auto* preset = presetControl.get();
        preset->focusLossPhase = EditPhase::Cancel;
        toolbar->AddItem(std::move(presetControl), false, 260);
        auto themeControl = std::make_unique<ComboBox>(
            std::vector<Choice>{{"Dark theme", "dark"}, {"Light theme", "light"}, {"Warm theme", "warm"}});
        auto* theme = themeControl.get();
        theme->SetValue(ThemePresetName(LWSUI::DefaultThemePreset));
        toolbar->AddItem(std::move(themeControl), false, 160);
        auto resetControl = std::make_unique<Button>("Reset layout");
        auto* reset = resetControl.get();
        reset->focusLossPhase = EditPhase::Cancel;
        toolbar->AddItem(std::move(resetControl), false, 130);
        toolbar->AddItem(std::make_unique<Label>("Scroll speed"), false, 90);
        auto speedControl = std::make_unique<SpeedProgress>();
        auto& speedProgress = *speedControl;
        toolbar->AddItem(std::move(speedControl), false, 100);
        auto speedText = std::make_unique<demo::SpeedReadout>();
        auto& speedValue = *speedText;
        toolbar->AddItem(std::move(speedText), false, 70);
        top->Add(std::move(toolbar));
        auto& description = top->Emplace<Label>();
        description.wrap = true;
        auto& instructions = top->Emplace<Label>(
            "Drag headers to move columns. Drag boundaries to resize. Double-click/F2 edits. Ctrl/Shift selects rows. "
            "Drag progress bars to adjust them. Shift+wheel scrolls horizontally. "
            "With dynamic resolution enabled, drag the scrollbar inward to reduce speed.");
        instructions.wrap = true;
        instructions.fontRole = Control::FontRole::Small;
        root->AddDocked(std::move(top), Dock::Top);
        auto logControl = std::make_unique<TextBox>("Events appear here", TextBoxMode::Multiline);
        auto* log = logControl.get();
        log->SetReadOnly(true);
        log->SetVisibleLines(3);
        root->AddDocked(std::move(logControl), Dock::Bottom, 72);
        auto featureStack = std::make_unique<StackPanel>();
        featureStack->spacing = 2;
        featureStack->Emplace<Label>("Live configuration").fontRole = Control::FontRole::Heading;
        auto featureChecks = std::make_unique<Grid>();
        featureChecks->columns = {-1, -1};
        featureChecks->spacing = 4;
        struct Toggle
        {
            const char* label;
            bool GridOptions::* member;
            CheckBox* control = nullptr;
        };
        std::vector<Toggle> toggles{{"Selection enabled", &GridOptions::selection},
                                    {"Full row select", &GridOptions::fullRowSelect},
                                    {"Multiple selection", &GridOptions::multipleSelection},
                                    {"Editing enabled", &GridOptions::editing},
                                    {"Read-only", &GridOptions::readOnly},
                                    {"Sorting", &GridOptions::sorting},
                                    {"Column reordering", &GridOptions::reordering},
                                    {"Column resizing", &GridOptions::resizing},
                                    {"Header visible", &GridOptions::headerVisible},
                                    {"Sticky headers", &GridOptions::stickyHeaders},
                                    {"Sort glyphs", &GridOptions::sortGlyphs},
                                    {"Row striping", &GridOptions::striped},
                                    {"Horizontal grid lines", &GridOptions::horizontalLines},
                                    {"Vertical grid lines", &GridOptions::verticalLines},
                                    {"Copy enabled", &GridOptions::copy},
                                    {"Copy headers", &GridOptions::copyHeaders},
                                    {"Single-click edits", &GridOptions::singleClickEdit},
                                    {"Double-click edits", &GridOptions::doubleClickEdit},
                                    {"F2 edits", &GridOptions::f2Edit},
                                    {"Typing edits", &GridOptions::typingEdit},
                                    {"Drag value edits", &GridOptions::dragEditing},
                                    {"Ctrl+A selects all", &GridOptions::selectAll},
                                    {"Tab navigates cells", &GridOptions::tabNavigation},
                                    {"Context menus", &GridOptions::contextMenus},
                                    {"Dynamic resolution", &GridOptions::dynamicResolution},
                                    {"Line numbers", &GridOptions::lineNumbers},
                                    {"Show guide", &GridOptions::resolutionGuide}};
        for (auto& toggle : toggles)
        {
            toggle.control = &featureChecks->Emplace<CheckBox>(toggle.label);
            if (toggle.member == &GridOptions::readOnly || toggle.member == &GridOptions::editing ||
                toggle.member == &GridOptions::singleClickEdit)
                toggle.control->focusLossPhase = EditPhase::Cancel;
        }
        auto& leftPin = featureChecks->Emplace<CheckBox>("Pin name left");
        auto& rightPin = featureChecks->Emplace<CheckBox>("Pin enabled right");
        auto& costVisible = featureChecks->Emplace<CheckBox>("Show cost column");
        featureStack->Add(std::move(featureChecks));
        auto fields = std::make_unique<Grid>();
        fields->columns = {120, -1};
        fields->spacing = 4;
        fields->Emplace<Label>("Row height");
        auto& rowHeight = fields->Emplace<NumericEdit<int64_t>>(NumericSpec<int64_t>{24, 80, 2});
        fields->Emplace<Label>("Header style");
        auto& headerStyle = fields->Emplace<ComboBox>(
            std::vector<Choice>{{"Theme default", "default"}, {"Blue header", "blue"}, {"Amber header", "amber"}});
        fields->Emplace<Label>("Fine resolution");
        auto& resolution = fields->Emplace<ComboBox>(std::vector<Choice>{{"One row", "row"}, {"One pixel", "pixel"}});
        fields->Emplace<Label>("Line label");
        auto& lineLabel = fields->Emplace<TextBox>("Line");
        fields->Emplace<Label>("Inward distance %");
        auto& distance = fields->Emplace<NumericEdit<int64_t>>(NumericSpec<int64_t>{1, 100, 1});
        featureStack->Add(std::move(fields));
        auto& metrics = featureStack->Emplace<Label>();
        metrics.wrap = true;
        metrics.fontRole = Control::FontRole::Small;
        auto features = std::make_unique<ScrollView>();
        features->SetContent(std::move(featureStack));
        auto* configuration = features.get();
        configuration->SetStyle(demo::grid::ConfigurationTheme(host.Style()));
        root->AddDocked(std::move(features), Dock::Right, 400);
        auto gridControl = std::make_unique<GridView>();
        auto* grid = gridControl.get();
        root->AddDocked(std::move(gridControl), Dock::Fill);
        auto panel = std::make_unique<Panel>(std::move(root));
        panel->padding = Insets{8};
        host.SetRoot(std::move(panel));
        std::deque<std::string> history;
        const auto record = [&](std::string message)
        {
            history.push_front(std::move(message));
            while (history.size() > 50)
                history.pop_back();
            std::string text;
            for (const auto& line : history)
            {
                if (!text.empty())
                    text += '\n';
                text += line;
            }
            log->SetText(std::move(text));
        };
        const auto sync = [&]
        {
            const auto& options = grid->Options();
            for (const auto& toggle : toggles)
            {
                toggle.control->SetValue(options.*toggle.member);
                bool enabled = true;
                if (toggle.member == &GridOptions::fullRowSelect || toggle.member == &GridOptions::multipleSelection)
                    enabled = options.selection;
                if (toggle.member == &GridOptions::selectAll)
                    enabled = options.selection && options.multipleSelection;
                if (toggle.member == &GridOptions::copyHeaders)
                    enabled = options.copy;
                if (toggle.member == &GridOptions::stickyHeaders)
                    enabled = options.headerVisible;
                if (toggle.member == &GridOptions::sortGlyphs)
                    enabled = options.sorting && options.headerVisible;
                if (toggle.member == &GridOptions::singleClickEdit || toggle.member == &GridOptions::doubleClickEdit ||
                    toggle.member == &GridOptions::f2Edit || toggle.member == &GridOptions::typingEdit ||
                    toggle.member == &GridOptions::dragEditing)
                    enabled = options.editing && !options.readOnly;
                if ((grid->Model() == random || grid->Model() == wide) && (toggle.member == &GridOptions::sorting ||
                    toggle.member == &GridOptions::selectAll || toggle.member == &GridOptions::resizing ||
                    toggle.member == &GridOptions::editing || toggle.member == &GridOptions::multipleSelection || toggle.member == &GridOptions::readOnly)) enabled = false;
                if (toggle.member == &GridOptions::resolutionGuide) enabled = options.dynamicResolution;
                toggle.control->SetEnabled(enabled);
            }
            const auto column = [&](const std::string& id) -> const GridColumn*
            {
                auto it = std::ranges::find(grid->Columns(), id, &GridColumn::id);
                return it == grid->Columns().end() ? nullptr : &*it;
            };
            leftPin.SetEnabled(column("name") != nullptr);
            leftPin.SetValue(column("name") && column("name")->pin == GridPin::Left);
            rightPin.SetValue(column("enabled") && column("enabled")->pin == GridPin::Right);
            rightPin.SetEnabled(column("enabled") != nullptr);
            costVisible.SetValue(column("cost") && column("cost")->visible);
            costVisible.SetEnabled(column("cost") != nullptr);
            lineLabel.SetText(options.lineNumberLabel);
            lineLabel.SetEnabled(options.lineNumbers);
            distance.SetValue(int64_t(std::lround(options.inwardDistance * 100)));
            distance.SetEnabled(options.dynamicResolution);
            resolution.SetValue(options.centerResolution == ScrollResolution::Row ? "row" : "pixel");
            resolution.SetEnabled(options.dynamicResolution);
            rowHeight.SetValue(int64_t(options.rowHeight));
            headerStyle.SetEnabled(options.headerVisible);
            metrics.SetText(std::to_string(grid->Model()->RowCount()) + " rows. Visible: " +
                            std::to_string(grid->VisibleRowCount()) + ". Virtual scrolling active.");
        };
        std::function<void(const std::string&)> applyPreset = [&](const std::string& name)
        {
            const auto item = std::ranges::find_if(presets,
                                                   [&](const auto& value) { return value.choice.value == name; });
            if (item == presets.end())
                throw std::invalid_argument("Unknown grid preset");
            grid->FinishEdit(EditPhase::Cancel);
            grid->SetModel(item->wideModel ? std::static_pointer_cast<GridModel>(wide) : item->randomModel ? std::static_pointer_cast<GridModel>(random) : item->externalModel ? std::static_pointer_cast<GridModel>(large)
                                               : std::static_pointer_cast<GridModel>(model));
            auto columns = Columns();
            if (item->wideModel)
            {
                columns.clear();
                for (const auto& definition : wide->Columns())
                {
                    GridColumn column{definition.id, "Column " + definition.id.substr(1)};
                    column.width = 160;
                    column.editable = column.sortable = column.resizable = false;
                    columns.push_back(std::move(column));
                }
            }
            if (item->pinnedColumns)
            {
                columns.front().pin = GridPin::Left;
                columns.back().pin = GridPin::Right;
            }
            if (item->list)
            {
                columns.resize(1);
                columns.front().widthMode = GridWidthMode::Proportional;
            }
            grid->SetColumns(std::move(columns));
            auto options = item->options;
            options.resolutionGuide = true;
            grid->SetOptions(options);
            GridAppearance appearance;
            appearance.headerPadding = 6;
            appearance.cellPadding = 4;
            if (item->headerStyle == "blue")
            {
                appearance.headerBackground = LLUtils::Color{uint32_t{0x16477aff}};
                appearance.headerForeground = LLUtils::Color{uint32_t{0xffffffff}};
                appearance.headerAlignment = Alignment::Center;
            }
            grid->SetAppearance(appearance);
            headerStyle.SetValue(item->headerStyle);
            description.SetText(item->description);
            preset->SetValue(name);
            host.Update();
            sync();
            record("Layout: " + name);
        };
        auto resolutionConnection = resolution.OnChange.Connect([&](const std::string& mode)
        {
            auto options = grid->Options();
            options.centerResolution = mode == "pixel" ? ScrollResolution::Pixel : ScrollResolution::Row;
            grid->SetOptions(options);
        });
        auto labelConnection = lineLabel.OnEdit.Connect([&](const std::string& label, EditPhase phase)
        {
            if (phase == EditPhase::Preview) return;
            auto options = grid->Options();
            options.lineNumberLabel = label;
            grid->SetOptions(options);
        });
        auto distanceConnection = distance.OnEdit.Connect([&](int64_t percent, EditPhase)
        {
            auto options = grid->Options();
            options.inwardDistance = double(percent) / 100;
            grid->SetOptions(options);
        });
        auto speedConnection = grid->OnScrollSpeed.Connect([&](double speed)
        {
            speedProgress.SetSpeed(speed);
            speedValue.SetSpeed(speed);
        });
        std::vector<Event<void(bool)>::Connection> checks;
        for (const auto& toggle : toggles)
            checks.push_back(toggle.control->OnChange.Connect(
                [&, member = toggle.member](bool value)
                {
                    auto options = grid->Options();
                    options.*member = value;
                    grid->SetOptions(options);
                    sync();
                    record("Configuration changed");
                }));
        checks.push_back(leftPin.OnChange.Connect(
            [&](bool value)
            {
                grid->PinColumn("name", value ? GridPin::Left : GridPin::None);
                sync();
            }));
        checks.push_back(rightPin.OnChange.Connect(
            [&](bool value)
            {
                grid->PinColumn("enabled", value ? GridPin::Right : GridPin::None);
                sync();
            }));
        checks.push_back(costVisible.OnChange.Connect(
            [&](bool value)
            {
                grid->SetColumnVisible("cost", value);
                sync();
            }));
        auto heightConnection = rowHeight.OnEdit.Connect(
            [&](int64_t value, EditPhase phase)
            {
                if (phase == EditPhase::Commit)
                {
                    auto o = grid->Options();
                    o.rowHeight = float(value);
                    grid->SetOptions(o);
                }
            });
        auto headerConnection = headerStyle.OnChange.Connect(
            [&](const std::string& name)
            {
                auto a = grid->Appearance();
                a.headerBackground.reset();
                a.headerForeground.reset();
                if (name != "default")
                {
                    a.headerBackground = LLUtils::Color{uint32_t{name == "blue" ? 0x16477affu : 0x76500effu}};
                    a.headerForeground = LLUtils::Color{uint32_t{0xffffffff}};
                }
                grid->SetAppearance(a);
            });
        auto presetConnection = preset->OnChange.Connect(
            [&](const std::string& name) { host.Post(grid->Handle(), [&, name](Control&) { applyPreset(name); }); });
        auto resetConnection = reset->OnClick.Connect(
            [&]
            {
                const auto name = preset->Value();
                host.Post(grid->Handle(), [&, name](Control&) { applyPreset(name); });
            });
        const auto applyTheme = [&](const std::string& name)
        {
            const auto style = demo::grid::CompactTheme(name == "light"  ? ThemePreset::Light
                                                        : name == "warm" ? ThemePreset::Warm
                                                                         : ThemePreset::Dark);
            host.SetTheme(style);
            configuration->SetStyle(demo::grid::ConfigurationTheme(style));
            theme->SetValue(name);
        };
        auto themeConnection = theme->OnChange.Connect(applyTheme);
        auto click = grid->OnCellClick.Connect(
            [&](const GridCellEvent& e)
            { record("Cell click: " + std::to_string(e.cell.row) + " / " + e.cell.column); });
        auto doubleClick = grid->OnCellDoubleClick.Connect([&](const GridCellEvent& e)
                                                           { record("Double click: " + e.cell.column); });
        auto value = grid->OnCellValueChanged.Connect(
            [&](const GridValueChange& e)
            { record("Value changed: " + e.cell.column + " = " + FormatGridValue(e.newValue)); });
        auto edit = grid->OnEdit.Connect(
            [&](const GridEditEvent& e)
            {
                record(std::string(e.phase == EditPhase::Commit   ? "Commit: "
                                   : e.phase == EditPhase::Cancel ? "Cancel: "
                                                                  : "Preview: ") +
                       e.cell.column);
            });
        auto selection = grid->OnSelectionChanged.Connect(
            [&](const GridSelectionChange& e)
            { record("Selection: +" + std::to_string(e.added.size()) + " / -" + std::to_string(e.removed.size())); });
        auto validation = grid->OnValidationFailed.Connect([&](const GridValidationEvent& e)
                                                           { record("Validation: " + e.message); });
        auto sorted = grid->OnSorted.Connect([&](const GridColumnEvent& e) { record("Sort: " + e.column); });
        auto moved = grid->OnColumnReordered.Connect([&](const GridColumnEvent& e)
                                                     { record("Column moved: " + e.column); });
        auto resized = grid->OnColumnResized.Connect(
            [&](const GridColumnEvent& e)
            {
                if (e.phase != EditPhase::Preview)
                    record("Column resized: " + e.column);
            });
        auto pinned = grid->OnColumnPinned.Connect(
            [&](const GridColumnEvent& e)
            {
                record("Pin changed: " + e.column);
                sync();
            });
        auto scrolled = grid->OnScroll.Connect([&](Size) { sync(); });
        bool failed = false;
        auto error = host.OnError.Connect(
            [&](const std::string& message)
            {
                ReportError(message, !smoke);
                failed = true;
                platform.RequestQuit();
            });
        auto closing = window.Listen(
            [&](const LWS::AnyEvent& event)
            {
                if (std::holds_alternative<LWS::EventCloseRequested>(event))
                {
                    grid->FinishEdit(EditPhase::Cancel);
                    platform.RequestQuit();
                    return LWS::EventResponse::Handled;
                }
                return LWS::EventResponse::Unhandled;
            });
        if (!closing)
            throw std::runtime_error("Cannot register window listener");
        check(window.SetVisible(true), "Cannot show grid demo");
        applyPreset("comfortable");
        host.Update();
        std::unique_ptr<LWS::Timer> timer;
        if (smoke)
        {
            timer = std::make_unique<LWS::Timer>(platform);
            check(timer->SetTargetWindow(&window), "Cannot attach demo timer");
            timer->SetCallback(
                [&]
                {
                    timer->SetInterval(0);
                    try
                    {
                        bool pointerDown = false;
                        const auto pointer = [&](Input input)
                        {
#ifdef LWS_HAS_WIN32_BACKEND
                            const auto hwnd = LWS::Win32::GetHwnd(window);
                            if (!hwnd)
                                throw std::runtime_error("Cannot obtain native smoke window");
                            const auto scale = host.DrawingScale().value_or(LWS::ContentScale{1, 1});
                            if (input.kind == InputKind::Down)
                                pointerDown = true;
                            if (input.kind == InputKind::Up)
                                pointerDown = false;
                            const auto position = MAKELPARAM(int(std::lround(input.x * scale.x)),
                                                             int(std::lround(input.y * scale.y)));
                            const UINT message = input.kind == InputKind::Down ? WM_LBUTTONDOWN
                                                 : input.kind == InputKind::Up ? WM_LBUTTONUP
                                                                               : WM_MOUSEMOVE;
                            SendMessageW(*hwnd, message, pointerDown ? MK_LBUTTON : 0, position);
#else
                            (void) pointerDown;
                            host.Route(input);
#endif
                        };
                        for (const auto& item : presets)
                        {
                            applyPreset(item.choice.value);
                            if (item.choice.value == "editors")
                            {
                                auto off = grid->Options();
                                off.singleClickEdit = false;
                                grid->SetOptions(off);
                                sync();
                                host.Update();
                                auto mode = std::ranges::find_if(
                                    toggles,
                                    [](const auto& toggle) { return toggle.member == &GridOptions::singleClickEdit; });
                                const auto box = mode->control->Bounds();
                                pointer({.kind = InputKind::Down, .x = box.x + 8, .y = box.y + box.height / 2});
                                pointer({.kind = InputKind::Up, .x = box.x + 8, .y = box.y + box.height / 2});
                                if (!grid->Options().singleClickEdit)
                                    throw std::runtime_error("Runtime single-click toggle did not apply");
                                for (const std::string column : {"enabled", "progress"})
                                {
                                    const GridCell cell{1, column};
                                    const auto original = grid->Model()->Value(cell);
                                    if (column == "progress")
                                    {
                                        grid->SetScrollOffset(0, 0);
                                        host.Update();
                                    }
                                    if (column == "enabled" || !grid->CellBounds(cell))
                                        if (!grid->SelectCell(cell))
                                            throw std::runtime_error("Cannot select click-action sample");
                                    host.Update();
                                    const auto bounds = grid->CellBounds(cell);
                                    if (!bounds)
                                        throw std::runtime_error("Click-action cell is not visible");
                                    const float x = bounds->x + bounds->width / 2, y = bounds->y + bounds->height / 2;
                                    pointer({.kind = InputKind::Down, .x = x, .y = y});
                                    if (column == "progress")
                                    {
                                        pointer({.kind = InputKind::Move, .x = x - 25, .y = y + 2 * bounds->height});
                                        pointer({.kind = InputKind::Move, .x = x + 7, .y = y});
                                        if (grid->Model()->Value(cell) != original || !grid->Editing())
                                            throw std::runtime_error(
                                                "Progress drag did not retain its preview transaction");
                                    }
                                    pointer({.kind = InputKind::Up, .x = column == "progress" ? x + 7 : x, .y = y});
                                    const GridValue expected = column == "enabled"
                                                                   ? GridValue{!std::get<bool>(original)}
                                                                   : GridValue{.55};
                                    if (grid->Model()->Value(cell) != expected || grid->Editing())
                                        throw std::runtime_error("Single-click action did not commit immediately");
                                    if (!grid->SetCellValue(cell, original))
                                        throw std::runtime_error("Cannot restore click-action sample");
                                }
                                grid->SetScrollOffset(0, 0);
                                grid->ClearSelection();
                                host.Update();
                            }
                            if (item.wideModel)
                            {
                                const auto reads = wide->reads;
                                grid->SetPreciseScrollOffset(160000, 0);
                                host.Update();
                                if (!grid->CellBounds({1, "c1000"}) || wide->reads - reads > 2000)
                                    throw std::runtime_error("Wide grid did not reach the final column with bounded reads");
                                auto* horizontal = static_cast<ScrollBar*>(grid->Children()[1].get());
                                const auto bar = horizontal->Bounds(), area = grid->Bounds();
                                const float x = bar.x + bar.width - 5, y = bar.y + bar.height / 2;
                                pointer({.kind = InputKind::Down, .x = x, .y = y});
                                pointer({.kind = InputKind::Move, .x = x, .y = area.y});
                                if (!horizontal->ResolutionDragging() || horizontal->ScrollSpeed() >= 100)
                                    throw std::runtime_error("Wide horizontal resolution did not engage");
                                if (snapshot)
                                    for (const std::string palette : {"dark", "light", "warm"})
                                    {
                                        applyTheme(palette);
                                        Snapshot(host, "grid-wide-horizontal-" + palette + (narrow ? "-narrow.ppm" : ".ppm"));
                                    }
                                pointer({.kind = InputKind::Up, .x = x, .y = area.y});
                            }
                            if (item.randomModel)
                            {
                                if (random->RowCount() != 100000000 || random->Value({99999990, "count"}) != random->Value({99999990, "count"}))
                                    throw std::runtime_error("Random virtual model is not stable");
                                grid->SetPreciseScrollOffset(0, 99999980. * grid->Options().rowHeight);
                                host.Update();
                                if (grid->FirstVisibleRow() != 99999980 || random->reads > 10000)
                                    throw std::runtime_error("Random virtual grid lost precision or read too many cells");
                                auto* bar = static_cast<ScrollBar*>(grid->Children().front().get());
                                const auto b = bar->Bounds(), area = grid->Bounds();
                                const float x = b.x + b.width / 2, y = b.y + b.height - 5;
                                pointer({.kind = InputKind::Down, .x = x, .y = y});
                                pointer({.kind = InputKind::Move, .x = area.x + area.width / 2, .y = y});
                                if (snapshot)
                                    for (const std::string palette : {"dark", "light", "warm"})
                                    {
                                        applyTheme(palette);
                                        Snapshot(host, "grid-resolution-vertical-" + palette + (narrow ? "-narrow.ppm" : ".ppm"));
                                    }
                                pointer({.kind = InputKind::Up, .x = area.x + area.width / 2, .y = y});
                                auto fineOptions = grid->Options();
                                fineOptions.centerResolution = ScrollResolution::Pixel;
                                grid->SetOptions(fineOptions);
                                sync();
                                host.Update();
                                auto* horizontal = static_cast<ScrollBar*>(grid->Children()[1].get());
                                const auto h = horizontal->Bounds();
                                const float hx = h.x + 8, hy = h.y + h.height / 2;
                                pointer({.kind = InputKind::Down, .x = hx, .y = hy});
                                pointer({.kind = InputKind::Move, .x = hx, .y = area.y + area.height / 2});
                                if (snapshot)
                                    for (const std::string palette : {"dark", "light", "warm"})
                                    {
                                        applyTheme(palette);
                                        Snapshot(host, "grid-resolution-horizontal-" + palette + (narrow ? "-narrow.ppm" : ".ppm"));
                                    }
                                pointer({.kind = InputKind::Up, .x = hx, .y = area.y + area.height / 2});
                            }
                            if (snapshot)
                                Snapshot(host, "grid-" + item.choice.value + (narrow ? "-narrow.ppm" : ".ppm"));
                        }
                        if (snapshot)
                        {
                            applyPreset("comfortable");
                            auto scrollingOptions = grid->Options();
                            scrollingOptions.stickyHeaders = false;
                            grid->SetOptions(scrollingOptions);
                            host.Update();
                            grid->SetScrollOffset(0, scrollingOptions.headerHeight + 4 * scrollingOptions.rowHeight);
                            sync();
                            Snapshot(host, narrow ? "grid-scrolling-headers-narrow.ppm" : "grid-scrolling-headers.ppm");
                            applyPreset("comfortable");
                            for (const std::string palette : {"dark", "light", "warm"})
                            {
                                applyTheme(palette);
                                Snapshot(host, "grid-theme-" + palette + (narrow ? "-narrow.ppm" : ".ppm"));
                            }
                        }
                        std::cout << "Grid demo smoke: 11 presets, 1000000 external rows, 100000000 random rows, " << large->reads
                                  << " external cell reads\n";
                    }
                    catch (const std::exception& e)
                    {
                        ReportError(e.what(), false);
                        failed = true;
                    }
                    platform.RequestQuit();
                });
            timer->SetInterval(500);
        }
        const auto result = platform.RunMessageLoop();
        return failed || result == LWS::LoopResult::Failed ? 1 : 0;
    }
    catch (const std::exception& e)
    {
        ReportError(e.what(), !smoke);
        return 1;
    }
}
