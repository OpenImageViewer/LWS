#include <catch2/catch_test_macros.hpp>
#ifdef LWS_HAS_WIN32_BACKEND
    #include "Win32TestSupport.hpp"
    #include <LWS/Win32/Platform.hpp>
    #include <Windows.h>

TEST_CASE("Standalone task wakeups require the LWS event loop", "[platform][dispatcher][win32]")
{
    REQUIRE(LWS::Win32::BootstrapProcess() == LWS::Result::Success);
    LWS::PlatformContext context;
    REQUIRE(context.Init({.backend = LWS::BackendId::Win32}) == LWS::Result::Success);
    bool ran = false;
    REQUIRE(context.PostTask([&] { ran = true; }) == LWS::Result::Success);
    // A plain host pump must not deliver event-based tasks. This catches accidental selection of the
    // message dispatcher in a normal build, while the external-loop tests prove the inverse contract.
    LwsTest::DispatchHostMessages();
    CHECK_FALSE(ran);
    CHECK(context.ProcessMessages() == LWS::LoopResult::Continue);
    CHECK(ran);
    CHECK(context.Shutdown() == LWS::Result::Success);
}
#endif
