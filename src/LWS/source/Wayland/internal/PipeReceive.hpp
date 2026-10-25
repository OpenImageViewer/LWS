#pragma once
#include <cerrno>
#include <string>
#include <unistd.h>
namespace LWS::internal
{
    // Read only the current dispatch budget. Retain the final read result and errno so each caller
    // keeps its existing EOF, hangup, timeout, cancellation, and size-limit policy.
    inline ssize_t receivePipe(int descriptor, std::string& text, size_t limit, size_t budget)
    {
        char buffer[4096];
        ssize_t count = 0;
        size_t received = 0;
        do
        {
            do
            {
                count = read(descriptor, buffer, sizeof(buffer));
            } while (count < 0 && errno == EINTR);
            if (count > 0)
            {
                text.append(buffer, static_cast<size_t>(count));
                received += static_cast<size_t>(count);
            }
        } while (count > 0 && received < budget && text.size() <= limit);
        return count;
    }
}  // namespace LWS::internal
