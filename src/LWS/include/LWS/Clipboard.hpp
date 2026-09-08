#pragma once

#include <LWS/Platform.hpp>
#include <LWS/Window.hpp>
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

        explicit Clipboard(PlatformContext& platform);
        ~Clipboard();

        Clipboard(const Clipboard&) = delete;
        Clipboard& operator=(const Clipboard&) = delete;
        Clipboard(Clipboard&&) = delete;
        Clipboard& operator=(Clipboard&&) = delete;

        void RegisterFormat(ClipboardFormatType format);
        ClipboardFormatType RegisterFormat(const string_type& format);
        ClipboardResult SetClipboardData(Window& ownerWindow, ClipboardFormatType format, const LLUtils::Buffer& data);
        ClipboardResult SetClipboardData(Window& ownerWindow, ClipboardFormatType format, const std::byte* data,
                                         size_t size);
        ClipboardResult SetClipboardData(Window& ownerWindow, std::span<const ClipboardDataView> data);
        ClipboardResult SetClipboardText(Window& ownerWindow, const char_type* text);
#ifdef LWS_HAS_WIN32_BACKEND
        ClipboardResult SetClipboardText(Window& ownerWindow, const char* text);
#endif
        ClipboardData GetClipboardData();

      private:

        PlatformContext& platform_;
        ClipboardResult GetClipboardError() const;
        std::vector<ClipboardFormatType> fListFormats;
    };
}  // namespace LWS
