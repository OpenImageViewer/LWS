#include "ColorPanel.hpp"
#include <LWSUI/Composites.hpp>
#include <LWSUI/UIHost.hpp>
#include <LWS/FileDialog.hpp>
#include <filesystem>
#include <cstdio>
namespace LWSUI
{
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
                    selected_ = forward ? (selected_ + 1) % list->Children().size()
                                        : (selected_ + list->Children().size() - 1) % list->Children().size();
                    for (size_t i = 0; i < list->Children().size(); ++i)
                        static_cast<MenuOption&>(*list->Children()[i]).SetActive(i == selected_);
                    changed_(selected_);
                    auto& option = *list->Children()[selected_];
                    float position = option.Bounds().y - Content()->Bounds().y;
                    if (position < Offset())
                        SetOffset(position);
                    else if (position + option.Bounds().height > Offset() + Bounds().height)
                        SetOffset(position + option.Bounds().height - Bounds().height);
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
    }  // namespace
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
    ColorPicker::ColorPicker()
    {
        columns = {-1, Style().swatchWidth};
        spacing = Style().swatchGap;
        text_ = &Emplace<TextBox>();
        swatch_ = &Emplace<ColorSwatch>();
        open_ = swatch_->OnClick.Connect([this] { Open(); });
        textConnection_ = text_->OnEdit.Connect(
            [this](const std::string& text, EditPhase phase)
            {
                const auto color = internal::ParseColor(text);
                if (!color)
                {
                    text_->SetValidation("Use #RRGGBB or #RRGGBBAA");
                    return;
                }
                value_ = *color;
                swatch_->SetValue(value_);
                OnEdit.Raise(value_, phase);
            });
        Refresh();
    }
    void ColorPicker::SetValue(LLUtils::Color value)
    {
        value_ = value;
        Refresh();
    }
    void ColorPicker::Refresh()
    {
        swatch_->SetValue(value_);
        text_->SetText(internal::ColorText(value_));
    }
    void ColorPicker::Open()
    {
        if (!Host() || !text_->Finish(EditPhase::Commit))
            return;
        start_ = value_;
        const auto& popupStyle = Host()->PopupStyle();
        auto panel = std::make_unique<internal::ColorPanel>(
            value_,
            [handle = Handle()](LLUtils::Color color)
            {
                if (auto* control = handle.Get())
                {
                    auto& picker = static_cast<ColorPicker&>(*control);
                    picker.SetValue(color);
                    picker.OnEdit.Raise(color, EditPhase::Preview);
                }
            },
            popupStyle);
        auto* initialFocus = &panel->InitialFocus();
        auto size = Host()->Window().GetClientAreaMetrics().logical;
        const auto fitted = panel->Fit(*Host(), {std::min(float(size.x), popupStyle.popupWidth), float(size.y)});
        if (!fitted)
        {
            Host()->ReportError("The color picker needs a larger window or smaller popup fonts.");
            return;
        }
        if (Host()->OpenPopup(
                *this, std::move(panel),
                {(float(size.x) - fitted->width) / 2, (float(size.y) - fitted->height) / 2, fitted->width,
                 fitted->height},
                [this](EditPhase phase)
                {
                    if (phase == EditPhase::Cancel)
                        SetValue(start_);
                    OnEdit.Raise(value_, phase);
                },
                true))
            Host()->Focus(initialFocus);
    }
    namespace
    {
        std::string FilePathError(std::string_view text)
        {
            if (text.empty())
                return {};
            if (text.find('\0') != text.npos)
                return "Choose an existing file.";
            try
            {
                const std::filesystem::path path(
                    std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
                std::error_code error;
                const auto status = std::filesystem::status(path, error);
                if (status.type() == std::filesystem::file_type::not_found)
                    return "File does not exist.";
                if (error)
                    return "Cannot check this file.";
                return std::filesystem::is_regular_file(status) ? std::string{} : "Choose an existing file.";
            }
            catch (const std::filesystem::filesystem_error&)
            {
                return "Invalid file path.";
            }
        }
        class FilePathTextBox final : public TextBox
        {
          public:

            explicit FilePathTextBox(Button& browse) : browse_(browse)
            {
                edit_ = OnEdit.Connect([this](const auto&, EditPhase phase) { edited_ = phase == EditPhase::Preview; });
            }
            void SetValue(std::string value)
            {
                edited_ = false;
                browse_.focusLossPhase = EditPhase::Commit;
                SetText(std::move(value));
            }
            bool ValidateDraft()
            {
                auto error = FilePathError(Text());
                const bool valid = error.empty();
                SetValidation(std::move(error));
                // Browse must remain available to replace an invalid typed draft.
                browse_.focusLossPhase = valid ? EditPhase::Commit : EditPhase::Cancel;
                return valid;
            }
            bool Finish(EditPhase phase) override
            {
                // Loaded values are displayable even if their files have since disappeared.
                if (phase == EditPhase::Commit && edited_ && !ValidateDraft())
                    return false;
                const bool result = TextBox::Finish(phase);
                if (phase == EditPhase::Cancel)
                    browse_.focusLossPhase = EditPhase::Commit;
                return result;
            }

          private:

            Button& browse_;
            bool edited_ = false;
            Event<void(const std::string&, EditPhase)>::Connection edit_;
        };
    }  // namespace
    FilePicker::FilePicker()
    {
        columns = {-1, Style().browseWidth};
        spacing = 0;
        auto button = std::make_unique<Button>("...");
        button->SetHighlightPattern("");
        auto& field = Emplace<FilePathTextBox>(*button);
        text_ = &field;
        browse_ = button->OnClick.Connect([this] { Browse(); });
        Add(std::move(button));
        edit_ = text_->OnEdit.Connect(
            [this, &field](const auto& value, EditPhase phase)
            {
                if (phase != EditPhase::Preview || field.ValidateDraft())
                    OnEdit.Raise(value, phase);
            });
    }
    void FilePicker::SetValue(std::string value)
    {
        static_cast<FilePathTextBox*>(text_)->SetValue(std::move(value));
    }
    bool FilePicker::OnInput(const Input& e)
    {
        if (e.kind == InputKind::KeyDown && e.key == LWS::KeyCode::F4)
        {
            Browse();
            return true;
        }
        return false;
    }
    void FilePicker::Browse()
    {
        if (!Host())
            return;
        if (!text_->Finish(EditPhase::Commit))
            text_->Finish(EditPhase::Cancel);
        auto self = Handle();  // The native dialog runs a nested event loop.
        LWS::string_type path;
        auto result = LWS::FileDialog::Show(LWS::FileDialogType::OpenFile, {}, NativeText("Choose file"),
                                            Host()->Window(), {}, 0, NativeText(Value()), path);
        if (!self)
            return;
        if (result == LWS::FileDialogResult::Success)
        {
            auto utf8 = std::filesystem::path(path).u8string();
            std::string chosen(reinterpret_cast<const char*>(utf8.data()), utf8.size());
            if (auto error = FilePathError(chosen); !error.empty())
            {
                Host()->ReportError(std::move(error));
                return;
            }
            SetValue(std::move(chosen));
            OnEdit.Raise(Value(), EditPhase::Commit);
        }
        else if (result != LWS::FileDialogResult::UserCanceled)
            Host()->ReportError("Native file dialog failed");
    }
}  // namespace LWSUI
