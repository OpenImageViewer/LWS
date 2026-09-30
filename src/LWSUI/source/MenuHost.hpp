#pragma once
#include <LWSUI/Containers.hpp>
#include <LWSUI/Menu.hpp>
#include <LWS/Timer.hpp>
#include <LWS/Window.hpp>
#include <memory>
#include <optional>
#include <utility>
#include <vector>
namespace LWSUI
{
    class UIHost;
}
namespace LWSUI::internal
{
    /// Maps a window event to a control input; nullopt for non-input events.
    std::optional<Input> InputFromEvent(const LWS::AnyEvent& event, LWS::PlatformContext& platform);
    /// One dropdown rendered into its own popup window client space {0,0,w,h}.
    class DropdownPanel final : public ScrollView
    {
        class Row final : public Control
        {
          public:

            Row(const MenuItem& item, bool checks, bool arrows) : item_(item), checks_(checks), arrows_(arrows) {}
            bool active = false;

          protected:

            Size OnMeasure(Size available) override;
            void OnRender(Canvas&) override;

          private:

            float CheckWidth() const;
            float ArrowWidth() const;
            float HintWidth(float width) const;
            const MenuItem& item_;
            bool checks_, arrows_;
        };

      public:

        explicit DropdownPanel(std::vector<MenuItem> items);
        float NaturalWidth(UIHost& host) const;
        size_t Count() const { return items_.size(); }
        const MenuItem& Item(size_t index) const { return items_[index]; }
        std::optional<size_t> Active() const { return active_; }
        bool Selectable(size_t index) const;
        std::optional<size_t> First() const;
        std::optional<size_t> Last() const;
        std::optional<size_t> Next(size_t from, int step) const;
        /// Mnemonic match at or after `from`, wrapping; nullopt when no row matches.
        std::optional<size_t> Mnemonic(char key, size_t from) const;
        /// Row rectangle in panel coordinates.
        Rect RowRect(size_t index) const { return rows_[index]->Bounds(); }
        void Select(std::optional<size_t> index, bool reveal = true);
        /// Raised on hover and keyboard selection changes; row activation is reported separately.
        LWSUI::Event<void(std::optional<size_t>)> OnSelect;
        LWSUI::Event<void(size_t)> OnActivate;

      protected:

        void OnRender(Canvas&) override;
        bool OnInput(const Input&) override;

      private:

        std::vector<MenuItem> items_;
        std::vector<Row*> rows_;
        std::optional<size_t> active_, pressed_;
        bool checks_ = false, arrows_ = false;
    };
    /// Open menu state: bar highlight, the chain of popup dropdown windows and their presentation.
    /// One instance lives inside UIHost for the host's lifetime.
    class MenuSession final
    {
      public:

        explicit MenuSession(UIHost& host);
        ~MenuSession();
        /// Main-window input hook; true = consumed.
        bool OnInput(const Input& input);
        /// Clear passive bar hover when the pointer leaves the owner window.
        void LeaveBar();
        void FocusLost();
        /// Popup-window listener hook; true = handled.
        bool OnLevelEvent(LWS::Window* window, const LWS::AnyEvent& event);
        /// Updates popup hosts and reaps levels retired during input dispatch.
        void Update();
        void Close();

        bool Active() const { return active_; }
        bool HasLevels() const { return !levels_.empty(); }
        size_t LevelCount() const { return levels_.size(); }
        DropdownPanel* Panel(size_t level) const;
        LWS::Window* LevelWindow(size_t level) const;
        /// Panel-local input as delivered by a level window; also the seam tests drive.
        bool PanelInput(size_t level, const Input& input);

      private:

        struct Level
        {
            std::unique_ptr<LWS::Window> window;
            std::unique_ptr<UIHost> host;
            DropdownPanel* panel = nullptr;
            LWS::EventConnection listener;
            LWSUI::Event<void(std::optional<size_t>)>::Connection selection;
            LWSUI::Event<void(size_t)>::Connection activation;
            LWSUI::Event<void(const std::string&)>::Connection error;
        };
        std::optional<Size> InstallPanel(Level& level, const std::vector<MenuItem>& items, bool first);
        bool KeyDown(const Input& input);
        bool PointerInput(const Input& input);
        bool WindowInput(const Input& input);
        void ActivateWindowButton(MenuWindowAction action);
        bool Mnemonic(char key);
        void Activate();
        void MoveHot(int step);
        void OpenTopMenu(size_t barIndex, bool first);
        bool ReplaceTopMenu(size_t barIndex, bool first);
        void SwitchTopMenu(int step);
        void OpenSubmenu(size_t level, size_t row);
        void ActivateRow(size_t level, size_t row);
        void OnRowSelected(Level* level, std::optional<size_t> row);
        void OnRowActivated(Level* level, size_t row);
        void CloseLevel(size_t level);
        void CloseDeepLevels(size_t keep);
        void SetHot(int index);
        void ArmHover(size_t level, size_t row);
        void CancelHover();
        std::optional<size_t> FindLevel(LWS::Window* window) const;
        size_t IndexOf(const Level* level) const;
        bool CanReuseWindows() const;
        Rect TopMenuAnchor(size_t barIndex) const;
        LWS::PopupGravity TopMenuGravity() const;
        bool CreateLevel(size_t parentLevel, std::vector<MenuItem> items, LWS::Point anchor,
                         LWS::PopupGravity gravity, bool first);

        static constexpr size_t NoLevel = static_cast<size_t>(-1);
        UIHost& host_;
        std::vector<std::unique_ptr<Level>> levels_;
        std::vector<std::unique_ptr<Level>> retired_;
        std::unique_ptr<LWS::HighPrecisionTimer> hoverTimer_;
        std::optional<std::pair<size_t, size_t>> pendingSubmenu_;
        int hot_ = -1;
        bool active_ = false, menuUsed_ = false;
        Orientation orientationAtOpen_ = Orientation::Horizontal;
    };
    /// Test seam onto the session instance a host owns.
    struct MenuSessionAccess
    {
        static MenuSession& Get(UIHost& host);
        static Rect WindowButtonBounds(MenuBar& bar, MenuWindowAction action) { return bar.WindowButtonRect(action); }
        static Rect DragBounds(MenuBar& bar) { return bar.WindowDragArea(); }
        static bool HitDrag(MenuBar& bar, float x, float y) { return bar.HitWindowDrag(x, y); }
        static bool WindowButtonEnabled(MenuBar& bar, MenuWindowAction action)
        {
            return bar.WindowButtonEnabled(action);
        }
    };
}  // namespace LWSUI::internal
