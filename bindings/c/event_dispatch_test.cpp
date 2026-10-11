#include <LLUtils/Event.h>
#include <cstdio>

template<bool ThreadSafe>
int CheckDispatch()
{
    LLUtils::Event<void(), ThreadSafe> event;
    bool keepGoing = true;
    unsigned calls = 0;
    auto first = event.Subscribe([&] { ++calls; keepGoing = false; });
    auto second = event.Subscribe([&] { ++calls; });
    event.RaiseWhile([&] { return keepGoing; });
    if (calls != 1) return 1;
    event.RaiseWhile([] { return false; });
    if (calls != 1) return 2;
    event.Raise();
    if (calls != 3) return 3;
    first.Unsubscribe();
    keepGoing = true;
    event.RaiseWhile([&] { return keepGoing; });
    if (calls != 4) return 4;

    LLUtils::Event<void(int), ThreadSafe> arguments;
    int received = 0;
    auto registration = arguments.Subscribe([&](int value) { received = value; });
    arguments.RaiseWhile([] { return true; }, 42);
    if (received != 42) return 5;

    return 0;
}

int main()
{
    const int ordinary = CheckDispatch<false>();
    if (ordinary) return ordinary;
    const int threadSafe = CheckDispatch<true>();
    if (threadSafe) return threadSafe;
    std::puts("PASS: RaiseWhile in both event modes, ordinary dispatch, unsubscription, and arguments");
    return 0;
}
