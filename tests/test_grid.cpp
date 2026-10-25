#ifdef LWS_TEST_UI
    #include <catch2/catch_test_macros.hpp>
    #include <catch2/catch_approx.hpp>
    #include <LWSUI/GridView.hpp>
    #include <LWSUI/Composites.hpp>
    #include <LWSUI/UIHost.hpp>
    #include "demo/GridDemoPresets.hpp"
    #include "demo/ScrollFeedback.hpp"
    #include <LWS/Platform.hpp>
    #include <LWS/TextClipboard.hpp>
    #ifdef LWS_HAS_WIN32_BACKEND
        #include <LWS/Win32/Platform.hpp>
    #endif
    #include <limits>
    #include <cmath>
    #include <thread>
namespace LWSUI::internal
{
    struct UIHostTestAccess
    {
        static bool Event(UIHost& host, const LWS::AnyEvent& event) { return host.Event(event); }
        static Control* Captured(UIHost& host) { return host.capture_.Get(); }
    };
}
using namespace LWSUI;
namespace
{
    std::shared_ptr<MemoryGridModel> Model()
    {
        GridModelColumn name{"name"}, number{"number", GridValueType::Integer},
            enabled{"enabled", GridValueType::Boolean};
        number.integer = {-100, 100, 1};
        auto model = std::make_shared<MemoryGridModel>(std::vector<GridModelColumn>{name, number, enabled});
        model->AddRow({std::string("Zulu"), int64_t(12), true});
        model->AddRow({std::string("Alpha"), int64_t(2), false});
        model->AddRow({std::string("Alpha"), int64_t(2), true});
        model->AddRow({std::string("Delta"), int64_t(35), false});
        return model;
    }
    void Setup(GridView& grid, const std::shared_ptr<GridModel>& model)
    {
        grid.SetModel(model);
        auto columns = grid.Columns();
        for (auto& c : columns)
            c.width = 120;
        grid.SetColumns(columns);
        grid.Measure({600, 400});
        grid.Arrange({0, 0, 600, 400});
    }
    struct Environment
    {
        LWS::PlatformContext context;
        std::unique_ptr<LWS::Window> window;
        std::unique_ptr<UIHost> host;
        Environment()
        {
    #ifdef LWS_HAS_WIN32_BACKEND
            REQUIRE(LWS::Win32::BootstrapProcess() == LWS::Result::Success);
            REQUIRE(context.Init({.backend = LWS::BackendId::Win32}) == LWS::Result::Success);
    #else
            REQUIRE(context.Init({.backend = LWS::BackendId::Wayland}) == LWS::Result::Success);
    #endif
            window = std::make_unique<LWS::Window>(context);
            REQUIRE(window->Create({.clientSize = {640, 480}, .visible = true}) == LWS::Result::Success);
            host = std::make_unique<UIHost>(*window);
    #ifndef LWS_HAS_WIN32_BACKEND
            for (int i = 0; i < 500 && !window->IsConfigured(); ++i)
            {
                REQUIRE(context.ProcessMessages() == LWS::LoopResult::Continue);
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            REQUIRE(window->IsConfigured());
    #endif
        }
    };
    class CountingModel final : public GridModel
    {
      public:

