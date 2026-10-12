#ifndef LWS_C_SUPPORT_H
#define LWS_C_SUPPORT_H
#include "lws_c_wrap.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef void (*lws_c_callback)(void* user);
typedef struct lws_c_event_connection lws_c_event_connection;

// Delete returned strings with this function in the allocating runtime.
void lws_c_string_free(const char* text);
void lws_c_wstring_free(const wchar_t* text);

// PostTask moves the callable. Delete its now-empty handle after posting.
SWIGTYPE_p_std__move_only_functionT_void_fF_t* lws_c_task_new(lws_c_callback callback, void* user);
void lws_c_task_delete(SWIGTYPE_p_std__move_only_functionT_void_fF_t* task);

// Event getters are borrowed. Connections own registrations and may outlive publishers.
lws_c_event_connection* lws_c_event0_connect(SWIGTYPE_p_LWSUI__EventT_void_fF_t* event, lws_c_callback callback, void* user);
void lws_c_event_connection_delete(lws_c_event_connection* connection);
void lws_c_event0_raise(SWIGTYPE_p_LWSUI__EventT_void_fF_t* event);

// Adopt a standalone button exactly once. Do not delete the raw button after adoption.
SWIGTYPE_p_std__unique_ptrT_LWSUI__Control_t* lws_c_button_take(LWSUI_Button* button);
// The same adoption rules apply to labels, text boxes, and containers.
SWIGTYPE_p_std__unique_ptrT_LWSUI__Control_t* lws_c_label_take(LWSUI_Label* label);
SWIGTYPE_p_std__unique_ptrT_LWSUI__Control_t* lws_c_text_box_take(LWSUI_TextBox* textBox);
SWIGTYPE_p_std__unique_ptrT_LWSUI__Control_t* lws_c_stack_panel_take(LWSUI_StackPanel* panel);
SWIGTYPE_p_std__unique_ptrT_LWSUI__Control_t* lws_c_panel_take(LWSUI_Panel* panel);
void lws_c_window_config_set_styles(LWS_WindowConfig* config, unsigned int styles);
// Post a normal Win32 close request for processing by the message loop.
enum Result lws_c_window_request_close(LWS_Window* window);
LWSUI_Control* lws_c_control_owner_get(SWIGTYPE_p_std__unique_ptrT_LWSUI__Control_t* owner);
void lws_c_control_owner_delete(SWIGTYPE_p_std__unique_ptrT_LWSUI__Control_t* owner);

// Called on close requests and native destruction. Close requests are handled so
// the caller can leave the message loop and destroy the host before its window.
SWIGTYPE_p_std__move_only_functionT_LWS__EventResponse_fLWS__AnyEvent_const_RF_t* lws_c_window_close_callback_new(lws_c_callback callback, void* user);

// Window Listen moves the callback. Delete its empty handle after registration.
SWIGTYPE_p_std__move_only_functionT_LWS__EventResponse_fLWS__AnyEvent_const_RF_t* lws_c_window_callback_new(lws_c_callback callback, void* user);
void lws_c_window_callback_delete(SWIGTYPE_p_std__move_only_functionT_LWS__EventResponse_fLWS__AnyEvent_const_RF_t* callback);
bool lws_c_listen_result_ok(SWIGTYPE_p_std__expectedT_LWS__EventConnection_LWS__Result_t* result);
// Borrowed from the result until result deletion.
LWS_EventConnection* lws_c_listen_result_connection(SWIGTYPE_p_std__expectedT_LWS__EventConnection_LWS__Result_t* result);
enum Result lws_c_listen_result_error(SWIGTYPE_p_std__expectedT_LWS__EventConnection_LWS__Result_t* result);
void lws_c_listen_result_delete(SWIGTYPE_p_std__expectedT_LWS__EventConnection_LWS__Result_t* result);

#ifdef __cplusplus
}
#endif
#endif
