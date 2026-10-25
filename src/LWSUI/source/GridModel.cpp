#include <LWSUI/GridModel.hpp>
#include <LWSUI/internal/NumericText.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>
namespace LWSUI
{
    GridStatus ValidateGridValue(const GridModelColumn& c, const GridValue& v)
    {
        const size_t type = c.type == GridValueType::Integer   ? 1
                            : c.type == GridValueType::Decimal ? 2
                            : c.type == GridValueType::Boolean ? 3
                                                               : 0;
        if (v.index() != type)
            return std::unexpected("Cell value has the wrong type");
        if (auto n = std::get_if<int64_t>(&v))
        {
            if (!ValidNumericValue(*n, c.integer))
                return std::unexpected("Enter an integer within the allowed bounds");
        }
        if (auto n = std::get_if<double>(&v))
        {
            if (!ValidNumericValue(*n, c.decimal))
                return std::unexpected("Enter a finite number within the allowed bounds");
        }
        if (c.type == GridValueType::Choice &&
            std::ranges::none_of(c.choices, [&](const Choice& x) { return x.value == std::get<std::string>(v); }))
            return std::unexpected("Choose a listed value");
        return {};
    }
    std::string FormatGridValue(const GridValue& v)
    {
        return std::visit(
            [](const auto& x) -> std::string
            {
                using T = std::decay_t<decltype(x)>;
                if constexpr (std::is_same_v<T, std::string>)
                    return x;
                else if constexpr (std::is_same_v<T, bool>)
                    return x ? "True" : "False";
                else
                    return internal::NumericText(x);
            },
            v);
    }
    int CompareGridValues(const GridValue& a, const GridValue& b)
    {
        if (a.index() != b.index())
            return a.index() < b.index() ? -1 : 1;
        return std::visit(
            [&](const auto& x)
            {
                const auto& y = std::get<std::decay_t<decltype(x)>>(b);
                return x < y ? -1 : y < x ? 1 : 0;
            },
            a);
    }
    MemoryGridModel::MemoryGridModel(std::vector<GridModelColumn> columns) : columns_(std::move(columns))
    {
        std::unordered_set<std::string> ids;
        for (const auto& c : columns_)
        {
            if (c.id.empty() || !ids.insert(c.id).second)
                throw std::invalid_argument("Column identities must be unique and nonempty");
            if (!ValidNumericSpec(c.integer) || !ValidNumericSpec(c.decimal))
                throw std::invalid_argument("Invalid numeric specification");
            if (c.type == GridValueType::Choice)
            {
                std::unordered_set<std::string> values;
                if (c.choices.empty())
                    throw std::invalid_argument("Choice column needs options");
                for (const auto& choice : c.choices)
                    if (!values.insert(choice.value).second)
                        throw std::invalid_argument("Choice values must be unique");
            }
        }
    }
    size_t MemoryGridModel::ColumnIndex(const GridColumnId& id) const
    {
        auto it = std::ranges::find(columns_, id, &GridModelColumn::id);
        if (it == columns_.end())
            throw std::out_of_range("Unknown grid column");
        return size_t(it - columns_.begin());
    }
    GridRowId MemoryGridModel::AddRow(std::vector<GridValue> values)
    {
        if (values.size() != columns_.size())
            throw std::invalid_argument("Row has the wrong cell count");
        for (size_t i = 0; i < values.size(); ++i)
            if (auto status = ValidateGridValue(columns_[i], values[i]); !status)
                throw std::invalid_argument(status.error());
        const auto id = nextId_++;
        rows_.push_back({id, std::move(values)});
        indices_[id] = rows_.size() - 1;
        OnChange.Raise({GridModelChange::Kind::Structure, {}});
        return id;
    }
    bool MemoryGridModel::RemoveRow(GridRowId row)
    {
        const auto index = IndexOf(row);
        if (!index)
            return false;
        rows_.erase(rows_.begin() + *index);
        indices_.erase(row);
        readOnly_.erase(row);
        for (size_t i = *index; i < rows_.size(); ++i)
            indices_[rows_[i].id] = i;
        OnChange.Raise({GridModelChange::Kind::Structure, {}});
        return true;
    }
    std::optional<size_t> MemoryGridModel::IndexOf(GridRowId row) const
    {
        auto it = indices_.find(row);
        return it == indices_.end() ? std::nullopt : std::optional(it->second);
    }
    GridValue MemoryGridModel::Value(const GridCell& cell) const
    {
        const auto index = IndexOf(cell.row);
        if (!index)
            throw std::out_of_range("Unknown grid row");
        return rows_[*index].values.at(ColumnIndex(cell.column));
    }
    bool MemoryGridModel::Editable(const GridCell& cell) const
    {
        if (!IndexOf(cell.row))
            return false;
        auto column = std::ranges::find(columns_, cell.column, &GridModelColumn::id);
        if (column == columns_.end() || !column->editable)
            return false;
        auto it = readOnly_.find(cell.row);
        return it == readOnly_.end() || std::ranges::find(it->second, cell.column) == it->second.end();
    }
    GridStatus MemoryGridModel::SetValue(const GridCell& cell, const GridValue& value)
    {
        if (!IndexOf(cell.row))
            return std::unexpected("Unknown grid row");
        auto col = std::ranges::find(columns_, cell.column, &GridModelColumn::id);
        if (col == columns_.end())
            return std::unexpected("Unknown grid column");
        if (auto status = ValidateGridValue(*col, value); !status)
            return status;
        if (validate)
            if (auto status = validate(cell, value); !status)
                return status;
        auto& target = rows_[*IndexOf(cell.row)].values[size_t(col - columns_.begin())];
        if (target == value)
            return {};
        target = value;
        OnChange.Raise({GridModelChange::Kind::Value, cell});
        return {};
    }
    void MemoryGridModel::SetCellReadOnly(const GridCell& cell, bool value)
    {
        if (!IndexOf(cell.row))
            throw std::out_of_range("Unknown grid row");
        ColumnIndex(cell.column);
        const auto found = readOnly_.find(cell.row);
        const bool wasReadOnly = found != readOnly_.end() &&
                                 std::ranges::find(found->second, cell.column) != found->second.end();
        if (wasReadOnly == value)
            return;
        auto& columns = readOnly_[cell.row];
        std::erase(columns, cell.column);
        if (value)
            columns.push_back(cell.column);
        OnChange.Raise({GridModelChange::Kind::Editability, cell});
    }
}  // namespace LWSUI
