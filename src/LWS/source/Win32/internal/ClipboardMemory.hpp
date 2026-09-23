#pragma once
#include <Windows.h>
#include <span>
#include <cstddef>
#include <cstring>
#include <utility>

namespace LWS::internal
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
}  // namespace LWS::internal
