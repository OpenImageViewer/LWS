#pragma once
#include <LWSUI/GridView.hpp>
#include <algorithm>
#include <numeric>
#include <unordered_set>
#include <unordered_map>
namespace LWSUI::internal
{
    struct GridCellHash
    {
        size_t operator()(const GridCell& c) const
        {
            return std::hash<GridRowId>{}(c.row) ^ (std::hash<std::string>{}(c.column) << 1);
        }
    };
    struct GridSelection
    {
        std::optional<GridCell> current, anchor;
        std::unordered_set<GridCell, GridCellHash> cells;
        bool Selected(GridCell c, bool rows) const
        {
            if (rows)
                c.column.clear();
            return cells.contains(c);
        }
        GridSelectionChange Replace(std::unordered_set<GridCell, GridCellHash> next)
        {
            GridSelectionChange change;
            for (const auto& c : next)
                if (!cells.contains(c))
                    change.added.push_back(c);
            for (const auto& c : cells)
                if (!next.contains(c))
                    change.removed.push_back(c);
            cells = std::move(next);
            return change;
        }
    };
    class GridProjection
    {
      public:

        void Rebuild(const GridModel& model, const GridColumn* column, GridSortDirection direction)
        {
            order_.clear();
            inverse_.clear();
            if (!column || direction == GridSortDirection::None)
                return;
            order_.resize(model.RowCount());
            std::iota(order_.begin(), order_.end(), size_t{});
            std::stable_sort(order_.begin(), order_.end(),
                             [&](size_t a, size_t b)
                             {
                                 const auto x = model.Value({model.RowId(a), column->id}),
                                            y = model.Value({model.RowId(b), column->id});
                                 const int result = column->compare ? column->compare(x, y) : CompareGridValues(x, y);
                                 return direction == GridSortDirection::Ascending ? result < 0 : result > 0;
                             });
            for (size_t i = 0; i < order_.size(); ++i)
                inverse_[model.RowId(order_[i])] = i;
        }
        GridRowId Row(const GridModel& model, size_t i) const { return model.RowId(order_.empty() ? i : order_.at(i)); }
        std::optional<size_t> Index(const GridModel& model, GridRowId row) const
        {
            if (order_.empty())
                return model.IndexOf(row);
            auto it = inverse_.find(row);
            return it == inverse_.end() ? std::nullopt : std::optional(it->second);
        }

      private:

