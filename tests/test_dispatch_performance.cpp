#include <catch2/catch_test_macros.hpp>
#ifdef LWS_HAS_WIN32_BACKEND
    #include <LWS/Platform.hpp>
    #include <LWS/Win32/Platform.hpp>
    #include <Windows.h>
    #include <atomic>
    #include <chrono>
    #include <cstdio>
    #include <semaphore>
    #include <thread>

namespace
{
    uint64_t ProcessCpuTime()
    {
        FILETIME created{}, exited{}, kernel{}, user{};
        GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
        const auto value = [](FILETIME time)
        { return (static_cast<uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime; };
        return value(kernel) + value(user);
    }
}  // namespace

TEST_CASE("Win32 task dispatcher performance sample", "[.][dispatch-performance][win32]")
{
    using Clock = std::chrono::steady_clock;
    REQUIRE(LWS::Win32::BootstrapProcess() == LWS::Result::Success);
    LWS::PlatformContext context;
    REQUIRE(context.Init({.backend = LWS::BackendId::Win32}) == LWS::Result::Success);
    constexpr size_t burstCount = 100000;
    constexpr size_t roundTrips = 2000;
    size_t completed{};
    std::atomic_bool accepted{true};
    std::binary_semaphore delivered{0};
    double burstMs{}, roundTripUs{};
    uint64_t idleCpu{};
    std::thread producer(
        [&]
        {
            auto start = Clock::now();
            for (size_t index = 0; index < burstCount; ++index)
                accepted.store(accepted.load() && context.PostTask(
                                                      [&]
                                                      {
                                                          if (++completed == burstCount)
                                                              delivered.release();
                                                      }) == LWS::Result::Success);
            delivered.acquire();
            burstMs = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            start = Clock::now();
            for (size_t index = 0; index < roundTrips; ++index)
            {
                accepted.store(accepted.load() &&
                               context.PostTask([&] { delivered.release(); }) == LWS::Result::Success);
                delivered.acquire();
            }
            roundTripUs = std::chrono::duration<double, std::micro>(Clock::now() - start).count() / roundTrips;
            const auto cpu = ProcessCpuTime();
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            idleCpu = ProcessCpuTime() - cpu;
            context.RequestQuit();
        });
    CHECK(context.RunMessageLoop() == LWS::LoopResult::Quit);
    producer.join();
    CHECK(accepted.load());
    CHECK(completed == burstCount);
    std::printf("dispatch: burst_ms=%.3f roundtrip_us=%.3f idle_cpu_ms=%.3f\n", burstMs, roundTripUs,
                static_cast<double>(idleCpu) / 10000.0);
    CHECK(context.Shutdown() == LWS::Result::Success);
}
#endif
