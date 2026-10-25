#include <catch2/catch_test_macros.hpp>
#ifdef LWS_PLATFORM_WAYLAND
    #include <LWS/source/Wayland/internal/PipeReceive.hpp>
    #include <array>
    #include <fcntl.h>
namespace
{
    struct Pipe
    {
        int ends[2]{-1, -1};
        Pipe() { REQUIRE(pipe2(ends, O_CLOEXEC | O_NONBLOCK) == 0); }
        ~Pipe()
        {
            for (int descriptor : ends)
                if (descriptor >= 0)
                    close(descriptor);
        }
    };
}  // namespace
TEST_CASE("Pipe receiving preserves bytes across blocking and dispatch boundaries", "[wayland][transfer]")
{
    Pipe pipe;
    std::string text;
    SECTION("would-block retains partial data and size limits stop the current dispatch")
    {
        REQUIRE(write(pipe.ends[1], "abcd", 4) == 4);
        CHECK(LWS::internal::receivePipe(pipe.ends[0], text, 4, 4096) == -1);
        CHECK(errno == EAGAIN);
        CHECK(text == "abcd");
        REQUIRE(write(pipe.ends[1], "efgh", 4) == 4);
        CHECK(LWS::internal::receivePipe(pipe.ends[0], text, 4, 4096) == 4);
        CHECK(text == "abcdefgh");
    }
    SECTION("a full dispatch budget defers EOF until the next dispatch")
    {
        const std::array<char, 4096> bytes{};
        REQUIRE(write(pipe.ends[1], bytes.data(), bytes.size()) == bytes.size());
        REQUIRE(close(pipe.ends[1]) == 0);
        pipe.ends[1] = -1;
        CHECK(LWS::internal::receivePipe(pipe.ends[0], text, 8192, 4096) == 4096);
        CHECK(text.size() == bytes.size());
        CHECK(LWS::internal::receivePipe(pipe.ends[0], text, 8192, 4096) == 0);
    }
}
#endif
