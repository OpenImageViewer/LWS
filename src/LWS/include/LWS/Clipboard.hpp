#pragma once

#include <LWS/interfaces/backends.hpp>
#include <LLUtils/Buffer.h>

#include <cstddef>
#include <cstdint>
#include <tuple>
#include <vector>

namespace LWS
{
    using ClipboardFormatType = std::uint32_t;
    using ClipboardData = std::tuple<ClipboardFormatType, LLUtils::Buffer>;

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
        ClipboardResult SetClipboardData(ClipboardFormatType format, const LLUtils::Buffer& data);
        ClipboardResult SetClipboardData(ClipboardFormatType format, const std::byte* data, size_t size);
        ClipboardResult SetClipboardText(const char_type* text);
        ClipboardResult SetClipboardText(const char* text);
        ClipboardResult SetClipboardDataNative(ClipboardFormatType format, Handle data);
        ClipboardData GetClipboardData();

    private:
        ClipboardResult GetClipboardError() const;
        std::vector<ClipboardFormatType> fListFormats;
    };
}
