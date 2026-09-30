#pragma once
#include <LWSUI/Control.hpp>
#include <LWSUI/Menu.hpp>
#include <LWS/Window.hpp>
namespace LWSUI
{
    namespace internal
    {
        class MenuSession;
        struct MenuSessionAccess;
    }
    // Window must outlive host. The host owns root and popup trees; focus/capture
    // and popup owners are checked non-owning handles. Callbacks run on the UI thread.
    class UIHost
    {
      public:

        explicit UIHost(LWS::Window& window, Theme theme = {});
        ~UIHost();
        void SetRoot(std::unique_ptr<Control> root);
        Control* Root() const { return root_.get(); }
        LWS::Window& Window() const { return window_; }
        const Theme& Style() const { return theme_; }
        void SetTheme(Theme theme)
        {
            CloseMainMenu();
            theme_ = std::move(theme);
            Invalidate(true);
        }
        void Invalidate(bool layout = false);
        void Update();
        // Opt-in synchronous rendering on size notifications; never runs deferred application work.
        void SetRedrawOnResize(bool enabled) { redrawOnResize_ = enabled; }
        bool RedrawOnResize() const { return redrawOnResize_; }
        Size MeasureText(std::string_view text, float width, bool wrap, const FontSpec& font);
        size_t HitTestText(std::string_view text, float x, const FontSpec& font);
        float MeasureCaret(std::string_view text, size_t byteOffset, const FontSpec& font);
        std::vector<TextLine> MeasureTextLines(std::string_view text, float width, const FontSpec& font);
        TextPosition HitTestText(std::string_view text, float x, float y, float width, const FontSpec& font);
        TextCaret MeasureTextCaret(std::string_view text, TextPosition position, float width, const FontSpec& font);
        const Theme& PopupStyle() const { return popupStyle_ ? *popupStyle_ : theme_; }
        void SetPopupStyle(Theme style) { popupStyle_ = std::move(style); }
        bool Route(Input input);
        bool Focus(Control* control);
        bool IsFocused(const Control& control) const { return focus_.Get() == &control; }
        bool HasFocusWithin(const Control& control) const;
        bool HasHoverWithin(const Control& control) const;
        // Replacement can fail if the current popup rejects committed closure.
        bool OpenPopup(Control& owner, std::unique_ptr<Control> popup, Rect bounds,
                       std::function<void(EditPhase)> closed = {}, bool modal = false);
        void ClosePopup(EditPhase phase = EditPhase::Commit);
        bool HasPopupWithin(const Control& control) const;
        bool ShowContextMenu(Control& owner, const ContextMenuRequest&, std::vector<ContextMenuItem> items);
        void CloseContextMenu();
        bool HasContextMenu() const { return contextMenu_ != nullptr; }
        bool HasPopup() const { return popup_ != nullptr; }
        bool Finish(EditPhase phase);
        // Main menu. Docked bars sit in the window and shrink the root rect; a floating bar
        // is code-positioned and overlays the root. Dropdowns are popup windows owned by the
        // session. Every entry point closes any open menu first.
        void SetMainMenu(std::unique_ptr<MenuBar> menu, MenuDock dock = MenuDock::Top);
        MenuBar* MainMenu() const { return menuBar_.get(); }
        bool HasMainMenu() const { return menuBar_ != nullptr; }
        void SetMainMenuDock(MenuDock dock);
        MenuDock MainMenuDock() const { return dock_; }
        /// Floating-bar origin, clamped to the client area; ignored for docked bars.
        void SetMainMenuPosition(float x, float y);
        void ClearMainMenu();
        bool HasOpenMenu() const;
        void CloseMainMenu();
        // Structural changes requested by control callbacks must be posted, including
        // Remove/Clear/SetRoot and replacing definitions of an attached SettingsView.
        // Post on a surviving ancestor, not the child being removed. The callback runs
        // before the next traversal and is discarded if its owner has detached.
        // This explicit rule avoids making every callback support synchronous destruction.
        // Host-managed popup closing is safe during dispatch: it retires the popup tree.
        void Post(ControlHandle owner, std::function<void(Control&)> callback);
        void ReportError(std::string message) { OnError.Raise(message); }
        LWSUI::Event<void(const std::string&)> OnError;

      private:

        friend class Control;
        friend class internal::MenuSession;
        friend struct internal::MenuSessionAccess;
        void Capture(Control* control);
        void ReleaseCapture(Control* control);
        void Detached(Control& control);
        void ScheduleUpdate();
        void UpdateCore(bool processDeferred);
        bool RenderFrame();
        bool Event(const LWS::AnyEvent&);
        bool InPopup(Control* control) const;
        bool RequestContextMenu(Input input, bool keyboard);
        bool RouteContextMenu(const Input&);
        bool ContextMenuOwnerValid() const;
        void Hover(Control* control);
        /// Panel style: popup style, or the theme with the popup surface as background.
        Theme MenuPanelStyle() const;
        void LayoutMainMenu(float width, float height);
        LWS::Window& window_;
        Theme theme_;
        std::optional<Theme> popupStyle_;
        Canvas canvas_;
        std::unique_ptr<Control> root_, popup_, contextMenu_;
        // Retired popups survive until the next update, so a popup may close itself.
        std::vector<std::unique_ptr<Control>> retired_;
        ControlHandle focus_, capture_, popupOwner_, hover_, pointerHover_;
        std::function<void(EditPhase)> popupClosed_;
        Rect popupBounds_, contextMenuBounds_;
        ControlHandle contextMenuOwner_;
        bool suppressPointerRelease_ = false;
        bool popupModal_ = false;
        std::vector<std::pair<ControlHandle, std::function<void(Control&)>>> deferred_;
        std::unique_ptr<internal::MenuSession> menuSession_;
        std::unique_ptr<MenuBar> menuBar_;
        MenuDock dock_ = MenuDock::Top;
        float floatingX_ = 0, floatingY_ = 0;
        Rect barBounds_, rootBounds_;
        bool layoutDirty_ = true, paintDirty_ = true, updating_ = false;
        bool redrawOnResize_ = false, resizePending_ = false;
        LWS::EventConnection listener_;
        bool updatePending_ = false;
        std::shared_ptr<UIHost*> lifetime_ = std::make_shared<UIHost*>(this);
    };
    LWS::string_type NativeText(std::string_view text);
}  // namespace LWSUI
