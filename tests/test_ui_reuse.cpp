#ifdef LWS_TEST_UI
#include <catch2/catch_test_macros.hpp>
#include <LWSUI/internal/ScopedFlag.hpp>
#include <stdexcept>
#include <LWSUI/Containers.hpp>
#include <LWSUI/internal/ControlTree.hpp>

using namespace LWSUI;
TEST_CASE("Scoped flags restore nested state during exception unwinding", "[ui][reuse][state]")
{
    bool flag = false;
    {
        const internal::ScopedFlag outer(flag, true);
        REQUIRE(flag);
        try
        {
            const internal::ScopedFlag inner(flag, false);
            REQUIRE_FALSE(flag);
            throw std::runtime_error("callback failed");
        }
        catch (const std::runtime_error&)
        {
            REQUIRE(flag);
        }
        REQUIRE(flag);
    }
    REQUIRE_FALSE(flag);
    flag = true;
    {
        const internal::ScopedFlag unchanged(flag, true);
        REQUIRE(flag);
    }
    REQUIRE(flag);
}

TEST_CASE("Shared ancestry membership is inclusive and distinguishes separate trees", "[ui][reuse][tree]")
{
    StackPanel root, other;
    auto& branch = root.Emplace<StackPanel>();
    auto& leaf = branch.Emplace<Label>("Leaf");
    REQUIRE(internal::IsWithin(&leaf, &root));
    REQUIRE(internal::IsWithin(&leaf, &branch));
    REQUIRE(internal::IsWithin(&leaf, &leaf));
    REQUIRE_FALSE(internal::IsWithin(&root, &leaf));
    REQUIRE_FALSE(internal::IsWithin(&leaf, &other));
    REQUIRE_FALSE(internal::IsWithin(nullptr, &root));
    REQUIRE_FALSE(internal::IsWithin(&leaf, nullptr));
    REQUIRE_FALSE(internal::IsWithin(nullptr, nullptr));
    auto detached = branch.Remove(leaf);
    REQUIRE_FALSE(internal::IsWithin(detached.get(), &root));
    REQUIRE(internal::IsWithin(detached.get(), detached.get()));
}

TEST_CASE("Connection collections retain heterogeneous subscriptions and disconnect on clear", "[ui][reuse][event]")
{
    Event<void(int)> first;
    Event<void()> second;
    int total = 0;
    ConnectionSet connections;
    connections.Connect(first, [&](int n) { total += n; });
    connections.Connect(second, [&] { ++total; });
    first.Raise(2);
    second.Raise();
    REQUIRE(total == 3);
    auto moved = std::move(connections);
    connections.Clear();
    first.Raise(2);
    REQUIRE(total == 5);
    moved.Clear();
    first.Raise(2);
    second.Raise();
    REQUIRE(total == 5);
    {
        ConnectionSet scoped;
        scoped.Connect(first, [&](int n) { total += n; });
        first.Raise(3);
    }
    first.Raise(3);
    REQUIRE(total == 8);
    moved.Connect(first, [&](int) { moved.Clear(); });
    moved.Connect(first, [&](int) { ++total; });
    first.Raise(1);
    REQUIRE(total == 8);
}
#endif
