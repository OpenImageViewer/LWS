#ifdef LWS_TEST_UI
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <LWSUI/GridModel.hpp>
#include <limits>
#include <memory>
#include <stdexcept>
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
}
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
#endif
