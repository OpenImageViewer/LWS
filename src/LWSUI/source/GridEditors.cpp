#include "GridInternals.hpp"
#include <LWSUI/Composites.hpp>
namespace LWSUI::internal
{
    class BuiltinGridEditor final : public GridCellEditor
    {
      public:

        BuiltinGridEditor(const GridModelColumn& column, const GridValue& value) : column_(column)
        {
            if (column.type == GridValueType::Integer)
            {
                auto& edit = Emplace<NumericEdit<int64_t>>(column.integer);
                edit.SetValue(std::get<int64_t>(value));
                field_ = &edit;
                integerField_ = &edit;
                integer_ = edit.OnEdit.Connect([this](int64_t, EditPhase p) { NumericEdited(p); });
            }
            else if (column.type == GridValueType::Decimal)
            {
                auto& edit = Emplace<NumericEdit<double>>(column.decimal);
                edit.SetValue(std::get<double>(value));
                field_ = &edit;
                decimalField_ = &edit;
                decimal_ = edit.OnEdit.Connect([this](double, EditPhase p) { NumericEdited(p); });
            }
            else if (column.type == GridValueType::Boolean)
            {
                checkbox_ = &Emplace<CheckBox>();
                checkbox_->SetValue(std::get<bool>(value));
                field_ = checkbox_;
                boolean_ = checkbox_->OnChange.Connect([this](bool) { OnEdit.Raise(EditPhase::Preview); });
            }
            else if (column.type == GridValueType::Choice)
            {
                combo_ = &Emplace<ComboBox>(column.choices);
                combo_->SetValue(std::get<std::string>(value));
                field_ = combo_;
                choice_ = combo_->OnChange.Connect([this](const std::string&) { OnEdit.Raise(EditPhase::Preview); });
            }
            else
            {
                text_ = &Emplace<TextBox>(std::get<std::string>(value));
                field_ = text_;
                textEdit_ = text_->OnEdit.Connect([this](const std::string&, EditPhase p) { OnEdit.Raise(p); });
            }
        }
        std::expected<GridValue, std::string> Draft() const override
        {
            if (integerField_)
                return integerField_->Draft().transform([](int64_t value) { return GridValue{value}; });
            if (decimalField_)
                return decimalField_->Draft().transform([](double value) { return GridValue{value}; });
            GridValue value;
            if (checkbox_)
                value = checkbox_->Value();
            else if (combo_)
                value = combo_->Value();
            else
                value = text_->Text();
            if (auto status = ValidateGridValue(column_, value); !status)
                return std::unexpected(status.error());
            return value;
        }
        void SetValidation(std::string message) override
        {
            if (integerField_)
                integerField_->SetValidation(message);
            else if (decimalField_)
                decimalField_->SetValidation(message);
            else if (text_)
                text_->SetValidation(message);
            SetTooltip(std::move(message));
        }
        Control& FocusTarget() override
        {
            return integerField_ ? integerField_->FocusTarget() : decimalField_ ? decimalField_->FocusTarget() : *field_;
        }

      protected:

        Size OnMeasure(Size available) override
        {
            field_->Measure(available);
            return available;
        }
        void OnArrange() override { field_->Arrange(Bounds()); }

      private:

        void NumericEdited(EditPhase phase)
        {
            // Moving between a numeric field and its rocker can commit the control.
            // The grid still owns the enclosing cell transaction.
            OnEdit.Raise(phase == EditPhase::Commit ? EditPhase::Preview : phase);
        }
        GridModelColumn column_;
        Control* field_ = nullptr;
        TextBox* text_ = nullptr;
        NumericEdit<int64_t>* integerField_ = nullptr;
        NumericEdit<double>* decimalField_ = nullptr;
        CheckBox* checkbox_ = nullptr;
        ComboBox* combo_ = nullptr;
        Event<void(int64_t, EditPhase)>::Connection integer_;
        Event<void(double, EditPhase)>::Connection decimal_;
        Event<void(bool)>::Connection boolean_;
        Event<void(const std::string&)>::Connection choice_;
        Event<void(const std::string&, EditPhase)>::Connection textEdit_;
    };
    std::unique_ptr<GridCellEditor> CreateGridEditor(const GridModelColumn& c, const GridValue& v)
    {
        return std::make_unique<BuiltinGridEditor>(c, v);
    }
}  // namespace LWSUI::internal
