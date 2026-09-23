#pragma once
#ifdef LWS_PLATFORM_WAYLAND
    #include <LWS/TextClipboard.hpp>
    #include <memory>
    #include <poll.h>
    #include <vector>
    #include <wayland-client.h>

namespace LWS::internal
{
    class WaylandPlatformState;
    // Clipboard transfers share the context's data device with URI drag-and-drop.
    class WaylandTextClipboard
    {
      public:

        explicit WaylandTextClipboard(WaylandPlatformState& platform);
        ~WaylandTextClipboard();
        void setDevice(wl_data_device_manager* manager, wl_data_device* device);
        void setSelection(wl_data_offer* offer, bool utf8, bool plain);
        ClipboardResult setText(Window& owner, std::string_view text);
        void requestText(Window& owner, ClipboardTextCallback callback);
        void process();
        void appendPollDescriptors(std::vector<pollfd>& descriptors, int& timeoutMilliseconds) const;
        void reset();

      private:

        struct State;
        std::unique_ptr<State> state_;
    };
}  // namespace LWS::internal
#endif