        std::vector<GridModelColumn> columns{{"a", GridValueType::Integer},
                                             {"b", GridValueType::Integer},
                                             {"c", GridValueType::Integer}};
        mutable size_t reads = 0, rowReads = 0;
        size_t rows = 1000000;
        std::span<const GridModelColumn> Columns() const override { return columns; }
        size_t RowCount() const override { return rows; }
        GridRowId RowId(size_t i) const override
        {
            ++rowReads;
            return i + 1;
        }
        std::optional<size_t> IndexOf(GridRowId id) const override
        {
            return id && id <= RowCount() ? std::optional<size_t>(id - 1) : std::nullopt;
        }
        GridValue Value(const GridCell& c) const override
        {
            ++reads;
            return int64_t(c.row);
        }
        bool Editable(const GridCell&) const override { return false; }
        GridStatus SetValue(const GridCell&, const GridValue&) override { return std::unexpected("Read-only model"); }
    };
}  // namespace
TEST_CASE("Grid model validates values and emits only completed changes", "[ui][grid][model]")
{
    auto model = Model();
    int changes = 0;
    auto listener = model->OnChange.Connect(
        [&](const GridModelChange& c)
        {
            ++changes;
            REQUIRE(c.kind == GridModelChange::Kind::Value);
            REQUIRE(model->Value(c.cell) == GridValue{int64_t(8)});
        });
    REQUIRE_FALSE(model->SetValue({1, "number"}, std::string("8")));
    REQUIRE_FALSE(model->SetValue({1, "number"}, int64_t(101)));
    REQUIRE_FALSE(model->SetValue({999, "number"}, int64_t(8)));
    REQUIRE(model->SetValue({1, "number"}, int64_t(8)));
    REQUIRE(model->SetValue({1, "number"}, int64_t(8)));
    REQUIRE(changes == 1);
    REQUIRE_THROWS_AS(model->AddRow({std::string("bad")}), std::invalid_argument);
    REQUIRE(FormatGridValue(GridValue{std::numeric_limits<int64_t>::max()}) == "9223372036854775807");
    GridModelColumn decimal{"d", GridValueType::Decimal};
    REQUIRE_FALSE(ValidateGridValue(decimal, std::numeric_limits<double>::infinity()));
    GridModelColumn choice{"choice", GridValueType::Choice};
    choice.choices = {{"First", "first"}};
    REQUIRE(ValidateGridValue(choice, std::string("first")));
    REQUIRE_FALSE(ValidateGridValue(choice, std::string("other")));
}
TEST_CASE("Grid sorting is typed stable and independent of model order", "[ui][grid][sort]")
{
    auto model = Model();
    GridView grid;
    Setup(grid, model);
    REQUIRE(grid.SelectCell({1, "name"}));
    REQUIRE(grid.Sort("number", GridSortDirection::Ascending));
    REQUIRE(grid.DisplayRowId(0) == 2);
    REQUIRE(grid.DisplayRowId(1) == 3);
    REQUIRE(grid.DisplayRowId(2) == 1);
    REQUIRE(grid.IsSelected({1, "enabled"}));
    REQUIRE(grid.CurrentCell() == GridCell{1, "name"});
    REQUIRE(model->RowId(0) == 1);
    REQUIRE(model->SetValue({1, "number"}, int64_t(-5)));
    REQUIRE(grid.DisplayRowId(0) == 1);
    REQUIRE(grid.Sort({}, GridSortDirection::None));
    REQUIRE(grid.DisplayRowId(0) == 1);
    auto reject = grid.OnSorting.Connect([](GridColumnEvent& e) { e.cancel = true; });
    REQUIRE_FALSE(grid.Sort("name", GridSortDirection::Ascending));
    REQUIRE(grid.SortDirection() == GridSortDirection::None);
}
TEST_CASE("Grid row selection handles ranges toggles sorting and removal", "[ui][grid][selection]")
{
    auto model = Model();
    GridView grid;
    Setup(grid, model);
    auto o = grid.Options();
    o.multipleSelection = true;
    grid.SetOptions(o);
    REQUIRE(grid.SelectCell({1, "name"}));
    REQUIRE(grid.SelectCell({3, "number"}, true));
    REQUIRE(grid.Selection().size() == 3);
    REQUIRE(grid.IsSelected({2, "enabled"}));
    REQUIRE(grid.SelectCell({2, "enabled"}, false, true));
    REQUIRE(grid.Selection().size() == 2);
    REQUIRE(grid.Sort("name", GridSortDirection::Ascending));
    REQUIRE(grid.IsSelected({1, "number"}));
    REQUIRE(model->RemoveRow(1));
    REQUIRE(grid.Selection().size() == 1);
    REQUIRE(grid.IsSelected({3, "name"}));
    o.multipleSelection = false;
    grid.SetOptions(o);
    REQUIRE(grid.Selection().size() == 1);
    o.selection = false;
    grid.SetOptions(o);
    REQUIRE(grid.Selection().empty());
    REQUIRE(grid.SelectCell({4, "name"}));
    REQUIRE(grid.Selection().empty());
}
TEST_CASE("Grid selection mode changes emit one coherent notification", "[ui][grid][selection]")
{
    GridView grid;
    Setup(grid, Model());
    REQUIRE(grid.SelectCell({1, "name"}));
    int events = 0;
    auto connection = grid.OnSelectionChanged.Connect([&](const GridSelectionChange&) { ++events; });
    auto o = grid.Options();
    o.fullRowSelect = false;
    grid.SetOptions(o);
    REQUIRE(events == 1);
    REQUIRE(grid.IsSelected({1, "name"}));
    REQUIRE_FALSE(grid.IsSelected({1, "number"}));
}
TEST_CASE("Grid shared layout preserves both pinned edges and hit testing", "[ui][grid][layout]")
{
    GridView grid;
    Setup(grid, Model());
    auto columns = grid.Columns();
    columns[0].pin = GridPin::Left;
    columns[0].width = 100;
    columns[1].width = 600;
    columns[2].pin = GridPin::Right;
    columns[2].width = 80;
    grid.SetColumns(columns);
    grid.Arrange({10, 20, 400, 220});
    auto left = grid.CellBounds({1, "name"}), right = grid.CellBounds({1, "enabled"});
    REQUIRE(left);
    REQUIRE(right);
    grid.SetScrollOffset(200, 0);
    REQUIRE(grid.CellBounds({1, "name"})->x == left->x);
    REQUIRE(grid.CellBounds({1, "enabled"})->x == right->x);
    REQUIRE(grid.CellAt(left->x + 5, left->y + 5) == GridCell{1, "name"});
    REQUIRE(grid.CellAt(right->x + 5, right->y + 5) == GridCell{1, "enabled"});
    REQUIRE_FALSE(grid.MoveColumn("name", 1));
    REQUIRE(grid.PinColumn("number", GridPin::Left));
    REQUIRE(grid.MoveColumn("number", 0));
    REQUIRE(grid.SetColumnVisible("number", false));
    grid.Arrange({10, 20, 400, 220});
    REQUIRE_FALSE(grid.CellBounds({1, "number"}));
    grid.Arrange({0, 0, 50, 70});
    REQUIRE(grid.CellAt(5, 40)->column == "name");
}
TEST_CASE("Grid virtual scrolling reads only visible cells", "[ui][grid][virtual]")
{
    GridView grid;
    auto model = std::make_shared<CountingModel>();
    Setup(grid, model);
    REQUIRE(model->reads == 0);
    REQUIRE(model->rowReads == 0);
    REQUIRE(grid.Children().size() == 2);
    grid.Arrange({0, 0, 250, 180});
    grid.SetScrollOffset(0, 340000);
    REQUIRE(grid.FirstVisibleRow() == 10000);
    REQUIRE(grid.VisibleRowCount() < 8);
    Canvas canvas;
    canvas.Begin(250, 180);
    grid.Render(canvas);
    canvas.End();
    REQUIRE(model->reads <= grid.VisibleRowCount() * 3);
    REQUIRE(model->rowReads <= grid.VisibleRowCount() * 3);
    REQUIRE(grid.Children().size() == 2);
    REQUIRE(grid.CellAt(10, 40)->row == 10001);
    auto o = grid.Options();
    o.headerVisible = false;
    grid.SetOptions(o);
    grid.Arrange({0, 0, 250, 180});
    REQUIRE(grid.CellAt(10, 1)->row == 10001);
}
TEST_CASE("Grid drafts validate veto commit cancel and read-only transitions", "[ui][grid][edit]")
{
    auto model = Model();
    GridView grid;
    Setup(grid, model);
    int changed = 0;
    auto connection = grid.OnCellValueChanged.Connect(
        [&](const GridValueChange& e)
        {
            ++changed;
            REQUIRE(e.oldValue == GridValue{int64_t(12)});
            REQUIRE(e.newValue == GridValue{int64_t(42)});
        });
    REQUIRE(grid.BeginEdit({1, "number"}));
    auto* text = dynamic_cast<TextBox*>(&grid.ActiveEditor()->FocusTarget());
    REQUIRE(text);
    text->SetText("101");
    REQUIRE_FALSE(grid.FinishEdit(EditPhase::Commit));
    REQUIRE(grid.Editing());
    REQUIRE(model->Value({1, "number"}) == GridValue{int64_t(12)});
    text->SetText("42");
    auto veto = grid.OnCellValueChanging.Connect(
        [](GridValueChange& e)
        {
            e.cancel = true;
            e.error = "Rejected";
        });
    REQUIRE_FALSE(grid.FinishEdit(EditPhase::Commit));
    REQUIRE(grid.Editing());
    veto.Disconnect();
    REQUIRE(grid.FinishEdit(EditPhase::Commit));
    REQUIRE_FALSE(grid.Editing());
    REQUIRE(changed == 1);
    REQUIRE(grid.BeginEdit({1, "number"}));
    text = dynamic_cast<TextBox*>(&grid.ActiveEditor()->FocusTarget());
    text->SetText("17");
    auto o = grid.Options();
    o.readOnly = true;
    grid.SetOptions(o);
    REQUIRE_FALSE(grid.Editing());
    REQUIRE_FALSE(grid.BeginEdit({1, "name"}));
    REQUIRE(model->Value({1, "number"}) == GridValue{int64_t(42)});
    REQUIRE(grid.SetCellValue({1, "name"}, std::string("external")));
    REQUIRE(changed == 1);
}
TEST_CASE("Grid rejected model writes preserve draft and accepted data", "[ui][grid][edit]")
{
    auto model = Model();
    model->validate = [](const GridCell&, const GridValue& v) -> GridStatus
    {
        if (v == GridValue{std::string("reject")})
            return std::unexpected("No");
        return {};
    };
    GridView grid;
    Setup(grid, model);
    REQUIRE(grid.BeginEdit({1, "name"}));
    auto* text = dynamic_cast<TextBox*>(&grid.ActiveEditor()->FocusTarget());
    text->SetText("reject");
    REQUIRE_FALSE(grid.FinishEdit(EditPhase::Commit));
    REQUIRE(grid.Editing());
    REQUIRE(text->Text() == "reject");
    REQUIRE(model->Value({1, "name"}) == GridValue{std::string("Zulu")});
    text->SetText("accept");
    REQUIRE(grid.FinishEdit(EditPhase::Commit));
    REQUIRE(model->Value({1, "name"}) == GridValue{std::string("accept")});
}
TEST_CASE("Grid external assignment cancels the edited cell silently", "[ui][grid][edit]")
{
    auto model = Model();
    GridView grid;
    Setup(grid, model);
    int changes = 0;
    auto event = grid.OnCellValueChanged.Connect([&](const GridValueChange&) { ++changes; });
    REQUIRE(grid.BeginEdit({1, "name"}));
    REQUIRE(model->SetValue({1, "name"}, std::string("external")));
    REQUIRE_FALSE(grid.Editing());
    REQUIRE(changes == 0);
    REQUIRE(grid.SetCellValue({1, "name"}, std::string("notified"), true));
    REQUIRE(changes == 1);
    REQUIRE(grid.SetCellValue({1, "name"}, std::string("notified"), true));
    REQUIRE(changes == 1);
}
TEST_CASE("Grid copy follows display order formats and quotes fields", "[ui][grid][copy]")
{
    auto model = Model();
    GridView grid;
    Setup(grid, model);
    auto o = grid.Options();
    o.multipleSelection = true;
    o.copyHeaders = true;
    grid.SetOptions(o);
    REQUIRE(model->SetValue({1, "name"}, std::string("A\tB\"C")));
    REQUIRE(grid.SelectCell({1, "name"}));
    REQUIRE(grid.SelectCell({2, "name"}, false, true));
    REQUIRE(grid.Sort("number", GridSortDirection::Ascending));
    REQUIRE(grid.MoveColumn("number", 0));
    REQUIRE(grid.SetColumnVisible("enabled", false));
    REQUIRE(grid.CopyText() == "number\tname\r\n2\tAlpha\r\n12\t\"A\tB\"\"C\"");
    o.copy = false;
    grid.SetOptions(o);
    REQUIRE(grid.CopyText().empty());
}
TEST_CASE("Grid keyboard navigation preserves current-cell and row selection semantics", "[ui][grid][keyboard]")
{
    GridView grid;
    Setup(grid, Model());
    auto o = grid.Options();
    o.multipleSelection = true;
    grid.SetOptions(o);
    grid.SelectCell({1, "name"});
    REQUIRE(grid.Dispatch({.kind = InputKind::KeyDown, .key = LWS::KeyCode::Down, .shift = true}));
    REQUIRE(grid.Selection().size() == 2);
    REQUIRE(grid.Dispatch({.kind = InputKind::KeyDown, .key = LWS::KeyCode::Down, .control = true}));
    REQUIRE(grid.CurrentCell()->row == 3);
    REQUIRE(grid.Selection().size() == 2);
    REQUIRE(grid.Dispatch({.kind = InputKind::KeyDown, .key = LWS::KeyCode::Space, .control = true}));
    REQUIRE(grid.Selection().size() == 3);
    REQUIRE(grid.Dispatch({.kind = InputKind::KeyDown, .key = LWS::KeyCode::Tab}));
    REQUIRE(grid.CurrentCell()->column == "number");
}
TEST_CASE("Grid pointer sorting resizing cancellation and reordering use public operations", "[ui][grid][pointer]")
{
    GridView grid;
    Setup(grid, Model());
    grid.Dispatch({.kind = InputKind::Down, .x = 20, .y = 10});
    grid.Dispatch({.kind = InputKind::Up, .x = 20, .y = 10});
    REQUIRE(grid.SortDirection() == GridSortDirection::Ascending);
    grid.Dispatch({.kind = InputKind::Down, .x = 118, .y = 10});
    grid.Dispatch({.kind = InputKind::Move, .x = 160, .y = 10});
    REQUIRE(grid.Columns()[0].width > 120);
    grid.Dispatch({.kind = InputKind::KeyDown, .key = LWS::KeyCode::Escape});
    REQUIRE(grid.Columns()[0].width == 120);
    grid.Arrange({0, 0, 600, 400});
    grid.Dispatch({.kind = InputKind::Down, .x = 20, .y = 10});
    grid.Dispatch({.kind = InputKind::Move, .x = 170, .y = 10});
    grid.Dispatch({.kind = InputKind::Up, .x = 170, .y = 10});
    REQUIRE(grid.Columns()[1].id == "name");
    REQUIRE(grid.SortDirection() == GridSortDirection::Ascending);
}
TEST_CASE("Grid composite focus validation prevents leaving a rejected edit", "[ui][grid][focus]")
{
    Environment env;
    env.host->SetInvalidEditPolicy(InvalidEditPolicy::BlockOnLeave);
    auto root = std::make_unique<DockPanel>();
    auto grid = std::make_unique<GridView>();
    auto* view = grid.get();
    Setup(*view, Model());
    auto& outside = root->AddDocked(std::make_unique<Button>("Outside"), Dock::Bottom, 40);
    root->AddDocked(std::move(grid), Dock::Fill);
    env.host->SetRoot(std::move(root));
    env.host->Update();
    REQUIRE(env.host->Focus(view));
    REQUIRE(view->BeginEdit({1, "number"}));
    REQUIRE(view->Editing());
    auto* text = dynamic_cast<TextBox*>(&view->ActiveEditor()->FocusTarget());
    text->SetText("999");
    REQUIRE_FALSE(env.host->Focus(&outside));
    REQUIRE(view->Editing());
    REQUIRE(env.host->IsFocused(*text));
    REQUIRE(env.host->Route({.kind = InputKind::KeyDown, .key = LWS::KeyCode::Escape}));
    REQUIRE_FALSE(view->Editing());
    REQUIRE(env.host->Focus(&outside));
    env.host->Update();
    REQUIRE(view->Children().size() == 2);
}
TEST_CASE("Horizontal scrollbar preserves vertical default and uses horizontal input", "[ui][grid][scrollbar]")
{
    ScrollBar bar;
    REQUIRE(bar.GetOrientation() == Orientation::Vertical);
    bar.SetOrientation(Orientation::Horizontal);
    bar.SetPage(.5);
    bar.Arrange({0, 0, 200, 20});
    bar.Dispatch({.kind = InputKind::Down, .x = 150, .y = 10});
    bar.Dispatch({.kind = InputKind::Up, .x = 150, .y = 10});
    REQUIRE(bar.Value() > .9);
    bar.Dispatch({.kind = InputKind::KeyDown, .key = LWS::KeyCode::Left});
    REQUIRE(bar.Value() < 1);
}


TEST_CASE("Resolution fraction sets row and pixel endpoints independently of scroll range", "[ui][grid][scrollbar]")
{
    for (auto orientation : {Orientation::Vertical, Orientation::Horizontal})
        for (auto mode : {ScrollResolution::Row, ScrollResolution::Pixel})
            for (double fraction : {.01, .1, .2, 1., 2., 10.})
                for (double range : {100., 10000., 1000000000.})
                {
                    ScrollBar bar;
                    const bool vertical = orientation == Orientation::Vertical;
                    bar.SetOrientation(orientation);
                    bar.Arrange(vertical ? Rect{380, 0, 20, 400} : Rect{0, 380, 400, 20});
                    bar.SetPage(.1);
                    bar.SetValue(.5);
                    bar.SetDynamicResolution(true);
                    REQUIRE(bar.ResolutionFraction() == .2);
                    bar.SetResolutionFraction(fraction);
                    bar.ConfigureResolution({0, 0, 400, 400}, range, 28, mode);
                    const auto input = [&](InputKind kind, float cross, float axis)
                    {
                        return bar.Dispatch({.kind = kind, .x = vertical ? cross : axis, .y = vertical ? axis : cross});
                    };
                    const double normal = range / 360;
                    const double endpoint = std::min(normal, (mode == ScrollResolution::Row ? 28. : 1.) * fraction);
                    input(InputKind::Down, 390, 200);
                    input(InputKind::Move, 180, 200);
                    REQUIRE(bar.Value() == .5);
                    const double display = endpoint == normal ? 100 : (mode == ScrollResolution::Row ? fraction : fraction / 28);
                    REQUIRE(bar.ScrollSpeed() == Catch::Approx(display));
                    input(InputKind::Move, 180, 201);
                    REQUIRE((bar.Value() - .5) * range == Catch::Approx(endpoint).margin(.000001));
                    const auto before = bar.Value();
                    input(InputKind::Move, 280, 201);
                    REQUIRE(bar.Value() == before);
                    input(InputKind::Move, 280, 202);
                    REQUIRE((bar.Value() - before) * range == Catch::Approx(std::sqrt(normal * endpoint)).margin(.000001));
                    const auto fine = bar.Value();
                    input(InputKind::Move, 390, 202);
                    REQUIRE(bar.Value() == fine);
                    REQUIRE(bar.ScrollSpeed() == 100);
                    bar.SetResolutionFraction(fraction == 1 ? .1 : 1);
                    REQUIRE_FALSE(bar.ResolutionDragging());
                    REQUIRE(bar.Value() == .5);
                }
    ScrollBar bar;
    GridView grid;
    ScrollView scroll;
    for (double invalid : {0., -1., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
    {
        REQUIRE_THROWS(bar.SetResolutionFraction(invalid));
        auto gridOptions = grid.Options();
        gridOptions.resolutionFraction = invalid;
        REQUIRE_THROWS(grid.SetOptions(gridOptions));
        auto scrollOptions = scroll.Options();
        scrollOptions.resolutionFraction = invalid;
        REQUIRE_THROWS(scroll.SetOptions(scrollOptions));
    }
}

TEST_CASE("Outward acceleration uses bounded multipliers and continuous relative dragging", "[ui][grid][scrollbar]")
{
    for (auto orientation : {Orientation::Vertical, Orientation::Horizontal})
        for (bool mirrored : {false, true})
            for (bool track : {false, true})
            {
                ScrollBar bar;
                const bool vertical = orientation == Orientation::Vertical;
                bar.SetOrientation(orientation);
                const float origin = mirrored ? 0 : 380;
                bar.Arrange(vertical ? Rect{origin, 0, 20, 400} : Rect{0, origin, 400, 20});
                bar.SetPage(.1);
                bar.SetValue(.5);
                bar.ConfigureResolution({0, 0, 400, 400}, 1000000, 28);
                bar.SetDynamicResolution(true);
                REQUIRE_FALSE(bar.OutwardAcceleration());
                const float cross = origin + 10, axis = track ? 300 : 200;
                const float outer = mirrored ? 0 : 400, sign = mirrored ? -1 : 1;
                const auto input = [&](InputKind kind, float perpendicular, float along)
                {
                    return bar.Dispatch({.kind = kind, .x = vertical ? perpendicular : along,
                                          .y = vertical ? along : perpendicular});
                };
                input(InputKind::Down, cross, axis);
                input(InputKind::Move, outer + sign * 40, axis);
                REQUIRE(bar.ScrollSpeed() == 100);
                input(InputKind::Up, outer + sign * 40, axis);
                bar.SetOutwardAcceleration(true);
                bar.SetValue(.5);
                input(InputKind::Down, cross, axis);
                const auto initial = bar.Value();
                input(InputKind::Move, outer, axis);
                REQUIRE(bar.ScrollSpeed() == 100);
                input(InputKind::Move, outer + sign * 20, axis);
                REQUIRE(bar.ScrollSpeed() == Catch::Approx(200));
                REQUIRE(bar.Value() == initial);
                input(InputKind::Move, outer + sign * 20, axis + 1);
                REQUIRE(bar.Value() - initial == Catch::Approx(2. / 360));
                const auto intermediate = bar.Value();
                input(InputKind::Move, outer + sign * 400, axis + 1);
                REQUIRE(bar.ScrollSpeed() == Catch::Approx(400));
                REQUIRE(bar.Value() == intermediate);
                input(InputKind::Move, outer + sign * 40, axis + 2);
                REQUIRE(bar.Value() - intermediate == Catch::Approx(4. / 360));
                const auto boosted = bar.Value();
                input(InputKind::Move, cross, axis + 2);
                REQUIRE(bar.ScrollSpeed() == 100);
                REQUIRE(bar.Value() == boosted);
                input(InputKind::Move, 200, axis + 2);
                REQUIRE(bar.ScrollSpeed() < 100);
                REQUIRE(bar.Value() == boosted);
                input(InputKind::Move, outer + sign * 40, 100000);
                REQUIRE(bar.Value() == 1);
                input(InputKind::Move, outer + sign * 40, 99999);
                REQUIRE(bar.Value() == Catch::Approx(1 - 4. / 360));
                input(InputKind::Move, outer + sign * 40, -100000);
                REQUIRE(bar.Value() == 0);
                input(InputKind::Cancel, cross, axis);
                REQUIRE(bar.Value() == Catch::Approx(.5));
                REQUIRE_FALSE(bar.ResolutionDragging());
                REQUIRE(bar.ScrollSpeed() == 100);
                bar.SetMaximumScrollSpeed(100);
                input(InputKind::Down, cross, 200);
                input(InputKind::Move, outer + sign * 40, 200);
                REQUIRE(bar.ScrollSpeed() == 100);
                bar.SetMaximumScrollSpeed(900);
                REQUIRE_FALSE(bar.ResolutionDragging());
                input(InputKind::Down, cross, 200);
                input(InputKind::Move, outer + sign * 20, 200);
                REQUIRE(bar.ScrollSpeed() == Catch::Approx(300));
                bar.SetOutwardDistance(.2);
                REQUIRE_FALSE(bar.ResolutionDragging());
                input(InputKind::Down, cross, 200);
                input(InputKind::Move, outer + sign * 40, 200);
                REQUIRE(bar.ScrollSpeed() == Catch::Approx(300));
                bar.SetVisible(false);
                REQUIRE_FALSE(bar.ResolutionDragging());
                REQUIRE(bar.ScrollSpeed() == 100);
                bar.SetVisible(true);
                input(InputKind::Down, cross, 200);
                input(InputKind::Move, outer + sign * 80, 200);
                input(InputKind::Up, outer + sign * 80, 201);
                REQUIRE_FALSE(bar.ResolutionDragging());
                REQUIRE(bar.ScrollSpeed() == 100);
                input(InputKind::Down, cross, 200);
                bar.SetOutwardAcceleration(false);
                REQUIRE_FALSE(bar.ResolutionDragging());
                REQUIRE(bar.ScrollSpeed() == 100);
            }
    ScrollBar bar;
    for (double invalid : {0., -1., 1.1, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
        REQUIRE_THROWS(bar.SetOutwardDistance(invalid));
    for (double invalid : {99., -1., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
        REQUIRE_THROWS(bar.SetMaximumScrollSpeed(invalid));
}

TEST_CASE("Outward acceleration options propagate and cancel captured container drags", "[ui][grid][scrollview]")
{
    Environment env;
    for (bool useGrid : {false, true})
    {
        GridView* grid = nullptr;
        ScrollView* scroll = nullptr;
        if (useGrid)
        {
            auto widget = std::make_unique<GridView>();
            grid = widget.get();
            grid->SetModel(std::make_shared<CountingModel>());
            auto columns = grid->Columns();
            for (auto& column : columns) column.width = 400;
            grid->SetColumns(columns);
            auto options = grid->Options();
            options.dynamicResolution = options.outwardAcceleration = true;
            options.resolutionFraction = .1;
            options.maximumScrollSpeed = 900;
            options.outwardDistance = .2;
            grid->SetOptions(options);
            env.host->SetRoot(std::move(widget));
        }
        else
        {
            auto widget = std::make_unique<ScrollView>();
            scroll = widget.get();
            scroll->SetContent(std::make_unique<Label>("Content"));
            scroll->SetContentMinimumSize({3000, 3000});
            ScrollViewOptions options;
            options.horizontalScroll = options.dynamicResolution = options.outwardAcceleration = true;
            options.resolutionFraction = .1;
            options.maximumScrollSpeed = 900;
            options.outwardDistance = .2;
            scroll->SetOptions(options);
            env.host->SetRoot(std::move(widget));
        }
        env.host->Update();
        auto* container = useGrid ? static_cast<Container*>(grid) : static_cast<Container*>(scroll);
        for (size_t i : {size_t(0), size_t(1)})
        {
            auto* bar = static_cast<ScrollBar*>(container->Children()[i].get());
            REQUIRE(bar->OutwardAcceleration());
            REQUIRE(bar->MaximumScrollSpeed() == 900);
            REQUIRE(bar->ResolutionFraction() == .1);
            REQUIRE(bar->OutwardDistance() == .2);
            if (!bar->Visible()) continue;
            const auto b = bar->Bounds(), area = container->Bounds();
            const bool vertical = i == 0;
            const float x = b.x + b.width / 2, y = b.y + b.height / 2;
            REQUIRE(env.host->Route({.kind = InputKind::Down, .x = x, .y = y}));
            REQUIRE(env.host->Route({.kind = InputKind::Move,
                .x = vertical ? b.x + b.width + area.width * .2f : x,
                .y = vertical ? y : b.y + b.height + area.height * .2f}));
            REQUIRE(bar->ScrollSpeed() == Catch::Approx(900).margin(.001));
            REQUIRE(internal::UIHostTestAccess::Captured(*env.host) == bar);
            REQUIRE(env.host->Route({.kind = InputKind::Up, .x = x, .y = y}));
            REQUIRE_FALSE(bar->ResolutionDragging());
            REQUIRE(bar->ScrollSpeed() == 100);
        }
        auto* bar = static_cast<ScrollBar*>(container->Children()[0].get());
        const auto b = bar->Bounds();
        REQUIRE(env.host->Route({.kind = InputKind::Down, .x = b.x + b.width / 2, .y = b.y + b.height / 2}));
        if (grid)
        {
            auto options = grid->Options();
            options.resolutionFraction = 1;
            grid->SetOptions(options);
            options.outwardDistance = 0;
            REQUIRE_THROWS(grid->SetOptions(options));
            options.outwardDistance = .1;
            options.maximumScrollSpeed = 99;
            REQUIRE_THROWS(grid->SetOptions(options));
        }
        else
        {
            auto options = scroll->Options();
            options.resolutionFraction = 1;
            scroll->SetOptions(options);
            options.outwardDistance = 0;
            REQUIRE_THROWS(scroll->SetOptions(options));
            options.outwardDistance = .1;
            options.maximumScrollSpeed = 99;
            REQUIRE_THROWS(scroll->SetOptions(options));
        }
        REQUIRE_FALSE(bar->ResolutionDragging());
        REQUIRE(internal::UIHostTestAccess::Captured(*env.host) == nullptr);
    }
}

TEST_CASE("Dynamic resolution uses relative motion and exponential sensitivity", "[ui][grid][scrollbar]")
{
    for (auto orientation : {Orientation::Vertical, Orientation::Horizontal})
    {
        ScrollBar bar;
        bar.SetResolutionFraction(1);
        bar.SetInwardDistance(.45);
        REQUIRE_FALSE(bar.DynamicResolution());
        bar.SetOrientation(orientation);
        const bool vertical = orientation == Orientation::Vertical;
        bar.Arrange(vertical ? Rect{380, 0, 20, 400} : Rect{0, 380, 400, 20});
        bar.SetPage(.1);
        bar.SetValue(.5);
        bar.ConfigureResolution({0, 0, 400, 400}, 1000000000., 28);
        bar.SetDynamicResolution(true);
        const auto input = [&](InputKind kind, float cross, float axis)
        {
            return bar.Dispatch({.kind = kind, .x = vertical ? cross : axis, .y = vertical ? axis : cross});
        };
        input(InputKind::Down, 390, 200);
        REQUIRE(bar.ResolutionDragging());
        const auto initial = bar.Value();
        input(InputKind::Move, 200, 200);
        REQUIRE(bar.Value() == initial);
        REQUIRE(bar.ScrollSpeed() == Catch::Approx(1));
        input(InputKind::Move, 200, 201);
        REQUIRE((bar.Value() - initial) * 1000000000. == Catch::Approx(28).margin(.0001));
        const auto fine = bar.Value();
        input(InputKind::Move, 290, 201);
        REQUIRE(bar.Value() == fine);
        REQUIRE(bar.ScrollSpeed() == Catch::Approx(10));
        input(InputKind::Move, 290, 202);
        const auto normal = 1000000000. / 360;
        REQUIRE((bar.Value() - fine) * 1000000000. == Catch::Approx(std::sqrt(normal * 28)).margin(.001));
        input(InputKind::Move, 100, 202);
        REQUIRE(bar.ScrollSpeed() == Catch::Approx(1));
        input(InputKind::Cancel, 100, 202);
        REQUIRE(bar.Value() == Catch::Approx(.5));
        REQUIRE_FALSE(bar.ResolutionDragging());
        REQUIRE(bar.ScrollSpeed() == 100);
        bar.ConfigureResolution({0, 0, 400, 400}, 1000000000., 28, ScrollResolution::Pixel);
        input(InputKind::Down, 390, 200);
        input(InputKind::Move, 200, 200);
        const auto pixel = bar.Value();
        input(InputKind::Move, 200, 201);
        REQUIRE((bar.Value() - pixel) * 1000000000. == Catch::Approx(1).margin(.0001));
        REQUIRE(bar.ScrollSpeed() == Catch::Approx(1. / 28));
        input(InputKind::Up, 200, 201);
        REQUIRE(bar.ScrollSpeed() == 100);
        REQUIRE_FALSE(bar.ResolutionDragging());
    }
}

TEST_CASE("Dynamic resolution handles opposite edges limits and lifecycle", "[ui][grid][scrollbar]")
{
    for (auto orientation : {Orientation::Vertical, Orientation::Horizontal})
    {
        ScrollBar bar;
        bar.SetResolutionFraction(1);
        bar.SetInwardDistance(.45);
        bar.SetOrientation(orientation);
        const bool vertical = orientation == Orientation::Vertical;
        bar.Arrange(vertical ? Rect{0, 0, 20, 400} : Rect{0, 0, 400, 20});
        bar.SetPage(.1);
        bar.ConfigureResolution({0, 0, 400, 400}, 1000000, 28);
        bar.SetDynamicResolution(true);
        const auto input = [&](InputKind kind, float cross, float axis)
        {
            return bar.Dispatch({.kind = kind, .x = vertical ? cross : axis, .y = vertical ? axis : cross});
        };
        const auto begin = [&]
        {
            bar.SetValue(.5);
            input(InputKind::Down, 10, 200);
            input(InputKind::Move, 200, 200);
            REQUIRE(bar.ScrollSpeed() == Catch::Approx(1));
        };
        begin();
        input(InputKind::Move, 200, 100000);
        REQUIRE(bar.Value() == 1);
        input(InputKind::Move, 200, 99999);
        REQUIRE(bar.Value() == Catch::Approx(1 - 28. / 1000000));
        input(InputKind::Move, 200, -100000);
        REQUIRE(bar.Value() == 0);
        input(InputKind::Up, 200, -100000);
        begin();
        input(InputKind::Move, -100, 200);
        REQUIRE(bar.ScrollSpeed() == 100);
        REQUIRE(bar.Value() == .5);
        bar.SetDynamicResolution(false);
        REQUIRE_FALSE(bar.ResolutionDragging());
        bar.SetDynamicResolution(true);
        begin();
        bar.SetOrientation(vertical ? Orientation::Horizontal : Orientation::Vertical);
        REQUIRE_FALSE(bar.ResolutionDragging());
        REQUIRE(bar.ScrollSpeed() == 100);
        bar.SetOrientation(orientation);
        begin();
        bar.ConfigureResolution({0, 0, 400, 400}, 1000000, 30);
        REQUIRE_FALSE(bar.ResolutionDragging());
        REQUIRE(bar.ScrollSpeed() == 100);
        bar.ConfigureResolution({0, 0, 400, 400}, 1000000, 28);
        begin();
        bar.SetPage(.2);
        REQUIRE_FALSE(bar.ResolutionDragging());
        REQUIRE(bar.ScrollSpeed() == 100);
        bar.SetPage(.1);
        begin();
        bar.Arrange(vertical ? Rect{0, 0, 20, 500} : Rect{0, 0, 500, 20});
        REQUIRE_FALSE(bar.ResolutionDragging());
        REQUIRE(bar.ScrollSpeed() == 100);
        bar.Arrange(vertical ? Rect{0, 0, 20, 400} : Rect{0, 0, 400, 20});
        begin();
        bar.SetVisible(false);
        REQUIRE_FALSE(bar.ResolutionDragging());
        REQUIRE(bar.ScrollSpeed() == 100);
    }
    ScrollBar bar;
    bar.SetDynamicResolution(true);
    bar.Arrange({380, 0, 20, 400});
    bar.ConfigureResolution({0, 0, 400, 400}, 0, 28);
    bar.Dispatch({.kind = InputKind::Down, .x = 390, .y = 200});
    REQUIRE_FALSE(bar.ResolutionDragging());
    bar.Finish(EditPhase::Cancel);
    REQUIRE_THROWS(bar.SetPage(std::numeric_limits<double>::quiet_NaN()));
    REQUIRE_THROWS(bar.ConfigureResolution({0, 0, 400, 400}, -1, 28));
    REQUIRE_THROWS(bar.ConfigureResolution({0, 0, 400, 400}, 100, 0));
}


TEST_CASE("Track presses engage dynamic resolution and release applies final motion", "[ui][grid][scrollbar]")
{
    for (auto orientation : {Orientation::Vertical, Orientation::Horizontal})
    {
        ScrollBar bar;
        bar.SetResolutionFraction(1);
        bar.SetInwardDistance(.45);
        const bool vertical = orientation == Orientation::Vertical;
        bar.SetOrientation(orientation);
        bar.Arrange(vertical ? Rect{380, 0, 20, 400} : Rect{0, 380, 400, 20});
        bar.SetPage(.1);
        bar.ConfigureResolution({0, 0, 400, 400}, 1000000, 28);
        bar.SetDynamicResolution(true);
        const auto input = [&](InputKind kind, float cross, float axis, LWS::MouseButton button = LWS::MouseButton::Left)
        {
            return bar.Dispatch({.kind = kind, .x = vertical ? cross : axis, .y = vertical ? axis : cross, .button = button});
        };
        for (float cross : {380.f, 390.f, 399.f})
        {
            bar.SetValue(.1);
            input(InputKind::Down, cross, 300);
            REQUIRE(bar.Value() > .7);
            REQUIRE(bar.ResolutionDragging());
            input(InputKind::Move, 200, 300);
            const double jumped = bar.Value();
            REQUIRE(bar.ScrollSpeed() == Catch::Approx(1));
            input(InputKind::Up, 200, 301, LWS::MouseButton::Right);
            REQUIRE(bar.ResolutionDragging());
            REQUIRE(bar.Value() == jumped);
            input(InputKind::Down, 200, 301);
            REQUIRE(bar.Value() == jumped);
            input(InputKind::Up, 200, 301);
            REQUIRE(bar.Value() == Catch::Approx(jumped + 28. / 1000000));
            REQUIRE_FALSE(bar.ResolutionDragging());
            REQUIRE(bar.ScrollSpeed() == 100);
            bar.SetValue(.1);
            input(InputKind::Down, cross, 300);
            input(InputKind::Cancel, 200, 300);
            REQUIRE(bar.Value() == Catch::Approx(.1));
        }
        bar.SetValue(.1);
        auto disable = bar.OnScrollSpeed.Connect([&](double speed)
        {
            if (speed < 100) bar.SetDynamicResolution(false);
        });
        input(InputKind::Down, 390, 300);
        input(InputKind::Move, 200, 301);
        REQUIRE_FALSE(bar.ResolutionDragging());
        REQUIRE(bar.Value() == Catch::Approx(.1));
        REQUIRE(bar.ScrollSpeed() == 100);
    }
}
TEST_CASE("Host routes captured scrollbar movement and cancels native lifecycle events", "[ui][grid][scrollbar]")
{
    Environment env;
    auto widget = std::make_unique<GridView>();
    auto* grid = widget.get();
    auto model = std::make_shared<CountingModel>();
    grid->SetModel(model);
    auto options = grid->Options();
    options.dynamicResolution = true;
    options.resolutionFraction = 1;
    grid->SetOptions(options);
    env.host->SetRoot(std::move(widget));
    env.host->Update();
    auto* bar = static_cast<ScrollBar*>(grid->Children().front().get());
    const auto begin = [&]
    {
        grid->SetPreciseScrollOffset(0, 0);
        env.host->Update();
        const auto b = bar->Bounds();
        REQUIRE(env.host->Route({.kind = InputKind::Down, .x = b.x + b.width / 2, .y = b.y + b.height / 2}));
        REQUIRE(bar->ResolutionDragging());
        REQUIRE(internal::UIHostTestAccess::Captured(*env.host) == bar);
        return b;
    };
    auto b = begin();
    REQUIRE(env.host->Route({.kind = InputKind::Move, .x = -100, .y = b.y + b.height / 2}));
    REQUIRE(bar->ScrollSpeed() == Catch::Approx(1));
    const double fine = grid->PreciseScrollOffset().height;
    REQUIRE(env.host->Route({.kind = InputKind::Up, .x = -100, .y = b.y + b.height / 2 + 1}));
    REQUIRE(grid->PreciseScrollOffset().height == Catch::Approx(fine + options.rowHeight));
    REQUIRE_FALSE(internal::UIHostTestAccess::Captured(*env.host));
    REQUIRE_FALSE(bar->ResolutionDragging());
    begin();
    internal::UIHostTestAccess::Event(*env.host, LWS::EventMouseCaptureLost{});
    REQUIRE_FALSE(bar->ResolutionDragging());
    REQUIRE_FALSE(internal::UIHostTestAccess::Captured(*env.host));
    REQUIRE(grid->PreciseScrollOffset().height == 0);
    begin();
    internal::UIHostTestAccess::Event(*env.host, LWS::EventFocusLost{});
    REQUIRE_FALSE(bar->ResolutionDragging());
    REQUIRE_FALSE(internal::UIHostTestAccess::Captured(*env.host));
    REQUIRE(grid->PreciseScrollOffset().height == 0);
    begin();
    REQUIRE(env.host->Route({.kind = InputKind::KeyDown, .key = LWS::KeyCode::Escape}));
    REQUIRE_FALSE(bar->ResolutionDragging());
    REQUIRE(grid->PreciseScrollOffset().height == 0);
    begin();
    bar->SetEnabled(false);
    REQUIRE_FALSE(bar->ResolutionDragging());
    REQUIRE_FALSE(internal::UIHostTestAccess::Captured(*env.host));
    bar->SetEnabled(true);
    begin();
    const auto retained = bar->Handle();
    env.host->SetRoot(std::make_unique<Label>("Replacement"));
    REQUIRE_FALSE(retained);
    REQUIRE_FALSE(internal::UIHostTestAccess::Captured(*env.host));
}

TEST_CASE("Grid retains row and pixel precision near one hundred million rows", "[ui][grid][scrollbar]")
{
    GridView grid;
    auto model = std::make_shared<CountingModel>();
    model->rows = 100000000;
    Setup(grid, model);
    auto options = grid.Options();
    options.rowHeight = 28;
    options.dynamicResolution = true;
    options.resolutionFraction = 1;
    grid.SetOptions(options);
    grid.Arrange({0, 0, 600, 400});
    grid.SetPreciseScrollOffset(0, 99999980. * 28);
    REQUIRE(grid.FirstVisibleRow() == 99999980);
    auto* bar = static_cast<ScrollBar*>(grid.Children().front().get());
    const auto bounds = bar->Bounds();
    const float x = bounds.x + bounds.width / 2, y = bounds.y + bounds.height - 5;
    bar->Dispatch({.kind = InputKind::Down, .x = x, .y = y});
    bar->Dispatch({.kind = InputKind::Move, .x = 0, .y = y});
    const auto before = grid.PreciseScrollOffset().height;
    bar->Dispatch({.kind = InputKind::Move, .x = 0, .y = y + 1});
    REQUIRE(grid.PreciseScrollOffset().height - before == Catch::Approx(28).margin(.00001));
    REQUIRE(grid.FirstVisibleRow() == 99999981);
    const auto cell = grid.CellAt(10, grid.Bounds().y + options.headerHeight + 1);
    REQUIRE(cell);
    REQUIRE(cell->row == 99999982);
    bar->Finish(EditPhase::Commit);
    options.centerResolution = ScrollResolution::Pixel;
    grid.SetOptions(options);
    grid.Arrange({0, 0, 600, 400});
    bar->Dispatch({.kind = InputKind::Down, .x = x, .y = y});
    bar->Dispatch({.kind = InputKind::Move, .x = 0, .y = y});
    const auto pixel = grid.PreciseScrollOffset().height;
    bar->Dispatch({.kind = InputKind::Move, .x = 0, .y = y + 1});
    REQUIRE(grid.PreciseScrollOffset().height - pixel == Catch::Approx(1).margin(.00001));
    bar->Finish(EditPhase::Commit);
    REQUIRE(grid.SelectCell({100000000, "a"}));
    REQUIRE(grid.CellBounds({100000000, "a"}));
    REQUIRE(model->reads < 1000);
}


TEST_CASE("Optional line numbers reserve pinned read-only space without model columns", "[ui][grid][lines]")
{
    GridView grid;
    const auto model = Model();
    Setup(grid, model);
    REQUIRE_FALSE(grid.Options().lineNumbers);
    const auto original = grid.CellBounds({1, "name"});
    REQUIRE(original);
    auto options = grid.Options();
    options.lineNumbers = true;
    grid.SetOptions(options);
    grid.Arrange({0, 0, 300, 180});
    const auto numbered = grid.CellBounds({1, "name"});
    REQUIRE(numbered);
    REQUIRE(numbered->x > original->x);
    REQUIRE(grid.Columns().size() == model->Columns().size());
    const float gutter = numbered->x / 2;
    REQUIRE_FALSE(grid.CellAt(gutter, numbered->y + 2));
    grid.Dispatch({.kind = InputKind::Down, .x = gutter, .y = numbered->y + 2, .clickCount = 2});
    REQUIRE_FALSE(grid.Editing());
    REQUIRE_FALSE(grid.CurrentCell());
    REQUIRE_FALSE(grid.SetCellValue({1, "Line"}, int64_t(123)));
    REQUIRE(grid.Sort("name", GridSortDirection::Ascending));
    REQUIRE(grid.DisplayRowId(0) == 2);
    auto columns = grid.Columns();
    columns[0].pin = GridPin::Left;
    grid.SetColumns(columns);
    grid.Arrange({0, 0, 300, 180});
    const auto pinned = grid.CellBounds({2, "name"});
    REQUIRE(pinned);
    grid.SetScrollOffset(80, 0);
    REQUIRE(grid.CellBounds({2, "name"})->x == pinned->x);
    REQUIRE_FALSE(grid.CellAt(gutter, pinned->y + 2));
    options.headerVisible = false;
    grid.SetOptions(options);
    grid.Arrange({0, 0, 300, 180});
    REQUIRE(grid.CellBounds({2, "name"})->y == 0);
    options.lineNumbers = false;
    grid.SetOptions(options);
    grid.Arrange({0, 0, 300, 180});
    REQUIRE(grid.CellBounds({2, "name"})->x == 0);
    grid.SetModel({});
    options.lineNumbers = true;
    grid.SetOptions(options);
    grid.Arrange({0, 0, 10, 100});
    REQUIRE(grid.VisibleRowCount() == 0);
}
TEST_CASE("Line numbers remain bounded at the end of a hundred million rows", "[ui][grid][lines]")
{
    Environment env;
    auto widget = std::make_unique<GridView>();
    auto* grid = widget.get();
    const auto model = std::make_shared<CountingModel>();
    model->rows = 100000000;
    grid->SetModel(model);
    auto options = grid->Options();
    options.lineNumbers = true;
    options.rowHeight = 28;
    grid->SetOptions(options);
    env.host->SetRoot(std::move(widget));
    env.host->Update();
    grid->SetPreciseScrollOffset(0, 99999980. * 28);
    env.host->Update();
    REQUIRE(grid->FirstVisibleRow() == 99999980);
    const auto bounds = grid->CellBounds({99999981, "a"});
    REQUIRE(bounds);
    REQUIRE(bounds->x > 0);
    REQUIRE_FALSE(grid->CellAt(bounds->x / 2, bounds->y + 1));
    const auto reads = model->reads;
    Canvas canvas;
    canvas.Begin(640, 480);
    grid->Render(canvas);
    canvas.End();
    REQUIRE(model->reads - reads <= grid->VisibleRowCount() * grid->Columns().size());
    REQUIRE(model->reads < 1000);
    REQUIRE(grid->SelectCell({100000000, "a"}));
    REQUIRE(grid->CellBounds({100000000, "a"}));
}


TEST_CASE("Configured inward percentages control both scrollbar axes", "[ui][grid][scrollbar]")
{
    for (auto orientation : {Orientation::Vertical, Orientation::Horizontal})
        for (bool opposite : {false, true})
            for (double distance : {.3, .5, 1.})
            {
                ScrollBar bar;
                bar.SetResolutionFraction(1);
                bar.SetOrientation(orientation);
                const bool vertical = orientation == Orientation::Vertical;
                bar.Arrange(vertical ? Rect{opposite ? 0.f : 380.f, 0, 20, 400}
                                     : Rect{0, opposite ? 0.f : 380.f, 400, 20});
                bar.SetPage(.1);
                bar.SetValue(.5);
                bar.ConfigureResolution({0, 0, 400, 400}, 1000000, 28);
                bar.SetInwardDistance(distance);
                bar.SetDynamicResolution(true);
                const auto input = [&](InputKind kind, float cross, float axis)
                {
                    return bar.Dispatch({.kind = kind, .x = vertical ? cross : axis, .y = vertical ? axis : cross});
                };
                input(InputKind::Down, opposite ? 10 : 390, 200);
                const float edge = opposite ? 20 : 380, direction = opposite ? 1 : -1;
                input(InputKind::Move, edge + direction * float(400 * distance / 2), 200);
                REQUIRE(bar.ScrollSpeed() == Catch::Approx(10));
                input(InputKind::Move, edge + direction * float(400 * distance), 200);
                REQUIRE(bar.ScrollSpeed() == Catch::Approx(1));
                const auto value = bar.Value();
                input(InputKind::Move, edge + direction * float(400 * distance), 201);
                REQUIRE(bar.Value() == Catch::Approx(value + 28. / 1000000));
                bar.SetInwardDistance(distance == .3 ? .5 : .3);
                REQUIRE_FALSE(bar.ResolutionDragging());
                REQUIRE(bar.Value() == .5);
            }
    ScrollBar bar;
    REQUIRE_THROWS(bar.SetInwardDistance(0));
    REQUIRE_THROWS(bar.SetInwardDistance(1.01));
    REQUIRE_THROWS(bar.SetInwardDistance(std::numeric_limits<double>::quiet_NaN()));
    bar.Arrange({380, 0, 20, 400});
    bar.SetPage(.1);
    bar.ConfigureResolution({0, 0, 400, 400}, 100, 28);
    bar.SetDynamicResolution(true);
    bar.Dispatch({.kind = InputKind::Down, .x = 390, .y = 20});
    bar.Dispatch({.kind = InputKind::Move, .x = 180, .y = 20});
    REQUIRE(bar.ScrollSpeed() == 100);
    bar.Finish(EditPhase::Commit);
}
TEST_CASE("Line label controls gutter measurement including empty labels", "[ui][grid][lines]")
{
    Environment env;
    auto widget = std::make_unique<GridView>();
    auto* grid = widget.get();
    grid->SetModel(Model());
    auto options = grid->Options();
    options.lineNumbers = true;
    grid->SetOptions(options);
    env.host->SetRoot(std::move(widget));
    env.host->Update();
    const auto original = grid->CellBounds({1, "name"});
    REQUIRE(original);
    options.lineNumberLabel = "Position in the displayed table";
    grid->SetOptions(options);
    env.host->Update();
    REQUIRE(grid->CellBounds({1, "name"})->x > original->x);
    options.lineNumberLabel.clear();
    grid->SetOptions(options);
    env.host->Update();
    REQUIRE(grid->CellBounds({1, "name"})->x < original->x);
    options.inwardDistance = 0;
    REQUIRE_THROWS(grid->SetOptions(options));
}
TEST_CASE("Two-axis ScrollView preserves child input and dynamic captured drags", "[ui][scrollview]")
{
    Environment env;
    auto widget = std::make_unique<ScrollView>();
    auto* scroll = widget.get();
    auto cards = std::make_unique<Grid>();
    cards->columns = {200, 200, 200};
    auto& box = cards->Emplace<CheckBox>("Keep me");
    auto& text = cards->Emplace<TextBox>("Original");
    cards->Emplace<Button>("Button");
    scroll->SetContent(std::move(cards));
    ScrollViewOptions options;
    options.horizontalScroll = options.dynamicResolution = true;
    options.resolution = ScrollResolution::Pixel;
    options.resolutionFraction = 1;
    scroll->SetOptions(options);
    scroll->SetContentMinimumSize({3000, 3000});
    env.host->SetRoot(std::move(widget));
    env.host->Update();
    auto* vertical = static_cast<ScrollBar*>(scroll->Children()[0].get());
    auto* horizontal = static_cast<ScrollBar*>(scroll->Children()[1].get());
    REQUIRE(vertical->Visible());
    REQUIRE(horizontal->Visible());
    const auto click = [&](Control& target)
    {
        const auto b = target.Bounds();
        REQUIRE(env.host->Route({.kind = InputKind::Down, .x = b.x + 5, .y = b.y + b.height / 2}));
        REQUIRE(env.host->Route({.kind = InputKind::Up, .x = b.x + 5, .y = b.y + b.height / 2}));
    };
    click(box);
    REQUIRE(box.Value());
    click(text);
    REQUIRE(env.host->Route({.kind = InputKind::KeyDown, .key = LWS::KeyCode::End}));
    REQUIRE(env.host->Route({.kind = InputKind::Text, .text = " edited"}));
    REQUIRE(env.host->Focus(&box));
    scroll->SetScrollOffset(1000, 1000);
    scroll->SetScrollOffset(0, 0);
    REQUIRE(box.Value());
    REQUIRE(text.Text() == "Original edited");
    for (auto* bar : {vertical, horizontal})
    {
        const auto b = bar->Bounds();
        const bool v = bar == vertical;
        const float x = b.x + b.width / 2, y = b.y + b.height / 2;
        REQUIRE(env.host->Route({.kind = InputKind::Down, .x = x, .y = y}));
        REQUIRE(bar->ResolutionDragging());
        const auto before = scroll->ScrollOffset();
        REQUIRE(env.host->Route({.kind = InputKind::Move, .x = v ? -100 : x, .y = v ? y : -100}));
        REQUIRE(env.host->Route({.kind = InputKind::Up, .x = v ? -100 : x + 1, .y = v ? y + 1 : -100}));
        const auto after = scroll->ScrollOffset();
        REQUIRE((v ? after.height - before.height : after.width - before.width) == Catch::Approx(1).margin(.001));
        REQUIRE_FALSE(bar->ResolutionDragging());
    }
    scroll->SetScrollOffset(0, 0);
    const auto b = scroll->Bounds();
    REQUIRE(env.host->Route({.kind = InputKind::Wheel, .x = b.x + 10, .y = b.y + 10, .wheel = -1, .shift = true}));
    REQUIRE(scroll->ScrollOffset().width > 0);
    REQUIRE(scroll->Offset() == 0);
    REQUIRE(env.host->Route({.kind = InputKind::Wheel, .x = b.x + 10, .y = b.y + 10, .wheel = -1}));
    REQUIRE(scroll->Offset() > 0);
    for (bool overlay : {false, true})
    {
        scroll->overlayScrollBar = overlay;
        scroll->Measure({640, 480});
        scroll->Arrange({0, 0, 640, 480});
        REQUIRE(scroll->HitTest(horizontal->Bounds().x + 5, horizontal->Bounds().y + 5) == horizontal);
        REQUIRE(scroll->HitTest(vertical->Bounds().x + 5, vertical->Bounds().y + 5) == vertical);
    }
    scroll->overlayScrollBar = false;
    scroll->SetScrollOffset(100000, 100000);
    const auto maximum = scroll->ScrollOffset();
    REQUIRE(maximum.width < 3000);
    REQUIRE(maximum.height < 3000);
    scroll->Measure({1200, 900});
    scroll->Arrange({0, 0, 1200, 900});
    REQUIRE(scroll->ScrollOffset().width < maximum.width);
    REQUIRE(scroll->ScrollOffset().height < maximum.height);
    options.horizontalScroll = false;
    scroll->SetOptions(options);
    env.host->Update();
    REQUIRE(scroll->ScrollOffset().width == 0);
    REQUIRE_FALSE(horizontal->Visible());
    scroll->SetContent({});
    env.host->Update();
    REQUIRE(scroll->ScrollOffset().width == 0);
    REQUIRE(scroll->Offset() == 0);
    REQUIRE_FALSE(vertical->Visible());
    REQUIRE_FALSE(horizontal->Visible());
    options.inwardDistance = 0;
    REQUIRE_THROWS(scroll->SetOptions(options));
    REQUIRE_THROWS(scroll->SetContentMinimumSize({-1, 0}));
}


TEST_CASE("ScrollView reuses intrinsic measurement and invalidates on geometry changes", "[ui][scrollview][cache]")
{
    class MeasuredContent final : public Container
    {
      public:
        int measures = 0;
        Size extent{3000, 3000};
      protected:
        Size OnMeasure(Size) override { ++measures; return extent; }
    };
    ScrollView scroll;
    auto content = std::make_unique<MeasuredContent>();
    auto* measured = content.get();
    auto& child = measured->Emplace<Label>("Initial");
    scroll.SetOptions({.horizontalScroll = true});
    scroll.SetContent(std::move(content));
    scroll.Measure({640, 480});
    scroll.Arrange({0, 0, 640, 480});
    scroll.SetScrollOffset(1, 1);
    const auto initial = measured->measures;
    for (int i = 2; i < 100; ++i) scroll.SetScrollOffset(float(i), float(i));
    REQUIRE(measured->measures == initial);
    child.SetText("Longer text that changes intrinsic width");
    scroll.SetScrollOffset(100, 100);
    REQUIRE(measured->measures > initial);
    auto previous = measured->measures;
    measured->Emplace<Button>("New child");
    scroll.SetScrollOffset(101, 101);
    REQUIRE(measured->measures > previous);
    previous = measured->measures;
    auto theme = scroll.Style();
    theme.font.size += 2;
    scroll.SetStyle(theme);
    scroll.SetScrollOffset(102, 102);
    REQUIRE(measured->measures > previous);
    previous = measured->measures;
    scroll.overlayScrollBar = true;
    scroll.SetScrollOffset(103, 103);
    REQUIRE(measured->measures > previous);
    previous = measured->measures;
    scroll.Arrange({0, 0, 800, 600});
    REQUIRE(measured->measures > previous);
    measured->extent = {100, 100};
    measured->Invalidate(true);
    scroll.SetScrollOffset(104, 104);
    REQUIRE(scroll.ScrollOffset().width == 0);
    REQUIRE(scroll.ScrollOffset().height == 0);
}
TEST_CASE("Fixed speed feedback does not trigger content remeasurement", "[ui][scrollview][cache]")
{
    class MeasuredContent final : public Control
    {
      public:
        int measures = 0;
      protected:
        Size OnMeasure(Size) override { ++measures; return {3000, 3000}; }
    };
    Environment env;
    auto root = std::make_unique<DockPanel>();
    auto feedback = std::make_unique<demo::SpeedReadout>();
    auto* readout = feedback.get();
    root->AddDocked(std::move(feedback), Dock::Top);
    auto scroll = std::make_unique<ScrollView>();
    auto* view = scroll.get();
    auto content = std::make_unique<MeasuredContent>();
    auto* measured = content.get();
    view->SetOptions({.horizontalScroll = true});
    view->SetContent(std::move(content));
    root->AddDocked(std::move(scroll), Dock::Fill);
    env.host->SetRoot(std::move(root));
    env.host->Update();
    view->SetScrollOffset(1, 1);
    env.host->Update();
    const auto initial = measured->measures;
    for (int i = 2; i < 20; ++i)
    {
        readout->SetSpeed(100. / i);
        view->SetScrollOffset(float(i), float(i));
        env.host->Update();
    }
    REQUIRE(measured->measures == initial);
    auto theme = env.host->Style();
    theme.font.size += 3;
    env.host->SetTheme(theme);
    env.host->Update();
    REQUIRE(measured->measures > initial);
}

TEST_CASE("Grid typed and custom editors share the transaction contract", "[ui][grid][edit]")
{
    GridModelColumn choice{"choice", GridValueType::Choice};
    choice.choices = {{"First", "first"}, {"Second", "second"}};
    GridModelColumn decimal{"decimal", GridValueType::Decimal};
    decimal.decimal = {0., 1., .1};
    auto model = std::make_shared<MemoryGridModel>(
        std::vector<GridModelColumn>{{"text"}, choice, decimal, {"check", GridValueType::Boolean}});
    model->AddRow({std::string("original"), std::string("first"), .5, false});
    GridView grid;
    Setup(grid, model);
    REQUIRE(grid.BeginEdit({1, "decimal"}));
    auto* text = dynamic_cast<TextBox*>(&grid.ActiveEditor()->FocusTarget());
    REQUIRE(text);
    text->SetText("0.75");
    REQUIRE(grid.FinishEdit(EditPhase::Commit));
    REQUIRE(model->Value({1, "decimal"}) == GridValue{.75});
    REQUIRE(grid.BeginEdit({1, "choice"}));
    auto* combo = dynamic_cast<ComboBox*>(&grid.ActiveEditor()->FocusTarget());
    REQUIRE(combo);
    combo->SetValue("second");
    REQUIRE(grid.FinishEdit(EditPhase::Commit));
    REQUIRE(model->Value({1, "choice"}) == GridValue{std::string("second")});
    REQUIRE(grid.BeginEdit({1, "check"}));
    auto* checkbox = dynamic_cast<CheckBox*>(&grid.ActiveEditor()->FocusTarget());
    REQUIRE(checkbox);
    checkbox->SetValue(true);
    REQUIRE(grid.FinishEdit(EditPhase::Cancel));
    REQUIRE(model->Value({1, "check"}) == GridValue{false});
    class Custom final : public GridCellEditor
    {
      public:

        Custom() { field = &Emplace<TextBox>("custom"); }
        std::expected<GridValue, std::string> Draft() const override { return GridValue{field->Text() + "!"}; }
        void SetValidation(std::string error) override { field->SetValidation(std::move(error)); }
        Control& FocusTarget() override { return *field; }

      protected:

        Size OnMeasure(Size size) override
        {
            field->Measure(size);
            return size;
        }
        void OnArrange() override { field->Arrange(Bounds()); }

      private:

        TextBox* field;
    };
    auto columns = grid.Columns();
    int factories = 0;
    columns[0].editor = [&](const GridModelColumn&, const GridValue&)
    {
        ++factories;
        return std::make_unique<Custom>();
    };
    grid.SetColumns(columns);
    grid.Arrange({0, 0, 600, 400});
    REQUIRE(grid.BeginEdit({1, "text"}));
    REQUIRE(factories == 1);
    REQUIRE(grid.FinishEdit(EditPhase::Commit));
    REQUIRE(model->Value({1, "text"}) == GridValue{std::string("custom!")});
    model->SetCellReadOnly({1, "text"}, true);
    REQUIRE_FALSE(grid.BeginEdit({1, "text"}));
}
TEST_CASE("Grid configured widths and capabilities use one validated path", "[ui][grid][layout]")
{
    auto model = Model();
    GridView grid;
    Setup(grid, model);
    auto columns = grid.Columns();
    columns[0].widthMode = GridWidthMode::Content;
    columns[1].widthMode = GridWidthMode::Proportional;
    columns[2].width = 80;
    grid.SetColumns(columns);
    grid.Arrange({0, 0, 600, 300});
    REQUIRE(grid.CellBounds({1, "number"})->width > 300);
    auto width = grid.Columns()[0].width;
    REQUIRE(model->SetValue({1, "name"}, std::string(40, 'x')));
    REQUIRE(grid.Columns()[0].width > width);
    auto veto = grid.OnColumnResizing.Connect([](GridColumnEvent& e) { e.cancel = true; });
    const auto before = grid.Columns()[0].width;
    REQUIRE_FALSE(grid.ResizeColumn("name", 180));
    REQUIRE(grid.Columns()[0].width == before);
    veto.Disconnect();
    auto o = grid.Options();
    o.reordering = o.resizing = o.sorting = false;
    grid.SetOptions(o);
    REQUIRE_FALSE(grid.MoveColumn("name", 1));
    REQUIRE_FALSE(grid.ResizeColumn("name", 180));
    REQUIRE_FALSE(grid.Sort("name", GridSortDirection::Ascending));
    columns[0].width = std::numeric_limits<float>::quiet_NaN();
    REQUIRE_THROWS_AS(grid.SetColumns(columns), std::invalid_argument);
    o.rowHeight = 0;
    REQUIRE_THROWS_AS(grid.SetOptions(o), std::invalid_argument);
}
TEST_CASE("Grid pointer exit and context requests report stable cells", "[ui][grid][events]")
{
    Environment env;
    auto root = std::make_unique<DockPanel>();
    auto grid = std::make_unique<GridView>();
    auto* view = grid.get();
    Setup(*view, Model());
    auto& outside = root->AddDocked(std::make_unique<Button>("Outside"), Dock::Bottom, 40);
    root->AddDocked(std::move(grid), Dock::Fill);
    env.host->SetRoot(std::move(root));
    env.host->Update();
    int enters = 0, leaves = 0, contexts = 0;
    auto entered = view->OnCellEnter.Connect(
        [&](const GridCellEvent& e)
        {
            ++enters;
            REQUIRE(e.cell.row == 1);
        });
    auto left = view->OnCellLeave.Connect(
        [&](const GridCellEvent& e)
        {
            ++leaves;
            REQUIRE(e.cell.row == 1);
        });
    auto context = view->OnContextRequest.Connect(
        [&](const GridCellEvent& e)
        {
            ++contexts;
            REQUIRE(e.cell.row == 1);
        });
    const auto cell = view->CellBounds({1, "name"});
    REQUIRE(cell);
    env.host->Route({.kind = InputKind::Move, .x = cell->x + 10, .y = cell->y + 10});
    REQUIRE(enters == 1);
    env.host->Route({.kind = InputKind::Move, .x = outside.Bounds().x + 10, .y = outside.Bounds().y + 10});
    REQUIRE(leaves == 1);
    REQUIRE(view->SelectCell({1, "name"}));
    REQUIRE(env.host->Focus(view));
    REQUIRE(env.host->Route({.kind = InputKind::KeyDown, .key = LWS::KeyCode::F10, .shift = true}));
    REQUIRE(contexts == 1);
    REQUIRE(env.host->HasContextMenu());
    env.host->CloseContextMenu();
}
TEST_CASE("Sparse cell copying preserves selected column alignment", "[ui][grid][copy]")
{
    GridView grid;
    Setup(grid, Model());
    auto options = grid.Options();
    options.fullRowSelect = false;
    options.multipleSelection = true;
    options.copyHeaders = true;
    grid.SetOptions(options);
    auto current = grid.OnCurrentCellChanged.Connect(
        [&](const std::optional<GridCell>& cell)
        {
            REQUIRE(cell);
            REQUIRE(grid.IsSelected(*cell));
        });
    REQUIRE(grid.SelectCell({1, "name"}));
    REQUIRE(grid.SelectCell({2, "number"}, false, true));
    REQUIRE(grid.CopyText() == "name\tnumber\r\nZulu\t\r\n\t2");
    grid.ClearSelection();
    REQUIRE(grid.CopyText().empty());
}

TEST_CASE("Single-click editing waits for release and respects selection gestures", "[ui][grid][singleclick]")
{
    GridView grid;
    Setup(grid, Model());
    REQUIRE_FALSE(grid.Options().singleClickEdit);
    auto options = grid.Options();
    options.singleClickEdit = true;
    options.multipleSelection = true;
    grid.SetOptions(options);
    int beginnings = 0;
    auto beginning = grid.OnEditBeginning.Connect([&](GridEditRequest&) { ++beginnings; });
    grid.Dispatch({.kind = InputKind::Down, .x = 10, .y = 45});
    REQUIRE_FALSE(grid.Editing());
    grid.Dispatch({.kind = InputKind::Up, .x = 10, .y = 45});
    REQUIRE(grid.Editing());
    REQUIRE(beginnings == 1);
    REQUIRE(grid.BeginEdit({1, "name"}));
    REQUIRE(beginnings == 1);
    grid.FinishEdit(EditPhase::Cancel);
    grid.Dispatch({.kind = InputKind::Down, .x = 10, .y = 45, .control = true});
    grid.Dispatch({.kind = InputKind::Up, .x = 10, .y = 45, .control = true});
    REQUIRE_FALSE(grid.Editing());
    grid.Dispatch({.kind = InputKind::Down, .x = 10, .y = 80, .shift = true});
    grid.Dispatch({.kind = InputKind::Up, .x = 10, .y = 80, .shift = true});
    REQUIRE_FALSE(grid.Editing());
    grid.Dispatch({.kind = InputKind::Down, .x = 10, .y = 45});
    grid.Dispatch({.kind = InputKind::Move, .x = 10, .y = 115});
    grid.Dispatch({.kind = InputKind::Up, .x = 10, .y = 115});
    REQUIRE_FALSE(grid.Editing());
    REQUIRE(grid.Selection().size() == 3);
    grid.Dispatch({.kind = InputKind::Down, .x = 10, .y = 45});
    grid.Dispatch({.kind = InputKind::Cancel});
    grid.Dispatch({.kind = InputKind::Up, .x = 10, .y = 45});
    REQUIRE_FALSE(grid.Editing());
    options.readOnly = true;
    grid.SetOptions(options);
    grid.Dispatch({.kind = InputKind::Down, .x = 10, .y = 45});
    grid.Dispatch({.kind = InputKind::Up, .x = 10, .y = 45});
    REQUIRE_FALSE(grid.Editing());
    options.readOnly = false;
    options.multipleSelection = false;
    grid.SetOptions(options);
    grid.Dispatch({.kind = InputKind::Down, .x = 10, .y = 45});
    grid.Dispatch({.kind = InputKind::Up, .x = 10, .y = 45});
    REQUIRE(grid.Editing());
    grid.FinishEdit(EditPhase::Cancel);
    grid.Dispatch({.kind = InputKind::Down, .x = 10, .y = 45});
    options.singleClickEdit = false;
    grid.SetOptions(options);
    grid.Dispatch({.kind = InputKind::Up, .x = 10, .y = 45});
    REQUIRE_FALSE(grid.Editing());
}
TEST_CASE("Non-sticky headers share scrolling geometry hit testing and extents", "[ui][grid][headers]")
{
    auto model = Model();
    GridView grid;
    Setup(grid, model);
    REQUIRE(grid.Options().stickyHeaders);
    auto options = grid.Options();
    options.stickyHeaders = false;
    grid.SetOptions(options);
    grid.Arrange({0, 0, 600, 100});
    grid.SetScrollOffset(0, 18);
    REQUIRE(grid.FirstVisibleRow() == 0);
    REQUIRE(grid.CellBounds({1, "name"})->y == 18);
    REQUIRE_FALSE(grid.CellAt(10, 10));
    REQUIRE(grid.CellAt(10, 20) == GridCell{1, "name"});
    grid.SetScrollOffset(0, 70);
    REQUIRE(grid.FirstVisibleRow() == 1);
    REQUIRE(grid.CellAt(10, 5) == GridCell{2, "name"});
    REQUIRE(grid.CellBounds({2, "name"})->y == 0);
    options.stickyHeaders = true;
    grid.SetOptions(options);
    grid.Arrange({0, 0, 600, 100});
    REQUIRE(grid.FirstVisibleRow() == 1);
    REQUIRE(grid.ScrollOffset().height == 34);
    REQUIRE_FALSE(grid.CellAt(10, 10));
    options.stickyHeaders = false;
    grid.SetOptions(options);
    grid.Arrange({0, 0, 600, 100});
    REQUIRE(grid.FirstVisibleRow() == 1);
    REQUIRE(grid.ScrollOffset().height == 70);
    options.headerVisible = false;
    grid.SetOptions(options);
    grid.Arrange({0, 0, 600, 100});
    REQUIRE(grid.FirstVisibleRow() == 1);
    REQUIRE(grid.ScrollOffset().height == 34);
    REQUIRE(grid.CellAt(10, 5) == GridCell{2, "name"});
    options.headerVisible = true;
    grid.SetOptions(options);
    grid.Arrange({0, 0, 600, 100});
    grid.SetScrollOffset(0, 0);
    grid.Dispatch({.kind = InputKind::Down, .x = 10, .y = 10});
    grid.Dispatch({.kind = InputKind::Up, .x = 10, .y = 10});
    REQUIRE(grid.SortDirection() == GridSortDirection::Ascending);
    Canvas canvas;
    canvas.Begin(600, 100);
    grid.SetScrollOffset(0, 70);
    grid.Render(canvas);
    canvas.End();
    REQUIRE(grid.CellAt(10, 5));
}
TEST_CASE("Scrolling headers preserve pinned columns and virtual row access", "[ui][grid][headers][virtual]")
{
    GridView grid;
    auto model = std::make_shared<CountingModel>();
    Setup(grid, model);
    auto columns = grid.Columns();
    columns[0].pin = GridPin::Left;
    columns[1].width = 600;
    columns[2].pin = GridPin::Right;
    grid.SetColumns(columns);
    auto options = grid.Options();
    options.stickyHeaders = false;
    grid.SetOptions(options);
    grid.Arrange({0, 0, 400, 180});
    grid.SetScrollOffset(100, options.headerHeight + 10000 * options.rowHeight);
    REQUIRE(grid.FirstVisibleRow() == 10000);
    REQUIRE(grid.CellAt(10, 5)->column == "a");
    REQUIRE(grid.CellAt(390 - grid.Style().scrollbarWidth, 5)->column == "c");
    Canvas canvas;
    canvas.Begin(400, 180);
    grid.Render(canvas);
    canvas.End();
    REQUIRE(model->reads <= grid.VisibleRowCount() * 3);
    REQUIRE(model->reads > 0);
    const auto first = grid.FirstVisibleRow();
    REQUIRE(grid.BeginEdit({10001, "a"}) == false);
    REQUIRE(grid.FirstVisibleRow() == first);
}
TEST_CASE("Choice popup focus stays inside the grid edit transaction", "[ui][grid][popup]")
{
    Environment env;
    GridModelColumn column{"choice", GridValueType::Choice};
    column.choices = {{"First", "first"}, {"Second", "second"}};
    auto model = std::make_shared<MemoryGridModel>(std::vector<GridModelColumn>{column});
    model->AddRow({std::string("first")});
    auto grid = std::make_unique<GridView>();
    auto* view = grid.get();
    Setup(*view, model);
    env.host->SetRoot(std::move(grid));
    env.host->Update();
    REQUIRE(view->BeginEdit({1, "choice"}));
    REQUIRE(env.host->Route({.kind = InputKind::KeyDown, .key = LWS::KeyCode::F4}));
    env.host->Update();
    REQUIRE(env.host->HasPopup());
    REQUIRE(env.host->Route({.kind = InputKind::KeyDown, .key = LWS::KeyCode::Down}));
    REQUIRE(view->Editing());
    REQUIRE(model->Value({1, "choice"}) == GridValue{std::string("first")});
    REQUIRE(env.host->Route({.kind = InputKind::KeyDown, .key = LWS::KeyCode::Escape}));
    REQUIRE_FALSE(env.host->HasPopup());
    REQUIRE(view->Editing());
    REQUIRE(env.host->Route({.kind = InputKind::KeyDown, .key = LWS::KeyCode::Enter}));
    REQUIRE_FALSE(view->Editing());
    REQUIRE(model->Value({1, "choice"}) == GridValue{std::string("second")});
    env.host->Update();
    REQUIRE(view->BeginEdit({1, "choice"}));
    REQUIRE(env.host->Route({.kind = InputKind::KeyDown, .key = LWS::KeyCode::F4}));
    env.host->Update();
    auto options = view->Options();
    options.readOnly = true;
    view->SetOptions(options);
    REQUIRE_FALSE(env.host->HasPopup());
    REQUIRE_FALSE(view->Editing());
    REQUIRE(model->Value({1, "choice"}) == GridValue{std::string("second")});
}
TEST_CASE("Demo catalog and compact styles retain all preset purposes", "[ui][grid][demo]")
{
    const auto presets = demo::grid::Presets();
    REQUIRE(presets.size() == 11);
    for (const auto& preset : presets)
    {
        REQUIRE_FALSE(preset.description.empty());
        REQUIRE_FALSE(preset.choice.value.empty());
    }
    REQUIRE(presets[5].options.singleClickEdit);
    REQUIRE_FALSE(presets[0].options.singleClickEdit);
    REQUIRE(presets[0].options.rowHeight == 28);
    REQUIRE(presets[1].options.rowHeight == 24);
    REQUIRE(GridOptions{}.rowHeight == 34);
    REQUIRE_FALSE(GridOptions{}.dynamicResolution);
    REQUIRE(presets[9].randomModel);
    REQUIRE(presets[9].options.dynamicResolution);
    REQUIRE(presets[9].options.lineNumbers);
    REQUIRE_FALSE(presets[9].options.sorting);
    REQUIRE_FALSE(presets[9].options.selectAll);
    REQUIRE_FALSE(presets[9].options.resizing);
    REQUIRE_FALSE(presets[9].options.multipleSelection);
    for (auto preset : {ThemePreset::Dark, ThemePreset::Light, ThemePreset::Warm})
    {
        const auto base = demo::grid::CompactTheme(preset), configuration = demo::grid::ConfigurationTheme(base);
        REQUIRE(base.font.size == 14);
        REQUIRE(configuration.font.size == 12);
        REQUIRE(configuration.checkboxSize == 14);
        REQUIRE(configuration.background == base.background);
        REQUIRE(configuration.foreground == base.foreground);
    }
}

TEST_CASE("Clicking a choice-popup option previews without ending grid editing", "[ui][grid][popup]")
{
    Environment env;
    GridModelColumn column{"choice", GridValueType::Choice};
    column.choices = {{"First", "first"}, {"Second", "second"}};
    auto model = std::make_shared<MemoryGridModel>(std::vector<GridModelColumn>{column});
    model->AddRow({std::string("first")});
    auto grid = std::make_unique<GridView>();
    auto* view = grid.get();
    Setup(*view, model);
    env.host->SetRoot(std::move(grid));
    env.host->Update();
    REQUIRE(view->BeginEdit({1, "choice"}));
    const auto cell = *view->CellBounds({1, "choice"});
    REQUIRE(env.host->Route({.kind = InputKind::KeyDown, .key = LWS::KeyCode::F4}));
    env.host->Update();
    const auto& style = env.host->PopupStyle();
    const float optionHeight = std::max(style.menuRowHeight, style.font.size * 1.4f + 2 * style.menuPadding);
    const float x = cell.x + 10, y = cell.y + cell.height + optionHeight * 1.5f;
    REQUIRE(env.host->Route({.kind = InputKind::Down, .x = x, .y = y}));
    REQUIRE(view->Editing());
    REQUIRE(env.host->Route({.kind = InputKind::Up, .x = x, .y = y}));
    REQUIRE_FALSE(env.host->HasPopup());
    REQUIRE(view->Editing());
    REQUIRE(view->ActiveEditor()->Draft() == GridValue{std::string("second")});
    REQUIRE(model->Value({1, "choice"}) == GridValue{std::string("first")});
    REQUIRE(view->FinishEdit(EditPhase::Cancel));
    REQUIRE(model->Value({1, "choice"}) == GridValue{std::string("first")});
}

TEST_CASE("Numeric cell focus changes and rocker clicks retain the grid transaction", "[ui][grid][numericfocus]")
{
    Environment env;
    auto model = Model();
    auto grid = std::make_unique<GridView>();
    auto* view = grid.get();
    Setup(*view, model);
    env.host->SetRoot(std::move(grid));
    env.host->Update();
    REQUIRE(view->BeginEdit({1, "number"}));
    auto* text = dynamic_cast<TextBox*>(&view->ActiveEditor()->FocusTarget());
    REQUIRE(text);
    text->SetText("40");
    env.host->Route({.kind = InputKind::KeyDown, .key = LWS::KeyCode::End});
    env.host->Route({.kind = InputKind::KeyDown, .key = LWS::KeyCode::Backspace});
    REQUIRE(text->Text() == "4");
    auto* numeric = dynamic_cast<NumericEdit<int64_t>*>(text->Parent());
    REQUIRE(numeric);
    const auto bounds = numeric->Children()[2]->Bounds();
    const float x = bounds.x + bounds.width / 2, y = bounds.y + bounds.height / 2;
    REQUIRE(env.host->Route({.kind = InputKind::Down, .x = x, .y = y}));
    REQUIRE(view->Editing());
    REQUIRE(env.host->Route({.kind = InputKind::Up, .x = x, .y = y}));
    REQUIRE(view->Editing());
    REQUIRE(model->Value({1, "number"}) == GridValue{int64_t(12)});
    REQUIRE(view->FinishEdit(EditPhase::Commit));
    REQUIRE(model->Value({1, "number"}) == GridValue{int64_t(5)});
}

TEST_CASE("Single-click checkbox changes are immediate validated user commits", "[ui][grid][clickvalue]")
{
    auto model = Model();
    GridView grid;
    Setup(grid, model);
    auto options = grid.Options();
    options.singleClickEdit = true;
    options.multipleSelection = true;
    grid.SetOptions(options);
    std::vector<GridValueChange> changes;
    auto changed = grid.OnCellValueChanged.Connect(
        [&](const GridValueChange& change)
        {
            REQUIRE(model->Value(change.cell) == change.newValue);
            changes.push_back(change);
        });
    auto changing = grid.OnCellValueChanging.Connect([&](GridValueChange& change)
                                                     { REQUIRE(model->Value(change.cell) == change.oldValue); });
    const auto click = [&](bool control = false, unsigned count = 1)
    {
        grid.Dispatch({.kind = InputKind::Down, .x = 260, .y = 45, .control = control, .clickCount = count});
        grid.Dispatch({.kind = InputKind::Up, .x = 260, .y = 45, .control = control, .clickCount = count});
    };
    click();
    REQUIRE(model->Value({1, "enabled"}) == GridValue{false});
    REQUIRE_FALSE(grid.Editing());
    REQUIRE(grid.Children().size() == 2);
    REQUIRE(changes.size() == 1);
    REQUIRE(changes[0].origin == GridOrigin::User);
    REQUIRE(changes[0].oldValue == GridValue{true});
    REQUIRE(changes[0].newValue == GridValue{false});
    click(true);
    REQUIRE(model->Value({1, "enabled"}) == GridValue{false});
    REQUIRE(changes.size() == 1);
    auto reject = grid.OnCellValueChanging.Connect([](GridValueChange& change) { change.cancel = true; });
    click();
    REQUIRE(model->Value({1, "enabled"}) == GridValue{false});
    REQUIRE(changes.size() == 1);
    reject.Disconnect();
    click(false, 2);
    REQUIRE(model->Value({1, "enabled"}) == GridValue{true});
    REQUIRE_FALSE(grid.Editing());
    REQUIRE(changes.size() == 2);
    options.readOnly = true;
    grid.SetOptions(options);
    click();
    REQUIRE(changes.size() == 2);
    options.readOnly = false;
    grid.SetOptions(options);
    model->SetCellReadOnly({1, "enabled"}, true);
    click();
    REQUIRE(changes.size() == 2);
    REQUIRE(grid.SetCellValue({1, "enabled"}, false, true));
    REQUIRE(changes.back().origin == GridOrigin::Programmatic);
}
TEST_CASE("Interactive renderer click values use geometry validation and lifecycle events", "[ui][grid][clickvalue]")
{
    GridModelColumn progress{"progress", GridValueType::Decimal};
    progress.decimal = {0., 1., .05};
    auto model = std::make_shared<MemoryGridModel>(std::vector<GridModelColumn>{progress});
    model->AddRow({.1});
    GridView grid;
    Setup(grid, model);
    auto columns = grid.Columns();
    columns[0].clickValue = [](const GridCellPresentation& cell, const Input& input) -> std::optional<GridValue>
    { return GridValue{double(std::clamp((input.x - cell.bounds.x) / cell.bounds.width, 0.f, 1.f))}; };
    grid.SetColumns(columns);
    auto options = grid.Options();
    options.singleClickEdit = true;
    grid.SetOptions(options);
    grid.Arrange({0, 0, 600, 400});
    int begins = 0, commits = 0, cancels = 0, errors = 0, changes = 0;
    auto begin = grid.OnEditBeginning.Connect([&](GridEditRequest&) { ++begins; });
    auto edit = grid.OnEdit.Connect(
        [&](const GridEditEvent& e)
        {
            if (e.phase == EditPhase::Commit)
                ++commits;
            if (e.phase == EditPhase::Cancel)
                ++cancels;
        });
    auto error = grid.OnValidationFailed.Connect([&](const GridValidationEvent&) { ++errors; });
    auto changed = grid.OnCellValueChanged.Connect(
        [&](const GridValueChange& e)
        {
            ++changes;
            REQUIRE(e.origin == GridOrigin::User);
        });
    const auto click = [&]
    {
        grid.Dispatch({.kind = InputKind::Down, .x = 60, .y = 45});
        grid.Dispatch({.kind = InputKind::Up, .x = 60, .y = 45});
    };
    click();
    REQUIRE(model->Value({1, "progress"}) == GridValue{.5});
    REQUIRE_FALSE(grid.Editing());
    REQUIRE(begins == 1);
    REQUIRE(commits == 1);
    REQUIRE(changes == 1);
    click();
    REQUIRE(commits == 2);
    REQUIRE(changes == 1);
    model->validate = [](const GridCell&, const GridValue&) -> GridStatus { return std::unexpected("Rejected"); };
    click();
    REQUIRE(model->Value({1, "progress"}) == GridValue{.5});
    REQUIRE(errors == 1);
    REQUIRE(cancels == 1);
    REQUIRE(changes == 1);
    model->validate = {};
    auto veto = grid.OnEditBeginning.Connect([](GridEditRequest& e) { e.cancel = true; });
    click();
    REQUIRE(commits == 2);
    veto.Disconnect();
    options.singleClickEdit = false;
    grid.SetOptions(options);
    click();
    REQUIRE(commits == 2);
}
TEST_CASE("Unconfigured LWSUI controls and hosts use Light with explicit palettes preserved", "[ui][grid][theme]")
{
    REQUIRE(LWSUI::DefaultThemePreset == ThemePreset::Light);
    const Theme theme;
    REQUIRE(theme.background == LLUtils::Color{uint32_t{0xf4f6f8ff}});
    REQUIRE(theme.surface == LLUtils::Color{uint32_t{0xffffffff}});
    REQUIRE(theme.foreground == LLUtils::Color{uint32_t{0x20252bff}});
    REQUIRE(theme.accent == LLUtils::Color{uint32_t{0x005fccff}});
    REQUIRE(MakeTheme().background == theme.background);
    REQUIRE(MakeTheme(ThemePreset::Dark).background == LLUtils::Color{uint32_t{0x171c25ff}});
    REQUIRE(MakeTheme(ThemePreset::Dark).changed == LLUtils::Color{uint32_t{0xfbbf24ff}});
    REQUIRE(MakeTheme(ThemePreset::Warm).background == LLUtils::Color{uint32_t{0xf4ede2ff}});
    Label label;
    REQUIRE(label.Style().background == theme.background);
    Environment env;
    REQUIRE(env.host->Style().background == theme.background);
    env.host->SetTheme(MakeTheme(ThemePreset::Dark));
    REQUIRE(env.host->Style().background == MakeTheme(ThemePreset::Dark).background);
}

TEST_CASE("Runtime single-click mode tolerates pointer movement within a cell", "[ui][grid][stability]")
{
    Environment env;
    GridModelColumn progress{"progress", GridValueType::Decimal};
    progress.decimal = {0., 1., .05};
    auto model = std::make_shared<MemoryGridModel>(std::vector<GridModelColumn>{{"name"}, progress});
    model->AddRow({std::string("Name"), .1});
    auto root = std::make_unique<DockPanel>();
    auto settings = std::make_unique<StackPanel>();
    auto& single = settings->Emplace<CheckBox>("Single-click edits");
    root->AddDocked(std::move(settings), Dock::Right, 160);
    auto control = std::make_unique<GridView>();
    auto* grid = control.get();
    grid->SetModel(model);
    auto columns = grid->Columns();
    columns[0].width = 120;
    columns[1].width = 400;
    columns[1].clickValue = [](const GridCellPresentation& cell, const Input& input) -> std::optional<GridValue>
    { return GridValue{double((input.x - cell.bounds.x) / cell.bounds.width)}; };
    grid->SetColumns(columns);
    auto options = grid->Options();
    options.multipleSelection = true;
    grid->SetOptions(options);
    root->AddDocked(std::move(control), Dock::Fill);
    env.host->SetRoot(std::move(root));
    env.host->Update();
    auto enabled = single.OnChange.Connect(
        [&](bool value)
        {
            auto o = grid->Options();
            o.singleClickEdit = value;
            grid->SetOptions(o);
        });
    const auto checkbox = single.Bounds();
    env.host->Route({.kind = InputKind::Down, .x = checkbox.x + 10, .y = checkbox.y + 10});
    env.host->Route({.kind = InputKind::Up, .x = checkbox.x + 10, .y = checkbox.y + 10});
    REQUIRE(grid->Options().singleClickEdit);
    const auto bounds = *grid->CellBounds({1, "progress"});
    const float x = bounds.x + bounds.width / 2, y = bounds.y + bounds.height / 2;
    env.host->Route({.kind = InputKind::Down, .x = x, .y = y});
    env.host->Route({.kind = InputKind::Move, .x = x + 7, .y = y});
    env.host->Route({.kind = InputKind::Up, .x = x + 7, .y = y});
    REQUIRE(std::get<double>(model->Value({1, "progress"})) == Catch::Approx(.5175));
    REQUIRE_FALSE(grid->Editing());
    REQUIRE(grid->ScrollOffset().width == 0);
}
TEST_CASE("Single-click mode overrides double-click editor activation", "[ui][grid][stability]")
{
    GridView grid;
    Setup(grid, Model());
    auto options = grid.Options();
    options.singleClickEdit = true;
    grid.SetOptions(options);
    grid.Dispatch({.kind = InputKind::Down, .x = 10, .y = 45, .clickCount = 2});
    REQUIRE_FALSE(grid.Editing());
    grid.Dispatch({.kind = InputKind::Up, .x = 10, .y = 45, .clickCount = 2});
    REQUIRE(grid.Editing());
    grid.FinishEdit(EditPhase::Cancel);
    grid.Dispatch({.kind = InputKind::Down, .x = 10, .y = 45, .clickCount = 3});
    REQUIRE_FALSE(grid.Editing());
    grid.Dispatch({.kind = InputKind::Up, .x = 10, .y = 45, .clickCount = 3});
    REQUIRE(grid.Editing());
}
TEST_CASE("Changing single-click mode discards the old editor transaction", "[ui][grid][stability]")
{
    auto model = Model();
    GridView grid;
    Setup(grid, model);
    REQUIRE(grid.BeginEdit({1, "number"}));
    auto* text = dynamic_cast<TextBox*>(&grid.ActiveEditor()->FocusTarget());
    REQUIRE(text);
    text->SetText("invalid");
    auto options = grid.Options();
    options.singleClickEdit = true;
    grid.SetOptions(options);
    REQUIRE_FALSE(grid.Editing());
    REQUIRE(model->Value({1, "number"}) == GridValue{int64_t(12)});
}

namespace
{
    std::shared_ptr<MemoryGridModel> DragModel(GridView& grid)
    {
        GridModelColumn progress{"progress", GridValueType::Decimal};
        progress.decimal = {0., 1., .05};
        auto model = std::make_shared<MemoryGridModel>(std::vector<GridModelColumn>{progress});
        model->AddRow({.2});
        model->AddRow({.6});
        Setup(grid, model);
        auto columns = grid.Columns();
        columns[0].dragValue = true;
        columns[0].clickValue = [](const GridCellPresentation& cell, const Input& input) -> std::optional<GridValue>
        { return GridValue{double(std::clamp((input.x - cell.bounds.x) / cell.bounds.width, 0.f, 1.f))}; };
        grid.SetColumns(columns);
        grid.Arrange({0, 0, 600, 400});
        return model;
    }
}  // namespace
TEST_CASE("Progress dragging previews the renderer and commits one cell on release", "[ui][grid][valuedrag]")
{
    GridView grid;
    auto model = DragModel(grid);
    auto options = grid.Options();
    options.multipleSelection = true;
    grid.SetOptions(options);
    int begins = 0, commits = 0, changes = 0, previews = 0;
    GridValue painted;
    auto begin = grid.OnEditBeginning.Connect([&](GridEditRequest&) { ++begins; });
    auto edits = grid.OnEdit.Connect(
        [&](const GridEditEvent& e)
        {
            if (e.phase == EditPhase::Preview)
                ++previews;
            if (e.phase == EditPhase::Commit)
                ++commits;
        });
    auto changed = grid.OnCellValueChanged.Connect(
        [&](const GridValueChange& e)
        {
            ++changes;
            REQUIRE(e.oldValue == GridValue{.2});
            REQUIRE(e.origin == GridOrigin::User);
        });
    auto columns = grid.Columns();
    columns[0].render = [&](Canvas&, const GridCellPresentation& cell)
    {
        if (cell.cell.row == 1)
            painted = cell.value;
    };
    grid.SetColumns(columns);
    grid.Arrange({0, 0, 600, 400});
    grid.Dispatch({.kind = InputKind::Down, .x = 30, .y = 45});
    REQUIRE(grid.Editing());
    REQUIRE_FALSE(grid.ActiveEditor());
    grid.Dispatch({.kind = InputKind::Move, .x = 90, .y = 90});
    REQUIRE(model->Value({1, "progress"}) == GridValue{.2});
    REQUIRE(grid.Selection().size() == 1);
    Canvas canvas;
    canvas.Begin(600, 400);
    grid.Render(canvas);
    canvas.End();
    REQUIRE(painted == GridValue{.75});
    grid.Dispatch({.kind = InputKind::Up, .x = 1000, .y = 900});
    REQUIRE(model->Value({1, "progress"}) == GridValue{1.});
    REQUIRE(model->Value({2, "progress"}) == GridValue{.6});
    REQUIRE_FALSE(grid.Editing());
    REQUIRE(begins == 1);
    REQUIRE(commits == 1);
    REQUIRE(changes == 1);
    REQUIRE(previews >= 2);
}
TEST_CASE("Progress drags cancel on Escape capture loss external assignment and read-only", "[ui][grid][valuedrag]")
{
    GridView grid;
    auto model = DragModel(grid);
    int changes = 0, cancels = 0;
    auto changed = grid.OnCellValueChanged.Connect([&](const GridValueChange&) { ++changes; });
    auto edits = grid.OnEdit.Connect(
        [&](const GridEditEvent& e)
        {
            if (e.phase == EditPhase::Cancel)
                ++cancels;
        });
    const auto start = [&]
    {
        grid.Dispatch({.kind = InputKind::Down, .x = 30, .y = 45});
        grid.Dispatch({.kind = InputKind::Move, .x = 100, .y = 45});
        REQUIRE(grid.Editing());
    };
    start();
    grid.Dispatch({.kind = InputKind::KeyDown, .key = LWS::KeyCode::Escape});
    REQUIRE_FALSE(grid.Editing());
    REQUIRE(changes == 0);
    start();
    grid.Dispatch({.kind = InputKind::Cancel});
    REQUIRE_FALSE(grid.Editing());
    REQUIRE(model->Value({1, "progress"}) == GridValue{.2});
    start();
    REQUIRE(grid.SetCellValue({1, "progress"}, .4));
    REQUIRE_FALSE(grid.Editing());
    REQUIRE(model->Value({1, "progress"}) == GridValue{.4});
    start();
    auto options = grid.Options();
    options.readOnly = true;
    grid.SetOptions(options);
    REQUIRE_FALSE(grid.Editing());
    REQUIRE(cancels == 4);
    grid.Dispatch({.kind = InputKind::Down, .x = 30, .y = 45});
    REQUIRE_FALSE(grid.Editing());
    options.readOnly = false;
    options.dragEditing = false;
    grid.SetOptions(options);
    grid.Dispatch({.kind = InputKind::Down, .x = 30, .y = 45});
    REQUIRE_FALSE(grid.Editing());
    REQUIRE(changes == 0);
}
TEST_CASE("Progress drag validation and vetoes preserve accepted model data", "[ui][grid][valuedrag]")
{
    GridView grid;
    auto model = DragModel(grid);
    int errors = 0;
    auto error = grid.OnValidationFailed.Connect([&](const GridValidationEvent&) { ++errors; });
    const auto gesture = [&]
    {
        grid.Dispatch({.kind = InputKind::Down, .x = 30, .y = 45});
        grid.Dispatch({.kind = InputKind::Move, .x = 90, .y = 45});
        grid.Dispatch({.kind = InputKind::Up, .x = 90, .y = 45});
    };
    auto veto = grid.OnCellValueChanging.Connect([](GridValueChange& e) { e.cancel = true; });
    gesture();
    REQUIRE(model->Value({1, "progress"}) == GridValue{.2});
    REQUIRE_FALSE(grid.Editing());
    veto.Disconnect();
    model->validate = [](const GridCell&, const GridValue&) -> GridStatus { return std::unexpected("Rejected"); };
    gesture();
    REQUIRE(model->Value({1, "progress"}) == GridValue{.2});
    REQUIRE(errors == 2);
    REQUIRE_FALSE(grid.Editing());
    auto begin = grid.OnEditBeginning.Connect([](GridEditRequest& e) { e.cancel = true; });
    gesture();
    REQUIRE(errors == 2);
    REQUIRE_FALSE(grid.Editing());
}
TEST_CASE("Progress sorting and pinned geometry remain stable during dragging", "[ui][grid][valuedrag]")
{
    GridView grid;
    auto model = DragModel(grid);
    REQUIRE(grid.Sort("progress", GridSortDirection::Ascending));
    REQUIRE(grid.PinColumn("progress", GridPin::Left));
    grid.Arrange({10, 20, 600, 400});
    grid.Dispatch({.kind = InputKind::Down, .x = 40, .y = 65});
    grid.Dispatch({.kind = InputKind::Move, .x = 130, .y = 65});
    REQUIRE(grid.DisplayRowId(0) == 1);
    grid.Dispatch({.kind = InputKind::Up, .x = 130, .y = 65});
    REQUIRE(grid.DisplayRowId(0) == 2);
    REQUIRE(model->Value({1, "progress"}) == GridValue{1.});
    grid.Dispatch({.kind = InputKind::Down, .x = 40, .y = 65});
    REQUIRE(grid.Editing());
    grid.Arrange({10, 20, 500, 400});
    REQUIRE_FALSE(grid.Editing());
}
TEST_CASE("Grid auto-sizing measures the resolved header font", "[ui][grid][reuse][layout]")
{
    Environment env;
    auto theme = env.host->Style();
    theme.font.size = 12;
    theme.headingFont.size = 28;
    env.host->SetTheme(theme);
    auto grid = std::make_unique<GridView>();
    auto* view = grid.get();
    grid->SetModel(std::make_shared<MemoryGridModel>(std::vector<GridModelColumn>{{"name"}}));
    auto columns = grid->Columns();
    columns[0].header = "A long header measured in its actual heading font";
    columns[0].maximumWidth = 2000;
    grid->SetColumns(columns);
    env.host->SetRoot(std::move(grid));
    env.host->Update();
    REQUIRE(view->AutoSizeColumn("name"));
    const auto expected = env.host->MeasureText(columns[0].header, 100000, false, theme.headingFont).width +
                          2 * view->Appearance().headerPadding + 18;
    REQUIRE(view->Columns()[0].width == Catch::Approx(expected));
    auto appearance = view->Appearance();
    appearance.headerFont = FontSpec{18};
    view->SetAppearance(appearance);
    REQUIRE(view->AutoSizeColumn("name"));
    const auto overridden = env.host->MeasureText(columns[0].header, 100000, false, *appearance.headerFont).width +
                            2 * appearance.headerPadding + 18;
    REQUIRE(view->Columns()[0].width == Catch::Approx(overridden));
    REQUIRE(overridden < expected);
}
TEST_CASE("Grid rechecks read-only state after value-changing callbacks", "[ui][grid][edit][readonly]")
{
    for (const bool modelReadOnly : {false, true})
    {
        Environment env;
        auto model = Model();
        auto grid = std::make_unique<GridView>();
        auto* view = grid.get();
        Setup(*view, model);
        env.host->SetRoot(std::move(grid));
        env.host->Update();
        REQUIRE(view->BeginEdit({1, "number"}));
        static_cast<TextBox&>(view->ActiveEditor()->FocusTarget()).SetText("42");
        int committed = 0, canceled = 0;
        auto edits = view->OnEdit.Connect([&](const GridEditEvent& event)
        {
            committed += event.phase == EditPhase::Commit;
            canceled += event.phase == EditPhase::Cancel;
        });
        auto veto = view->OnCellValueChanging.Connect([&](GridValueChange& change)
        {
            if (change.origin != GridOrigin::User)
                return;
            if (modelReadOnly)
                model->SetCellReadOnly(change.cell, true);
            else
            {
                auto options = view->Options();
                options.readOnly = true;
                view->SetOptions(options);
            }
        });
        REQUIRE_FALSE(view->FinishEdit(EditPhase::Commit));
        REQUIRE_FALSE(view->Editing());
        REQUIRE(model->Value({1, "number"}) == GridValue{int64_t{12}});
        REQUIRE(committed == 0);
        REQUIRE(canceled == 1);
        REQUIRE(view->SetCellValue({1, "number"}, int64_t{42}, true));
        REQUIRE(model->Value({1, "number"}) == GridValue{int64_t{42}});
    }
}

#ifdef LWS_HAS_WIN32_BACKEND
TEST_CASE("Grid Ctrl Insert copies the selection without adding paste", "[ui][grid][copy][clipboard]")
{
    Environment env;
    auto grid = std::make_unique<GridView>();
    auto* view = grid.get();
    Setup(*view, Model());
    env.host->SetRoot(std::move(grid)); env.host->Update();
    REQUIRE(view->SelectCell({1, "name"}));
    REQUIRE(env.host->Focus(view));
    REQUIRE(env.host->Route({.kind = InputKind::KeyDown, .key = LWS::KeyCode::Insert, .control = true}));
    std::string copied;
    LWS::RequestClipboardText(*env.window, [&](auto result, auto value)
    { if (result == LWS::ClipboardResult::Success) copied = std::move(value); });
    if (copied.empty()) SKIP("Clipboard access unavailable");
    REQUIRE(copied == view->CopyText());
    REQUIRE_FALSE(env.host->Route({.kind = InputKind::KeyDown, .key = LWS::KeyCode::Insert, .shift = true}));
}
#endif
#endif
