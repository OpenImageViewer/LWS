#pragma once
#include <LWSUI/Canvas.hpp>
#include <LWSUI/Theme.hpp>
#include <LWSUI/Event.hpp>
#include <LWS/KeyCode.hpp>
#include <LWS/MouseButton.hpp>
#include <optional>
#include <functional>
namespace LWSUI
{
    struct Size
    {
        float width = 0, height = 0;
    };
    struct Rect
    {
        float x = 0, y = 0, width = 0, height = 0;
        bool Contains(float px, float py) const { return px >= x && py >= y && px < x + width && py < y + height; }
    };
    enum class EditPhase
    {
        Preview,
        Commit,
        Cancel
    };
    enum class InputKind
    {
        Move,
        Down,
        Up,
        Wheel,
        KeyDown,
        KeyUp,
        Text,
        Focus,
        Blur,
        Cancel
    };
    struct Input
    {
        InputKind kind;
        float x = 0, y = 0, wheel = 0;
        LWS::KeyCode key = LWS::KeyCode::Unknown;
        std::string text;
        bool control = false, shift = false, repeat = false, alt = false;
        LWS::MouseButton button = LWS::MouseButton::Left;
    };
    class UIHost;
    class Container;
    class Control;
    // Handles do not own controls. Detach invalidates all existing handles.
    class ControlHandle
    {
      public:

        Control* Get() const
        {
            auto p = lifetime_.lock();
            return p ? p->control : nullptr;
        }
        explicit operator bool() const { return Get() != nullptr; }

      private:

        friend class Control;
        struct Lifetime
        {
            Control* control;
        };
        std::weak_ptr<Lifetime> lifetime_;
    };
    struct ContextMenuRequest
    {
        ControlHandle target;
        float x = 0, y = 0;
        bool keyboard = false;
    };
    struct ContextMenuItem
    {
        std::string label, shortcut;
        std::function<void(Control&)> action;
        bool enabled = true;
        std::optional<bool> checked;
        bool separator = false;
        static ContextMenuItem Separator()
        {
            ContextMenuItem item;
            item.separator = true;
            return item;
        }
    };
    using ContextMenuProvider = std::function<std::vector<ContextMenuItem>(Control&, const ContextMenuRequest&)>;
    // UI-thread confined. All sizes and input positions are logical pixels at 96 DPI.
    // Render never changes values or layout. Setters invalidate the next host update.
    // Callbacks may edit values, but must use UIHost::Post for structural tree changes;
    // they must not synchronously destroy controls or the host during dispatch.
    class Control
    {
      public:

        Control();
        virtual ~Control();
        Control(const Control&) = delete;
        Control& operator=(const Control&) = delete;
        Size Measure(Size available);
        void Arrange(Rect bounds);
        void Render(Canvas& canvas);
        bool Dispatch(const Input& input);
        bool PreviewInput(const Input& input) { return Visible() && Enabled() && OnPreviewInput(input); }
        virtual Control* HitTest(float x, float y);
        const Rect& Bounds() const { return bounds_; }
        Size DesiredSize() const { return desired_; }
        ControlHandle Handle() const;
        UIHost* Host() const { return host_; }
        Container* Parent() const { return parent_; }
        const Theme& Style() const;
        // Section styles inherit through the control tree; popup roots receive their
        // own style from the host. No settings IDs or JSON enter reusable controls.
        void SetStyle(Theme style)
        {
            style_ = std::move(style);
            Invalidate(true);
        }
        enum class FontRole
        {
            Normal,
            Small,
            Heading,
            Title,
            Glyph
        };
        FontRole fontRole = FontRole::Normal;
        const FontSpec& Font() const;
        void SetHighlightPattern(std::string pattern)
        {
            highlightPattern_ = std::move(pattern);
            Invalidate();
        }
        std::string_view HighlightPattern() const;
        std::vector<TextSpan> HighlightSpans(std::string_view text) const;
        void DrawText(Canvas&, std::string_view, float x, float y, float width, float height, LLUtils::Color,
                      bool wrap = false, bool highlight = true) const;
        bool Visible() const { return visible_; }
        bool Enabled() const { return enabled_; }
        bool Hovered() const { return hovered_; }
        void SetVisible(bool value);
        void SetEnabled(bool value);
        void SetForeground(std::optional<LLUtils::Color> color)
        {
            if (foreground_ == color)
                return;
            foreground_ = color;
            Invalidate();
        }
        LLUtils::Color Foreground() const;
        void Invalidate(bool layout = false);
        // Action controls such as Cancel may discard the previous focused draft.
        EditPhase focusLossPhase = EditPhase::Commit;
        virtual bool Focusable() const { return false; }
        virtual bool Finish(EditPhase) { return true; }
        LWSUI::Event<void()> OnHover;
        // Replaces the default provider. An empty result suppresses ancestor menus.
        void SetContextMenuProvider(ContextMenuProvider provider) { contextMenuProvider_ = std::move(provider); }

      protected:

        virtual Size OnMeasure(Size available);
        virtual void OnArrange() {}
        virtual void OnRender(Canvas&) {}
        virtual bool OnInput(const Input&) { return false; }
        virtual bool OnPreviewInput(const Input&) { return false; }
        virtual void OnDetach() {}
        void Capture();
        void ReleaseCapture();

      private:

        friend class Container;
        friend class UIHost;
        void Attach(UIHost* host, Container* parent);
        ContextMenuProvider contextMenuProvider_;
        Rect bounds_;
        Size desired_;
        bool visible_ = true, enabled_ = true, hovered_ = false;
        std::optional<LLUtils::Color> foreground_;
        std::optional<Theme> style_;
        std::optional<std::string> highlightPattern_;
        UIHost* host_ = nullptr;
        Container* parent_ = nullptr;
        std::shared_ptr<ControlHandle::Lifetime> lifetime_;
    };
    class Container : public Control
    {
      public:

        Control& Add(std::unique_ptr<Control> child);
        template <class T, class... Args>
        T& Emplace(Args&&... args)
        {
            auto child = std::make_unique<T>(std::forward<Args>(args)...);
            auto& reference = *child;
            Add(std::move(child));
            return reference;
        }
        std::unique_ptr<Control> Remove(Control& child);
        void Clear();
        const auto& Children() const { return children_; }
        Control* HitTest(float x, float y) override;
        bool Finish(EditPhase phase) override;

      protected:

        void OnRender(Canvas&) override;

      private:

        friend class Control;
        std::unique_ptr<Control> RemoveAt(size_t index);
        std::vector<std::unique_ptr<Control>> children_;
    };
}  // namespace LWSUI