        std::vector<size_t> order_;
        std::unordered_map<GridRowId, size_t> inverse_;
    };
    struct GridColumnGeometry
    {
        size_t index;
        Rect bounds, clip;
    };
    struct GridLayout
    {
        Rect viewport, body, header, center, left, right;
        std::vector<GridColumnGeometry> columns;
        float horizontalExtent = 0, verticalExtent = 0, scrollPageHeight = 0, rowOffset = 0;
        float VerticalLimit() const { return std::max(0.f, verticalExtent - scrollPageHeight); }
        float RowY(size_t row, float height) const { return body.y + float(double(row) * height - rowOffset); }
        bool horizontalBar = false, verticalBar = false;
        void Build(Rect bounds, const std::vector<GridColumn>& definitions, const GridOptions& options, size_t rowCount,
                   float bar, float offsetX, float offsetY)
        {
            columns.clear();
            const float headerHeight = options.headerVisible ? options.headerHeight : 0;
            const float fixedHeader = options.stickyHeaders ? std::min(bounds.height, headerHeight) : 0;
            verticalExtent = float(double(rowCount) * options.rowHeight) + (options.stickyHeaders ? 0 : headerHeight);
            std::vector<float> widths(definitions.size());
            float fixed = 0, weights = 0;
            for (size_t i = 0; i < definitions.size(); ++i)
            {
                const auto& c = definitions[i];
                if (!c.visible)
                    continue;
                widths[i] = std::clamp(c.width, c.minimumWidth, c.maximumWidth);
                if (c.widthMode == GridWidthMode::Proportional)
                    weights += c.weight;
                else
                    fixed += widths[i];
            }
            const auto calculate = [&](float available)
            {
                for (size_t i = 0; i < definitions.size(); ++i)
                {
                    const auto& c = definitions[i];
                    if (c.visible && c.widthMode == GridWidthMode::Proportional)
                        widths[i] = std::clamp(std::max(0.f, available - fixed) * c.weight / std::max(1.f, weights),
                                               c.minimumWidth, c.maximumWidth);
                }
            };
            verticalBar = horizontalBar = false;
            for (int pass = 0; pass < 3; ++pass)
            {
                const float available = std::max(0.f, bounds.width - (verticalBar ? bar : 0));
                calculate(available);
                float pinned = 0, scrolling = 0;
                for (size_t i = 0; i < definitions.size(); ++i)
                    if (definitions[i].pin == GridPin::None)
                        scrolling += widths[i];
                    else
                        pinned += widths[i];
                const float middle = std::max(0.f, available - pinned);
                const bool horizontal = middle > 0 && scrolling > middle;
                const bool vertical = verticalExtent >
                                      std::max(0.f, bounds.height - fixedHeader - (horizontal ? bar : 0));
                if (horizontal == horizontalBar && vertical == verticalBar)
                    break;
                horizontalBar = horizontal;
                verticalBar = vertical;
            }
            calculate(std::max(0.f, bounds.width - (verticalBar ? bar : 0)));
            const float width = std::max(0.f, bounds.width - (verticalBar ? bar : 0));
            viewport = {bounds.x, bounds.y, width, std::max(0.f, bounds.height - (horizontalBar ? bar : 0))};
            scrollPageHeight = std::max(0.f, viewport.height - fixedHeader);
            header = {bounds.x, bounds.y - (options.stickyHeaders ? 0 : offsetY), width, headerHeight};
            const float remainingHeader = std::clamp(header.y + header.height - viewport.y, 0.f, viewport.height);
            body = {bounds.x, bounds.y + remainingHeader, width, viewport.height - remainingHeader};
            rowOffset = options.stickyHeaders ? offsetY : std::max(0.f, offsetY - headerHeight);
            float leftWidth = 0, rightWidth = 0, middleWidth = 0;
            for (size_t i = 0; i < definitions.size(); ++i)
                if (definitions[i].pin == GridPin::Left)
                    leftWidth += widths[i];
                else if (definitions[i].pin == GridPin::Right)
                    rightWidth += widths[i];
                else
                    middleWidth += widths[i];
            // Narrow bounds never allow pinned regions to overlap.
            const float visibleLeft = std::min(leftWidth, width);
            const float visibleRight = std::min(rightWidth, std::max(0.f, width - visibleLeft));
            left = {body.x, body.y, visibleLeft, body.height};
            right = {body.x + width - visibleRight, body.y, visibleRight, body.height};
            center = {body.x + visibleLeft, body.y, std::max(0.f, width - visibleLeft - visibleRight), body.height};
            horizontalExtent = center.width > 0 ? std::max(0.f, middleWidth - center.width) : 0;
            horizontalBar = horizontalExtent > 0;
            float xLeft = body.x, xRight = body.x + width - rightWidth,
                  xMiddle = center.x - std::min(offsetX, horizontalExtent);
            for (size_t i = 0; i < definitions.size(); ++i)
            {
                const auto& c = definitions[i];
                if (!c.visible)
                    continue;
                float& x = c.pin == GridPin::Left ? xLeft : c.pin == GridPin::Right ? xRight : xMiddle;
                const auto region = c.pin == GridPin::Left ? left : c.pin == GridPin::Right ? right : center;
                const Rect boundsCell{x, body.y - rowOffset, widths[i], body.height};
                x += widths[i];
                columns.push_back({i, boundsCell, region});
            }
        }
        const GridColumnGeometry* Find(size_t index) const
        {
            auto it = std::ranges::find(columns, index, &GridColumnGeometry::index);
            return it == columns.end() ? nullptr : &*it;
        }
        const GridColumnGeometry* At(float x) const
        {
            for (const auto& c : columns)
                if (x >= c.bounds.x && x < c.bounds.x + c.bounds.width && x >= c.clip.x && x < c.clip.x + c.clip.width)
                    return &c;
            return nullptr;
        }
    };
    inline std::string GridText(const GridColumn& column, const GridModelColumn& modelColumn, const GridValue& value)
    {
        if (column.format)
            return column.format(value);
        if (modelColumn.type == GridValueType::Choice)
            for (const auto& option : modelColumn.choices)
                if (option.value == std::get<std::string>(value))
                    return option.label;
        return FormatGridValue(value);
    }
    inline std::string ClipboardField(std::string text)
    {
        if (text.find_first_of("\t\r\n\"") == text.npos)
            return text;
        std::string escaped = "\"";
        for (char c : text)
        {
            if (c == '"')
                escaped += '"';
            escaped += c;
        }
        return escaped + '"';
    }
    std::unique_ptr<GridCellEditor> CreateGridEditor(const GridModelColumn&, const GridValue&);
}  // namespace LWSUI::internal
