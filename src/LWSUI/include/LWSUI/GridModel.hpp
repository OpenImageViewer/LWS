#pragma once
#include <LWSUI/Event.hpp>
#include <LWSUI/Choice.hpp>
#include <LWSUI/NumericSpec.hpp>
#include <expected>
#include <variant>
#include <span>
#include <unordered_map>
#include <optional>
#include <string>
#include <cstdint>
namespace LWSUI
{
    using GridRowId = uint64_t;
    using GridColumnId = std::string;
    using GridValue = std::variant<std::string, int64_t, double, bool>;
    using GridStatus = std::expected<void, std::string>;
    enum class GridValueType
    {
        Text,
        Integer,
        Decimal,
        Boolean,
        Choice
    };
    struct GridCell
    {
        GridRowId row = 0;
        GridColumnId column;
        bool operator==(const GridCell&) const = default;
    };
    struct GridModelColumn
    {
        GridColumnId id;
        GridValueType type = GridValueType::Text;
        bool editable = true;
        NumericSpec<int64_t> integer;
        NumericSpec<double> decimal;
        std::vector<Choice> choices;
    };
    struct GridModelChange
    {
        enum class Kind
        {
            Value,
            Structure,
            Editability
        };
        Kind kind = Kind::Structure;
        GridCell cell;
    };
    // UI-thread confined. Identities are unique and survive display-order changes.
    // IndexOf must be efficient for models with large row counts.
    // Notify after state changes. Structural changes during callbacks must be posted.
    class GridModel
    {
      public:

        virtual ~GridModel() = default;
        virtual std::span<const GridModelColumn> Columns() const = 0;
        virtual size_t RowCount() const = 0;
        virtual GridRowId RowId(size_t index) const = 0;
        virtual std::optional<size_t> IndexOf(GridRowId row) const = 0;
        virtual GridValue Value(const GridCell& cell) const = 0;
        virtual bool Editable(const GridCell& cell) const = 0;
        // Programmatic assignment validates values but bypasses user editability.
        virtual GridStatus SetValue(const GridCell& cell, const GridValue& value) = 0;
        Event<void(const GridModelChange&)> OnChange;
    };
    GridStatus ValidateGridValue(const GridModelColumn&, const GridValue&);
    std::string FormatGridValue(const GridValue&);
    int CompareGridValues(const GridValue&, const GridValue&);
    class MemoryGridModel final : public GridModel
    {
      public:

        explicit MemoryGridModel(std::vector<GridModelColumn> columns);
        GridRowId AddRow(std::vector<GridValue> values);
        bool RemoveRow(GridRowId row);
        std::span<const GridModelColumn> Columns() const override { return columns_; }
        size_t RowCount() const override { return rows_.size(); }
        GridRowId RowId(size_t index) const override { return rows_.at(index).id; }
        std::optional<size_t> IndexOf(GridRowId row) const override;
        GridValue Value(const GridCell& cell) const override;
        bool Editable(const GridCell& cell) const override;
        GridStatus SetValue(const GridCell& cell, const GridValue& value) override;
        void SetCellReadOnly(const GridCell&, bool);
        // Called by every write, including external writes. Rejection leaves data intact.
        std::function<GridStatus(const GridCell&, const GridValue&)> validate;

      private:

        struct Row
        {
            GridRowId id;
            std::vector<GridValue> values;
        };
        size_t ColumnIndex(const GridColumnId&) const;
        std::vector<GridModelColumn> columns_;
        std::vector<Row> rows_;
        std::unordered_map<GridRowId, size_t> indices_;
        std::unordered_map<GridRowId, std::vector<GridColumnId>> readOnly_;
        GridRowId nextId_ = 1;
    };
}  // namespace LWSUI
