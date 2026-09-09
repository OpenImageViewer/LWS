#ifdef LWS_PLATFORM_WAYLAND

    #include "WaylandDragAndDropController.hpp"

    #include "PlatformState.hpp"
    #include "UriList.hpp"

    #include <LWS/Wayland/WindowBackendWayland.hpp>

    #include <algorithm>
    #include <cerrno>
    #include <cstring>
    #include <fcntl.h>
    #include <poll.h>
    #include <utility>
    #include <unistd.h>
    #include <vector>

namespace LWS::internal
{
    WaylandDragAndDropController::WaylandDragAndDropController(WaylandPlatformState& platform) : fPlatform(platform) {}

    void WaylandDragAndDropController::bindManager(wl_registry* registry, uint32_t name, uint32_t version)
    {
        if (fDataDeviceManager != nullptr)
            return;
        fDataDeviceManager = static_cast<wl_data_device_manager*>(
            wl_registry_bind(registry, name, &wl_data_device_manager_interface,
                             std::min(version, uint32_t{WL_DATA_OFFER_SET_ACTIONS_SINCE_VERSION})));
        createDataDevice();
    }

    void WaylandDragAndDropController::setSeat(wl_seat* seat)
    {
        if (fSeat == seat)
            return;
        releaseDataDevice();
        fSeat = seat;
        createDataDevice();
    }

    void WaylandDragAndDropController::dataOffer(void* data, wl_data_device*, wl_data_offer* offer)
    {
        auto& controller = *static_cast<WaylandDragAndDropController*>(data);
        if (controller.fPendingDataOffer != nullptr)
            wl_data_offer_destroy(controller.fPendingDataOffer);
        controller.fPendingDataOffer = offer;
        controller.fPendingOfferHasUriList = false;
        static constexpr wl_data_offer_listener offerListener{
            .offer = dataOfferMimeType,
            .source_actions = dataOfferSourceActions,
            .action = dataOfferAction,
        };
        wl_data_offer_add_listener(offer, &offerListener, &controller);
    }

    void WaylandDragAndDropController::dataOfferMimeType(void* data, wl_data_offer* offer, const char* mimeType)
    {
        auto& controller = *static_cast<WaylandDragAndDropController*>(data);
        if (offer == controller.fPendingDataOffer && std::strcmp(mimeType, "text/uri-list") == 0)
            controller.fPendingOfferHasUriList = true;
    }

    void WaylandDragAndDropController::dataOfferSourceActions(void*, wl_data_offer*, uint32_t) {}

    void WaylandDragAndDropController::dataOfferAction(void* data, wl_data_offer* offer, uint32_t action)
    {
        auto& controller = *static_cast<WaylandDragAndDropController*>(data);
        if (offer == controller.fDragDataOffer)
            controller.fDragAction = action;
    }

