#pragma once

#ifdef LWS_PLATFORM_WAYLAND

    #include <cstdint>
    #include <string>

    #include <wayland-client.h>

namespace LWS
{
    class WindowBackendWayland;
}

namespace LWS::internal
{
    class WaylandPlatformState;

    class WaylandDragAndDropController
    {
      public:

        explicit WaylandDragAndDropController(WaylandPlatformState& platform);

        void bindManager(wl_registry* registry, uint32_t name, uint32_t version);
        void setSeat(wl_seat* seat);
        [[nodiscard]] bool supported() const { return fDataDevice != nullptr; }
        [[nodiscard]] int pollDescriptor() const { return fDropDescriptor; }
        void processEvents(short events);
        void windowRemoved(WindowBackendWayland& window);
        void reset();

      private:

        static void dataOffer(void* data, wl_data_device* device, wl_data_offer* offer);
        static void dataOfferMimeType(void* data, wl_data_offer* offer, const char* mimeType);
        static void dataOfferSourceActions(void* data, wl_data_offer* offer, uint32_t actions);
        static void dataOfferAction(void* data, wl_data_offer* offer, uint32_t action);
        static void dataDeviceEnter(void* data, wl_data_device* device, uint32_t serial, wl_surface* surface,
                                    wl_fixed_t x, wl_fixed_t y, wl_data_offer* offer);
        static void dataDeviceLeave(void* data, wl_data_device* device);
        static void dataDeviceMotion(void* data, wl_data_device* device, uint32_t time, wl_fixed_t x, wl_fixed_t y);
        static void dataDeviceDrop(void* data, wl_data_device* device);
        static void dataDeviceSelection(void* data, wl_data_device* device, wl_data_offer* offer);

        void createDataDevice();
        void releaseDataDevice();
        void clearDragOffer();
        void clearDropTransfer();

        WaylandPlatformState& fPlatform;
        wl_data_device_manager* fDataDeviceManager = nullptr;
        wl_data_device* fDataDevice = nullptr;
        wl_data_offer* fPendingDataOffer = nullptr;
        wl_data_offer* fDragDataOffer = nullptr;
        wl_data_offer* fTransferDataOffer = nullptr;
        wl_seat* fSeat = nullptr;
        WindowBackendWayland* fDragWindow = nullptr;
        WindowBackendWayland* fTransferWindow = nullptr;
        bool fPendingOfferHasUriList = false;
        bool fDragOfferHasUriList = false;
        uint32_t fDragAction = WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE;
        int fDropDescriptor = -1;
        std::string fDropData;
    };
}  // namespace LWS::internal

#endif
