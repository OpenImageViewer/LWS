#include "lws_c_support.h"
#include <LWS/Window.hpp>
#include <LWS/Win32/WindowExtensions.hpp>
#include <LWSUI/Containers.hpp>
#include <cstdlib>
#include <memory>
#include <utility>

using Task = std::move_only_function<void()>;
using Owner = std::unique_ptr<LWSUI::Control>;
using ListenResult = std::expected<LWS::EventConnection, LWS::Result>;
struct lws_c_event_connection { LWSUI::Event<void()>::Connection registration; };

void lws_c_string_free(const char* text) { std::free(const_cast<char*>(text)); }
void lws_c_wstring_free(const wchar_t* text) { std::free(const_cast<wchar_t*>(text)); }

SWIGTYPE_p_std__move_only_functionT_void_fF_t* lws_c_task_new(lws_c_callback callback, void* user)
{
    if (!callback) return nullptr;
    return reinterpret_cast<SWIGTYPE_p_std__move_only_functionT_void_fF_t*>(new Task([callback, user] { callback(user); }));
}
void lws_c_task_delete(SWIGTYPE_p_std__move_only_functionT_void_fF_t* task)
{
    delete reinterpret_cast<Task*>(task);
}

lws_c_event_connection* lws_c_event0_connect(SWIGTYPE_p_LWSUI__EventT_void_fF_t* event, lws_c_callback callback, void* user)
{
    if (!event || !callback) return nullptr;
    auto& publisher = *reinterpret_cast<LWSUI::Event<void()>*>(event);
    return new lws_c_event_connection{publisher.Connect([callback, user] { callback(user); })};
}
void lws_c_event_connection_delete(lws_c_event_connection* connection) { delete connection; }
void lws_c_event0_raise(SWIGTYPE_p_LWSUI__EventT_void_fF_t* event)
{
    if (event) reinterpret_cast<LWSUI::Event<void()>*>(event)->Raise();
}

SWIGTYPE_p_std__unique_ptrT_LWSUI__Control_t* lws_c_button_take(LWSUI_Button* button)
{
    return reinterpret_cast<SWIGTYPE_p_std__unique_ptrT_LWSUI__Control_t*>(new Owner(reinterpret_cast<LWSUI::Button*>(button)));
}
LWSUI_Control* lws_c_control_owner_get(SWIGTYPE_p_std__unique_ptrT_LWSUI__Control_t* owner)
{
    return owner ? reinterpret_cast<LWSUI_Control*>(reinterpret_cast<Owner*>(owner)->get()) : nullptr;
}
void lws_c_control_owner_delete(SWIGTYPE_p_std__unique_ptrT_LWSUI__Control_t* owner)
{
    delete reinterpret_cast<Owner*>(owner);
}

SWIGTYPE_p_std__move_only_functionT_LWS__EventResponse_fLWS__AnyEvent_const_RF_t* lws_c_window_callback_new(lws_c_callback callback, void* user)
{
    if (!callback) return nullptr;
    auto* value = new LWS::EventCallback([callback, user](const LWS::AnyEvent&) {
        callback(user);
        return LWS::EventResponse::Unhandled;
    });
    return reinterpret_cast<SWIGTYPE_p_std__move_only_functionT_LWS__EventResponse_fLWS__AnyEvent_const_RF_t*>(value);
}
void lws_c_window_callback_delete(SWIGTYPE_p_std__move_only_functionT_LWS__EventResponse_fLWS__AnyEvent_const_RF_t* callback)
{
    delete reinterpret_cast<LWS::EventCallback*>(callback);
}
bool lws_c_listen_result_ok(SWIGTYPE_p_std__expectedT_LWS__EventConnection_LWS__Result_t* result)
{
    return result && reinterpret_cast<ListenResult*>(result)->has_value();
}
LWS_EventConnection* lws_c_listen_result_connection(SWIGTYPE_p_std__expectedT_LWS__EventConnection_LWS__Result_t* result)
{
    if (!lws_c_listen_result_ok(result)) return nullptr;
    return reinterpret_cast<LWS_EventConnection*>(&reinterpret_cast<ListenResult*>(result)->value());
}
enum Result lws_c_listen_result_error(SWIGTYPE_p_std__expectedT_LWS__EventConnection_LWS__Result_t* result)
{
    if (!result) return Result_InvalidArgument;
    const auto& value = *reinterpret_cast<ListenResult*>(result);
    return value ? Result_Success : static_cast<enum Result>(value.error());
}
void lws_c_listen_result_delete(SWIGTYPE_p_std__expectedT_LWS__EventConnection_LWS__Result_t* result)
{
    delete reinterpret_cast<ListenResult*>(result);
}

SWIGTYPE_p_std__unique_ptrT_LWSUI__Control_t* lws_c_label_take(LWSUI_Label* label)
{
    return reinterpret_cast<SWIGTYPE_p_std__unique_ptrT_LWSUI__Control_t*>(new Owner(reinterpret_cast<LWSUI::Label*>(label)));
}
SWIGTYPE_p_std__unique_ptrT_LWSUI__Control_t* lws_c_text_box_take(LWSUI_TextBox* textBox)
{
    return reinterpret_cast<SWIGTYPE_p_std__unique_ptrT_LWSUI__Control_t*>(new Owner(reinterpret_cast<LWSUI::TextBox*>(textBox)));
}
SWIGTYPE_p_std__unique_ptrT_LWSUI__Control_t* lws_c_stack_panel_take(LWSUI_StackPanel* panel)
{
    return reinterpret_cast<SWIGTYPE_p_std__unique_ptrT_LWSUI__Control_t*>(new Owner(reinterpret_cast<LWSUI::StackPanel*>(panel)));
}
SWIGTYPE_p_std__unique_ptrT_LWSUI__Control_t* lws_c_panel_take(LWSUI_Panel* panel)
{
    return reinterpret_cast<SWIGTYPE_p_std__unique_ptrT_LWSUI__Control_t*>(new Owner(reinterpret_cast<LWSUI::Panel*>(panel)));
}
void lws_c_window_config_set_styles(LWS_WindowConfig* config, unsigned int styles)
{
    reinterpret_cast<LWS::WindowConfig*>(config)->styles = LWS::WindowStyleFlags(static_cast<LWS::WindowStyle>(styles));
}
SWIGTYPE_p_std__move_only_functionT_LWS__EventResponse_fLWS__AnyEvent_const_RF_t* lws_c_window_close_callback_new(lws_c_callback callback, void* user)
{
    if (!callback) return nullptr;
    auto* value = new LWS::EventCallback([callback, user](const LWS::AnyEvent& event) {
        if (std::holds_alternative<LWS::EventCloseRequested>(event)) {
            callback(user);
            return LWS::EventResponse::Handled;
        }
        if (std::holds_alternative<LWS::EventWindowDestroyed>(event)) callback(user);
        return LWS::EventResponse::Unhandled;
    });
    return reinterpret_cast<SWIGTYPE_p_std__move_only_functionT_LWS__EventResponse_fLWS__AnyEvent_const_RF_t*>(value);
}

enum Result lws_c_window_request_close(LWS_Window* window)
{
    if (!window) return Result_InvalidArgument;
    const auto handle = LWS::Win32::GetHwnd(*reinterpret_cast<LWS::Window*>(window));
    if (!handle) return static_cast<enum Result>(handle.error());
    return PostMessageW(*handle, WM_CLOSE, 0, 0) ? Result_Success : Result_Failure;
}
