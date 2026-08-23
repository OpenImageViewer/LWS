#ifdef LWS_PLATFORM_WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <LWS/Clipboard.hpp>
#include <LLUtils/StringUtility.h>

#include <array>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    class ClipboardSession
    {
    public:
        explicit ClipboardSession(HWND owner) : open_(OpenClipboard(owner) != FALSE) {}
        ~ClipboardSession()
        {
            if (open_)
            {
                std::ignore = CloseClipboard();
            }
        }

        ClipboardSession(const ClipboardSession&) = delete;
        ClipboardSession& operator=(const ClipboardSession&) = delete;

        explicit operator bool() const { return open_; }

    private:
        bool open_ = false;
    };

    class GlobalMemory
    {
    public:
        class ScopedLock
        {
        public:
            explicit ScopedLock(HGLOBAL handle) : handle_(handle), data_(GlobalLock(handle)) {}
            ~ScopedLock()
            {
                if (data_ != nullptr)
                {
                    std::ignore = GlobalUnlock(handle_);
                }
            }

            ScopedLock(const ScopedLock&) = delete;
            ScopedLock& operator=(const ScopedLock&) = delete;

            explicit operator bool() const { return data_ != nullptr; }
            void* data() const { return data_; }

        private:
            HGLOBAL handle_ = nullptr;
            void* data_ = nullptr;
        };

        explicit GlobalMemory(std::span<const std::byte> data) : handle_(GlobalAlloc(GMEM_MOVEABLE, data.size()))
        {
            if (handle_ != nullptr)
            {
                ScopedLock lockedMemory = lock();
                if (lockedMemory)
                {
                    memcpy(lockedMemory.data(), data.data(), data.size());
                }
                else
                {
                    reset();
                }
            }
        }

        GlobalMemory(GlobalMemory&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}
        GlobalMemory& operator=(GlobalMemory&& other) noexcept
        {
            if (this != &other)
            {
                reset();
                handle_ = std::exchange(other.handle_, nullptr);
            }
            return *this;
        }
        ~GlobalMemory() { reset(); }

        GlobalMemory(const GlobalMemory&) = delete;
        GlobalMemory& operator=(const GlobalMemory&) = delete;

        explicit operator bool() const { return handle_ != nullptr; }
        HGLOBAL get() const { return handle_; }
        HGLOBAL release() { return std::exchange(handle_, nullptr); }
        ScopedLock lock() const { return ScopedLock(handle_); }
        static ScopedLock LockBorrowed(HGLOBAL handle) { return ScopedLock(handle); }

    private:
        void reset()
        {
            if (handle_ != nullptr)
            {
                std::ignore = GlobalFree(handle_);
                handle_ = nullptr;
            }
        }

        HGLOBAL handle_ = nullptr;
    };
}

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

    ClipboardResult Clipboard::SetClipboardData(Handle ownerWindow, ClipboardFormatType format,
                                                 const LLUtils::Buffer& data)
    {
        return SetClipboardData(ownerWindow, format, data.data(), data.size());
    }

    ClipboardResult Clipboard::SetClipboardData(Handle ownerWindow, ClipboardFormatType format,
                                                 const std::byte* data, size_t size)
    {
        if (data == nullptr || size == 0)
        {
            return ClipboardResult::UnknownError;
        }

        const ClipboardDataView entry{ .format = format, .data = { data, size } };
        return SetClipboardData(ownerWindow, std::span(&entry, 1));
    }

    ClipboardResult Clipboard::SetClipboardData(Handle ownerWindow, std::span<const ClipboardDataView> data)
    {
        if (ownerWindow == 0 || data.empty())
        {
            return ClipboardResult::UnknownError;
        }

        std::vector<GlobalMemory> handles;
        handles.reserve(data.size());
        for (const ClipboardDataView& entry : data)
        {
            if (entry.format == 0 || entry.data.empty())
            {
                return ClipboardResult::UnknownError;
            }

            handles.emplace_back(entry.data);
            if (!handles.back())
            {
                return ClipboardResult::UnknownError;
            }
        }

        ClipboardSession session(reinterpret_cast<HWND>(ownerWindow));
        if (!session)
        {
            return GetClipboardError();
        }

        if (EmptyClipboard() == FALSE)
        {
            return GetClipboardError();
        }

        for (size_t index = 0; index < data.size(); ++index)
        {
            if (::SetClipboardData(data[index].format, handles[index].get()) == nullptr)
            {
                const ClipboardResult result = GetClipboardError();
                std::ignore = EmptyClipboard();
                return result;
            }
            std::ignore = handles[index].release();
        }

        return ClipboardResult::Success;
    }

    ClipboardResult Clipboard::SetClipboardText(Handle ownerWindow, const char_type* text)
    {
        if (text == nullptr)
        {
            return ClipboardResult::UnknownError;
        }

        const std::wstring_view textView(text);
        const std::string ansi = LLUtils::StringUtility::ConvertString<std::string>(text);
        const std::array entries{
            ClipboardDataView{
                .format = CF_UNICODETEXT,
                .data = { reinterpret_cast<const std::byte*>(text), (textView.length() + 1) * sizeof(char_type) },
            },
            ClipboardDataView{
                .format = CF_TEXT,
                .data = { reinterpret_cast<const std::byte*>(ansi.data()), (ansi.length() + 1) * sizeof(char) },
            },
        };
        return SetClipboardData(ownerWindow, entries);
    }

    ClipboardResult Clipboard::SetClipboardText(Handle ownerWindow, const char* text)
    {
        if (text == nullptr)
        {
            return ClipboardResult::UnknownError;
        }

        const string_type converted = LLUtils::StringUtility::ToWString(text);
        return SetClipboardText(ownerWindow, converted.c_str());
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

        if (selectedFormatID == 0)
        {
            return result;
        }

        ClipboardSession session(nullptr);
        if (!session)
        {
            return result;
        }

        HANDLE clipboard = ::GetClipboardData(selectedFormatID);
        if (clipboard == nullptr || clipboard == INVALID_HANDLE_VALUE)
        {
            return result;
        }

        const size_t size = GlobalSize(clipboard);
        GlobalMemory::ScopedLock lockedMemory = GlobalMemory::LockBorrowed(clipboard);
        if (!lockedMemory)
        {
            return result;
        }

        auto& buffer = std::get<LLUtils::Buffer>(result);
        buffer.Allocate(size);
        buffer.Write(reinterpret_cast<const std::byte*>(lockedMemory.data()), 0, size);
        std::get<ClipboardFormatType>(result) = selectedFormatID;
        return result;
    }
}
#endif
