#include <LWSUI/Composites.hpp>
#include <LWSUI/UIHost.hpp>
#include "MenuRows.hpp"
#include <algorithm>
namespace LWSUI
{
    namespace
    {
        class MenuOption : public Button
        {
          public:

            MenuOption(std::string label, bool active) : Button(std::move(label)), active_(active) {}
            void SetActive(bool active)
            {
                active_ = active;
                Invalidate();
            }

          protected:

            Size OnMeasure(Size available) override
            {
                return {available.width, std::max(Style().menuRowHeight, Font().size * 1.4f + 2 * Style().menuPadding)};
            }
            void OnRender(Canvas& canvas) override
            {
                auto b = Bounds();
                const float p = Style().menuPadding;
                canvas.Fill(b.x, b.y, b.width, b.height,
                            Hovered() ? Style().selection
                            : active_ ? Style().selectedOption
                                      : Style().line);
                if (active_)
                    canvas.Fill(b.x, b.y, Style().focusWidth, b.height, Style().accent);
                DrawText(canvas, text_, b.x + p, b.y + p, b.width - 2 * p, b.height - 2 * p, Foreground());
            }

          private:

            bool active_;
        };
        class ChoiceMenu : public ScrollView
        {
          public:

            explicit ChoiceMenu(size_t selected, std::function<void(size_t)> changed)
                : selected_(selected), changed_(std::move(changed))
            {
            }

          protected:

            bool OnInput(const Input& input) override
            {
                using Key = LWS::KeyCode;
                auto* list = dynamic_cast<StackPanel*>(Content());
                if (input.kind != InputKind::KeyDown || !list || list->Children().empty())
                    return ScrollView::OnInput(input);
                if (input.key == Key::Down || input.key == Key::Right || input.key == Key::Up || input.key == Key::Left)
                {
                    bool forward = input.key == Key::Down || input.key == Key::Right;
                    selected_ = *internal::NextMenuRow(list->Children().size(), selected_, forward ? 1 : -1,
                                                       [](size_t) { return true; });
                    for (size_t i = 0; i < list->Children().size(); ++i)
                        static_cast<MenuOption&>(*list->Children()[i]).SetActive(i == selected_);
                    changed_(selected_);
                    auto& option = *list->Children()[selected_];
                    internal::RevealMenuRow(*this, list->Children(), selected_, list->spacing);
                    if (Host())
                        Host()->Focus(&option);
                    return true;
                }
                if (input.key == Key::Enter || input.key == Key::Space)
                    return list->Children()[selected_]->Dispatch(input);
                return ScrollView::OnInput(input);
            }

          private:

            size_t selected_;
            std::function<void(size_t)> changed_;
        };
    }
    RadioGroup::RadioGroup(std::vector<Choice> choices) : choices_(std::move(choices))
    {
        spacing = 0;
        for (const auto& choice : choices_)
        {
            auto& button = Emplace<RadioButton>(choice.label);
            buttons_.push_back(&button);
            connections_.push_back(button.OnChange.Connect(
                [this, value = choice.value](bool)
                {
                    SetValue(value);
                    OnChange.Raise(value_);
                }));
        }
    }
    void RadioGroup::SetValue(std::string value)
    {
        value_ = std::move(value);
        for (size_t i = 0; i < buttons_.size(); ++i)
            buttons_[i]->SetValue(choices_[i].value == value_);
    }
    bool RadioGroup::OnInput(const Input& input)
    {
        if (input.kind != InputKind::KeyDown || choices_.empty() ||
            (input.key != LWS::KeyCode::Left && input.key != LWS::KeyCode::Right))
            return false;
        size_t selected = 0;
        for (size_t i = 0; i < choices_.size(); ++i)
            if (choices_[i].value == value_)
                selected = i;
        selected = (selected + (input.key == LWS::KeyCode::Left ? choices_.size() - 1 : 1)) % choices_.size();
        SetValue(choices_[selected].value);
        OnChange.Raise(value_);
        return true;
    }
    ComboBox::ComboBox(std::vector<Choice> choices) : choices_(std::move(choices))
    {
        if (!choices_.empty())
            SetValue(choices_[0].value);
    }
    void ComboBox::SetValue(std::string value)
    {
        value_ = std::move(value);
        for (const auto& c : choices_)
            if (c.value == value_)
                SetText(c.label);
    }
    void ComboBox::Activate()
    {
        if (!Host())
            return;
        auto list = std::make_unique<StackPanel>();
        list->spacing = Host()->PopupStyle().spacing;
        choicesConnections_.clear();
        for (const auto& choice : choices_)
        {
            auto& button = list->Emplace<MenuOption>(choice.label, choice.value == value_);
            choicesConnections_.push_back(button.OnClick.Connect(
                [this, value = choice.value]
                {
                    SetValue(value);
                    OnChange.Raise(value_);
                    Host()->ClosePopup();
                }));
        }
        size_t selected = 0;
        for (size_t i = 0; i < choices_.size(); ++i)
            if (choices_[i].value == value_)
                selected = i;
        auto scroll = std::make_unique<ChoiceMenu>(selected,
                                                   [this](size_t index)
                                                   {
                                                       SetValue(choices_[index].value);
                                                       OnChange.Raise(value_);
                                                   });
        scroll->overlayScrollBar = true;
        scroll->SetContent(std::move(list));
        auto b = Bounds();
        const auto& popupStyle = Host()->PopupStyle();
        Host()->OpenPopup(*this, std::move(scroll),
                          {b.x, b.y + b.height, b.width,
                           std::min(popupStyle.menuMaxRows, float(choices_.size())) *
                               std::max(popupStyle.menuRowHeight,
                                        popupStyle.font.size * 1.4f + 2 * popupStyle.menuPadding)});
    }
    void ComboBox::OnRender(Canvas& canvas)
    {
        auto b = Bounds();
        const float p = Style().textPadding;
        canvas.Fill(b.x, b.y, b.width, b.height, Style().surface);
        DrawText(canvas, text_, b.x + p, b.y + p, b.width - Style().checkboxSize - 2 * p, b.height - 2 * p,
                 Foreground());
        canvas.Text("v", b.x + b.width - Style().checkboxSize, b.y + p, Style().checkboxSize, b.height - 2 * p,
                    Style().accent, Font());
    }
    bool ComboBox::OnInput(const Input& e)
    {
        if (e.kind == InputKind::KeyDown && !choices_.empty() &&
            (e.key == LWS::KeyCode::Left || e.key == LWS::KeyCode::Right))
        {
            size_t selected = 0;
            for (size_t i = 0; i < choices_.size(); ++i)
                if (choices_[i].value == value_)
                    selected = i;
            selected = (selected + (e.key == LWS::KeyCode::Left ? choices_.size() - 1 : 1)) % choices_.size();
            SetValue(choices_[selected].value);
            OnChange.Raise(value_);
            return true;
        }
        if (e.kind == InputKind::KeyDown && e.key == LWS::KeyCode::F4)
        {
            Activate();
            return true;
        }
        return Button::OnInput(e);
    }
}  // namespace LWSUI
