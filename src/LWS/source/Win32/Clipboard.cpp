#ifdef LWS_PLATFORM_WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <LWS/Clipboard.hpp>
#include <LLUtils/Exception.h>
#include <LLUtils/StringUtility.h>

#include <string_view>

namespace LWS
{
    void Clipboard::RegisterFormat(ClipboardFormatType format)
    {
        fListFormats.push_back(format);
    }

    ClipboardFormatType Clipboard::RegisterFormat(const string_type& format)
    {
        auto formatID = RegisterClipboardFormat(format.c_str());
        RegisterFormat(formatID);
        return formatID;
    }

    ClipboardResult Clipboard::SetClipboardData(ClipboardFormatType format, const LLUtils::Buffer& data)
    {
        return SetClipboardData(format, data.data(), data.size());
    }

    ClipboardResult Clipboard::SetClipboardData(ClipboardFormatType format, const std::byte* data, size_t size)
    {
        HANDLE handle = GlobalAlloc(GHND, size);
        ClipboardResult result = ClipboardResult::UnknownError;
        if (handle != nullptr && handle != INVALID_HANDLE_VALUE)
        {
            void* buffer = GlobalLock(handle);
            if (buffer == nullptr)
            {
                LL_EXCEPTION_SYSTEM_ERROR("Can't lock global memory.");
            }

            memcpy(buffer, data, size);

            if (GlobalUnlock(handle) > 0 && GetLastError() != NO_ERROR)
            {
                LL_EXCEPTION_SYSTEM_ERROR("Can't unlock global memory.");
            }

            result = SetClipboardDataNative(format, reinterpret_cast<Handle>(handle));
            if (result != ClipboardResult::Success && GlobalFree(handle) != nullptr)
            {
                LL_EXCEPTION_SYSTEM_ERROR("Can't free global handle");
            }
        }
        return result;
    }

    ClipboardResult Clipboard::SetClipboardText(const char_type* text)
    {
        std::wstring_view strView(text);
        auto result = SetClipboardData(CF_UNICODETEXT, reinterpret_cast<const std::byte*>(text),
                                       (strView.length() + 1) * sizeof(char_type));

        if (result == ClipboardResult::Success)
        {
            std::string ansi = LLUtils::StringUtility::ConvertString<std::string>(text);
            result = SetClipboardData(CF_TEXT, reinterpret_cast<const std::byte*>(ansi.data()),
                                      (ansi.length() + 1) * sizeof(char));
        }

        return result;
    }

    ClipboardResult Clipboard::SetClipboardText(const char* text)
    {
        return SetClipboardText(LLUtils::StringUtility::ToWString(text).c_str());
    }

    ClipboardResult Clipboard::GetClipboardError() const
    {
        switch (GetLastError())
        {
        case ERROR_ACCESS_DENIED:
            return ClipboardResult::AccessDenied;
        default:
            return ClipboardResult::UnknownError;
        }
    }

    ClipboardResult Clipboard::SetClipboardDataNative(ClipboardFormatType format, Handle data)
    {
        ClipboardResult result = ClipboardResult::UnknownError;
        if (OpenClipboard(nullptr) == 0)
        {
            result = GetClipboardError();
        }
        else
        {
            if (::SetClipboardData(format, reinterpret_cast<HANDLE>(data)) != nullptr)
            {
                if (CloseClipboard() == FALSE)
                {
                    LL_EXCEPTION_SYSTEM_ERROR("can not close clipboard.");
                }
                result = ClipboardResult::Success;
            }
            else
            {
                result = GetClipboardError();
            }
        }

        return result;
    }

    ClipboardData Clipboard::GetClipboardData()
    {
        ClipboardData result;
        ClipboardFormatType selectedFormatID{};

        for (const auto formatID : fListFormats)
        {
            if (IsClipboardFormatAvailable(formatID))
            {
                selectedFormatID = formatID;
                break;
            }
        }

        if (selectedFormatID != 0 && OpenClipboard(nullptr))
        {
            HANDLE clipboard = ::GetClipboardData(selectedFormatID);
            if (!clipboard)
            {
                LL_EXCEPTION(LLUtils::Exception::ErrorCode::NotImplemented, "Unsupported clipboard bitmap format type");
            }

            if (clipboard != nullptr && clipboard != INVALID_HANDLE_VALUE)
            {
                size_t size = GlobalSize(clipboard);
                void* clipboardBuffer = GlobalLock(clipboard);
                if (clipboardBuffer != nullptr)
                {
                    auto& buffer = std::get<LLUtils::Buffer>(result);
                    buffer.Allocate(size);
                    buffer.Write(reinterpret_cast<const std::byte*>(clipboardBuffer), 0, size);
                    std::get<ClipboardFormatType>(result) = selectedFormatID;
                    GlobalUnlock(clipboard);
                }
            }
            CloseClipboard();
        }

        return result;
    }
}
#endif