    void WaylandDragAndDropController::dataDeviceEnter(void* data, wl_data_device*, uint32_t serial,
                                                       wl_surface* surface, wl_fixed_t, wl_fixed_t,
                                                       wl_data_offer* offer)
    {
        auto& controller = *static_cast<WaylandDragAndDropController*>(data);
        controller.clearDragOffer();
        controller.fDragDataOffer = offer;
        controller.fDragOfferHasUriList = offer == controller.fPendingDataOffer && controller.fPendingOfferHasUriList;
        controller.fPendingDataOffer = nullptr;
        controller.fPendingOfferHasUriList = false;
        if (WindowBackendWayland* window = controller.fPlatform.findWindow(surface); window != nullptr)
            controller.fDragWindow = window->dragDropTarget();

        if (offer == nullptr)
            return;

        const bool accepted = controller.fDropDescriptor < 0 && controller.fDragWindow != nullptr &&
                              controller.fDragOfferHasUriList;
        wl_data_offer_accept(offer, serial, accepted ? "text/uri-list" : nullptr);
        if (!accepted)
            controller.fDragWindow = nullptr;
        controller.fDragAction = wl_data_offer_get_version(offer) >= WL_DATA_OFFER_SET_ACTIONS_SINCE_VERSION
                                     ? WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE
                                     : WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY;
        if (accepted && wl_data_offer_get_version(offer) >= WL_DATA_OFFER_SET_ACTIONS_SINCE_VERSION)
        {
            wl_data_offer_set_actions(offer, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY,
                                      WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
        }
    }

    void WaylandDragAndDropController::dataDeviceLeave(void* data, wl_data_device*)
    {
        static_cast<WaylandDragAndDropController*>(data)->clearDragOffer();
    }

    void WaylandDragAndDropController::dataDeviceMotion(void*, wl_data_device*, uint32_t, wl_fixed_t, wl_fixed_t) {}

    void WaylandDragAndDropController::dataDeviceDrop(void* data, wl_data_device*)
    {
        auto& controller = *static_cast<WaylandDragAndDropController*>(data);
        const bool accepted = controller.fDragDataOffer != nullptr && controller.fDragWindow != nullptr &&
                              controller.fDragOfferHasUriList &&
                              controller.fDragAction == WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY;
        if (!accepted)
        {
            controller.clearDragOffer();
            return;
        }

        int descriptors[2]{-1, -1};
        if (pipe2(descriptors, O_CLOEXEC) != 0 || fcntl(descriptors[0], F_SETFL, O_NONBLOCK) != 0)
        {
            if (descriptors[0] >= 0)
                close(descriptors[0]);
            if (descriptors[1] >= 0)
                close(descriptors[1]);
            controller.clearDragOffer();
            return;
        }

        wl_data_offer_receive(controller.fDragDataOffer, "text/uri-list", descriptors[1]);
        close(descriptors[1]);
        controller.fDropDescriptor = descriptors[0];
        controller.fTransferDataOffer = std::exchange(controller.fDragDataOffer, nullptr);
        controller.fTransferWindow = std::exchange(controller.fDragWindow, nullptr);
        controller.fDragOfferHasUriList = false;
    }

    void WaylandDragAndDropController::dataDeviceSelection(void* data, wl_data_device*, wl_data_offer* offer)
    {
        auto& controller = *static_cast<WaylandDragAndDropController*>(data);
        if (offer != nullptr)
            wl_data_offer_destroy(offer);
        if (offer == controller.fPendingDataOffer)
        {
            controller.fPendingDataOffer = nullptr;
            controller.fPendingOfferHasUriList = false;
        }
    }

    void WaylandDragAndDropController::createDataDevice()
    {
        if (fDataDevice == nullptr && fDataDeviceManager != nullptr && fSeat != nullptr)
        {
            fDataDevice = wl_data_device_manager_get_data_device(fDataDeviceManager, fSeat);
            static constexpr wl_data_device_listener deviceListener{
                .data_offer = dataOffer,
                .enter = dataDeviceEnter,
                .leave = dataDeviceLeave,
                .motion = dataDeviceMotion,
                .drop = dataDeviceDrop,
                .selection = dataDeviceSelection,
            };
            wl_data_device_add_listener(fDataDevice, &deviceListener, this);
        }
    }

    void WaylandDragAndDropController::clearDragOffer()
    {
        if (fDragDataOffer != nullptr)
            wl_data_offer_destroy(fDragDataOffer);
        fDragDataOffer = nullptr;
        fDragWindow = nullptr;
        fDragOfferHasUriList = false;
        fDragAction = WL_DATA_DEVICE_MANAGER_DND_ACTION_NONE;
    }

    void WaylandDragAndDropController::releaseDataDevice()
    {
        clearDragOffer();
        clearDropTransfer();
        if (fPendingDataOffer != nullptr)
            wl_data_offer_destroy(fPendingDataOffer);
        if (fDataDevice != nullptr)
        {
            if (wl_data_device_get_version(fDataDevice) >= WL_DATA_DEVICE_RELEASE_SINCE_VERSION)
                wl_data_device_release(fDataDevice);
            else
                wl_data_device_destroy(fDataDevice);
        }
        fPendingDataOffer = nullptr;
        fDataDevice = nullptr;
        fSeat = nullptr;
        fPendingOfferHasUriList = false;
    }

    void WaylandDragAndDropController::clearDropTransfer()
    {
        if (fDropDescriptor >= 0)
            close(fDropDescriptor);
        if (fTransferDataOffer != nullptr)
            wl_data_offer_destroy(fTransferDataOffer);
        fDropDescriptor = -1;
        fTransferDataOffer = nullptr;
        fTransferWindow = nullptr;
        fDropData.clear();
    }

    void WaylandDragAndDropController::processEvents(short events)
    {
        constexpr size_t MaxDropDataSize = 1024 * 1024;
        constexpr size_t MaxReadPerDispatch = 64 * 1024;
        if (fDropDescriptor < 0 || (events & (POLLIN | POLLHUP | POLLERR)) == 0)
            return;

        char buffer[4096];
        ssize_t readSize = 0;
        size_t readThisDispatch = 0;
        do
        {
            do
            {
                readSize = read(fDropDescriptor, buffer, sizeof(buffer));
            } while (readSize < 0 && errno == EINTR);
            if (readSize > 0)
            {
                fDropData.append(buffer, static_cast<size_t>(readSize));
                readThisDispatch += static_cast<size_t>(readSize);
            }
        } while (readSize > 0 && readThisDispatch < MaxReadPerDispatch && fDropData.size() <= MaxDropDataSize);

        if (fDropData.size() > MaxDropDataSize)
        {
            clearDropTransfer();
            return;
        }
        if (readSize > 0)
            return;
        if (readSize < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) && (events & (POLLHUP | POLLERR)) == 0)
            return;

        WindowBackendWayland* window = fTransferWindow;
        const std::vector<std::filesystem::path> paths = readSize == 0 ? parseUriList(fDropData)
                                                                       : std::vector<std::filesystem::path>{};
        if (readSize == 0 && wl_data_offer_get_version(fTransferDataOffer) >= WL_DATA_OFFER_FINISH_SINCE_VERSION)
            wl_data_offer_finish(fTransferDataOffer);
        clearDropTransfer();
        if (window != nullptr && window->fDragAndDropEnabled)
        {
            for (const std::filesystem::path& path : paths)
            {
                if (window->getHandle() == 0 || !window->fDragAndDropEnabled)
                    break;
                window->dispatchEvent(EventDragDropFile{path});
            }
        }
    }

    void WaylandDragAndDropController::windowRemoved(WindowBackendWayland& window)
    {
        if (fDragWindow == &window)
            clearDragOffer();
        if (fTransferWindow == &window)
            clearDropTransfer();
    }

    void WaylandDragAndDropController::reset()
    {
        releaseDataDevice();
        if (fDataDeviceManager != nullptr)
            wl_data_device_manager_destroy(fDataDeviceManager);
        fDataDeviceManager = nullptr;
    }
}  // namespace LWS::internal

#endif
