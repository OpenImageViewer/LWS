#ifdef LWS_PLATFORM_X11

    #include <LWS/Clipboard.hpp>
    #include <LWS/FileDialog.hpp>
    #include <LWS/NotificationIconGroup.hpp>
    #include <LWS/Timer.hpp>
    #include <LWS/interfaces/backends.hpp>

    #include <memory>
    #include <utility>

namespace LWS
{
    void Clipboard::RegisterFormat(ClipboardFormatType format)
    {
        fListFormats.push_back(format);
    }
    ClipboardFormatType Clipboard::RegisterFormat(const string_type&)
    {
        return 0;
    }
    ClipboardResult Clipboard::SetClipboardData(Window&, ClipboardFormatType, const LLUtils::Buffer&)
    {
        return ClipboardResult::UnknownError;
    }
    ClipboardResult Clipboard::SetClipboardData(Window&, ClipboardFormatType, const std::byte*, size_t)
    {
        return ClipboardResult::UnknownError;
    }
    ClipboardResult Clipboard::SetClipboardData(Window&, std::span<const ClipboardDataView>)
    {
        return ClipboardResult::UnknownError;
    }
    ClipboardResult Clipboard::SetClipboardText(Window&, const char_type*)
    {
        return ClipboardResult::UnknownError;
    }
    ClipboardData Clipboard::GetClipboardData()
    {
        return {};
    }
    ClipboardResult Clipboard::GetClipboardError() const
    {
        return ClipboardResult::UnknownError;
    }

    FileDialogFilterBuilder::FileDialogFilterBuilder(const ListFileDialogFilters& filters) : fFilters(filters) {}
    const FileDialogFilterBuilder::ListFileDialogFilters& FileDialogFilterBuilder::GetFilters() const
    {
        return fFilters;
    }
    FileDialogResult FileDialog::Show(FileDialogType, const FileDialogFilterBuilder::ListFileDialogFilters&,
                                      const file_dialog_string_type&, Window&, const file_dialog_string_type&, uint32_t,
                                      file_dialog_string_type, file_dialog_string_type&)
    {
        return FileDialogResult::UnknownError;
    }
    FileDialogResult FileDialog::Show(FileDialogType, const FileDialogFilterBuilder::ListFileDialogFilters&,
                                      const file_dialog_string_type&, Window&, const file_dialog_string_type&, uint32_t,
                                      file_dialog_string_type, ListFileDialogFileNames&)
    {
        return FileDialogResult::UnknownError;
    }

    class NotificationIconGroup::Impl
    {
    };

    NotificationIconGroup::NotificationIconGroup(PlatformContext& platform)
        : platform_(platform), impl_(std::make_unique<Impl>())
    {
        platform_.RegisterService();
    }
    NotificationIconGroup::~NotificationIconGroup()
    {
        impl_.reset();
        platform_.UnregisterService();
    }
    NotificationIconGroup::IconID NotificationIconGroup::AddIconResource(uint16_t, const string_type&)
    {
        return 0;
    }
    Rect NotificationIconGroup::GetIconRect(IconID) const
    {
        return {};
    }
}  // namespace LWS

namespace LWS::internal
{
    class CursorBackendX11 final : public ICursorBackend
    {
      public:

        void setVisible(bool) override {}
        void setCursorShape(CursorShape) override {}
        Result setCustomCursor(const BitmapBuffer&, Point) override { return Result::NotSupported; }
        BackendId backend() const override { return BackendId::X11; }
    };

    class TimerBackendX11 final : public ITimerBackend
    {
      public:

        void setTargetWindow(Handle) override {}
        uint32_t getInterval() const override { return interval_; }
        void setInterval(uint32_t interval) override { interval_ = interval; }
        void setCallback(Callback callback) override { callback_ = std::move(callback); }

      private:

        uint32_t interval_{};
        Callback callback_;
    };

    class HighPrecisionTimerBackendX11 final : public IHighPrecisionTimerBackend
    {
      public:

        void setRepeatInterval(uint32_t) override {}
        void setDueTime(uint32_t) override {}
        bool getEnabled() const override { return enabled_; }
        void enable(bool enabled) override { enabled_ = enabled; }

      private:

        bool enabled_{};
    };

    std::unique_ptr<ICursorBackend> createDefaultCursorBackend()
    {
        return std::make_unique<CursorBackendX11>();
    }
    std::unique_ptr<ITimerBackend> createTimerBackend(PlatformContext&)
    {
        return std::make_unique<TimerBackendX11>();
    }
    std::unique_ptr<IHighPrecisionTimerBackend> createHighPrecisionTimerBackend(PlatformContext&,
                                                                                ITimerBackend::Callback)
    {
        return std::make_unique<HighPrecisionTimerBackendX11>();
    }
}  // namespace LWS::internal

#endif
