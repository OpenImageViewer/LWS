#include <LWS/Clipboard.hpp>

namespace LWS
{
    Clipboard::Clipboard(PlatformContext& platform) : platform_(platform)
    {
        platform_.RegisterService();
    }

    Clipboard::~Clipboard()
    {
        platform_.UnregisterService();
    }
}  // namespace LWS
