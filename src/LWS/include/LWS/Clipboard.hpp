#pragma once

#include <LWS/interfaces/backends.hpp>
#include <LLUtils/Buffer.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <tuple>
#include <vector>

namespace LWS
{
    using ClipboardFormatType = std::uint32_t;
    using ClipboardData = std::tuple<ClipboardFormatType, LLUtils::Buffer>;

    struct ClipboardDataView
    {
        ClipboardFormatType format = 0;
        std::span<const std::byte> data;
    };

    enum class ClipboardResult
    {
        Success,
        AccessDenied,
        UnknownError,
    };

    class Clipboard
    {
    public:
        void RegisterFormat(ClipboardFormatType format);
        ClipboardFormatType RegisterFormat(const string_type& format);
        ClipboardResult SetClipboardData(Handle ownerWindow, ClipboardFormatType format, const LLUtils::Buffer& data);
        ClipboardResult SetClipboardData(Handle ownerWindow, ClipboardFormatType format, const std::byte* data, size_t size);
        ClipboardResult SetClipboardData(Handle ownerWindow, std::span<const ClipboardDataView> data);
        ClipboardResult SetClipboardText(Handle ownerWindow, const char_type* text);
#ifdef LWS_PLATFORM_WIN32
        ClipboardResult SetClipboardText(Handle ownerWindow, const char* text);
#endif
        ClipboardData GetClipboardData();

    private:
        ClipboardResult GetClipboardError() const;
        std::vector<ClipboardFormatType> fListFormats;
    };
}
