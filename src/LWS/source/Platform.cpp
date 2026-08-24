#include <LWS/Platform.hpp>

namespace LWS::Platform
{
    Session::Session() : fResult(init()) {}

    Session::~Session()
    {
        if (fResult == Result::Success)
            shutdown();
    }
}
