#include "lws_c_support.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { fprintf(stderr, "Failed at line %d: %s\n", __LINE__, #expression); exit(1); } } while (0)

static void count_call(void* user) { ++*(unsigned*)user; }

int main(void)
{
    LWSUI_Rect* rect = LWSUI_Rect_new();
    LWSUI_Rect_x_set(rect, 10);
    LWSUI_Rect_y_set(rect, 20);
    LWSUI_Rect_width_set(rect, 100);
    LWSUI_Rect_height_set(rect, 50);
    CHECK(LWSUI_Rect_Contains(rect, 11, 21));
    CHECK(!LWSUI_Rect_Contains(rect, 110, 21));
    LWSUI_Rect_delete(rect);
    CHECK(strcmp(LWSUI_ThemePresetName(ThemePreset_Light), "light") == 0);

    LWSUI_Button* button = LWSUI_Button_new();
    LWSUI_Button_SetText(button, "C wrapper button");
    const char* text = LWSUI_Button_Text(button);
    CHECK(strcmp(text, "C wrapper button") == 0);
    lws_c_string_free(text);
    CHECK(LWSUI_Button_Enabled(button));
    LWSUI_Button_SetEnabled(button, false);
    CHECK(!LWSUI_Button_Enabled(button));
    CHECK(LWSUI_Button_OnClick_get(button) != NULL);
    unsigned clicks = 0;
    SWIGTYPE_p_LWSUI__EventT_void_fF_t* event = LWSUI_Button_OnClick_get(button);
    lws_c_event_connection* click_connection = lws_c_event0_connect(event, count_call, &clicks);
    CHECK(click_connection != NULL);
    lws_c_event0_raise(event);
    CHECK(clicks == 1);
    lws_c_event_connection_delete(click_connection);
    lws_c_event0_raise(event);
    CHECK(clicks == 1);

    LWSUI_Container* container = LWSUI_Container_new();
    SWIGTYPE_p_std__unique_ptrT_LWSUI__Control_t* owner = lws_c_button_take(button);
    LWSUI_Control* child = lws_c_control_owner_get(owner);
    CHECK(LWSUI_Container_Add(container, owner) == child);
    CHECK(lws_c_control_owner_get(owner) == NULL);
    lws_c_control_owner_delete(owner);
    owner = LWSUI_Container_Remove(container, child);
    CHECK(lws_c_control_owner_get(owner) == child);
    lws_c_control_owner_delete(owner);
    LWSUI_Container_delete(container);

    CHECK(LWS_Win32_BootstrapProcess() == Result_Success);
    LWS_PlatformContext* context = LWS_PlatformContext_new();
    LWS_PlatformConfig* config = LWS_PlatformConfig_new();
    LWS_PlatformConfig_backend_set(config, BackendId_Win32);
    CHECK(!LWS_PlatformContext_IsUsable(context));
    CHECK(LWS_PlatformContext_Init(context, config) == Result_Success);
    CHECK(LWS_PlatformContext_IsUsable(context));
    CHECK(LWS_PlatformContext_IsCurrentThread(context));
    LWS_PlatformConfig_delete(config);

    unsigned tasks = 0;
    SWIGTYPE_p_std__move_only_functionT_void_fF_t* task = lws_c_task_new(count_call, &tasks);
    CHECK(LWS_PlatformContext_PostTask(context, task) == Result_Success);
    lws_c_task_delete(task);
    CHECK(tasks == 0);
    CHECK(LWS_PlatformContext_ProcessMessages(context) == LoopResult_Continue);
    CHECK(tasks == 1);

    LWS_Window* window = LWS_Window_new(context);
    CHECK(LWS_Window_Create(window) == Result_Success);
    CHECK(LWS_Window_IsCreated(window));
    CHECK(LWS_Window_SetTitle(window, L"C wrapper wide title") == Result_Success);
    const wchar_t* title = LWS_Window_GetTitle(window);
    CHECK(wcscmp(title, L"C wrapper wide title") == 0);
    lws_c_wstring_free(title);
    CHECK(LWS_PlatformContext_ProcessMessages(context) == LoopResult_Continue);
    unsigned window_events = 0;
    SWIGTYPE_p_std__move_only_functionT_LWS__EventResponse_fLWS__AnyEvent_const_RF_t* callback = lws_c_window_callback_new(count_call, &window_events);
    SWIGTYPE_p_std__expectedT_LWS__EventConnection_LWS__Result_t* registration = LWS_Window_Listen(window, callback);
    lws_c_window_callback_delete(callback);
    CHECK(lws_c_listen_result_ok(registration));
    CHECK(lws_c_listen_result_error(registration) == Result_Success);
    LWS_EventConnection* connection = lws_c_listen_result_connection(registration);
    CHECK(LWS_EventConnection_IsConnected(connection));
    CHECK(LWS_Window_Destroy(window) == Result_Success);
    CHECK(window_events > 0);
    CHECK(!LWS_EventConnection_IsConnected(connection));
    lws_c_listen_result_delete(registration);
    callback = lws_c_window_callback_new(count_call, &window_events);
    registration = LWS_Window_Listen(window, callback);
    lws_c_window_callback_delete(callback);
    CHECK(!lws_c_listen_result_ok(registration));
    CHECK(lws_c_listen_result_error(registration) == Result_InvalidState);
    CHECK(lws_c_listen_result_connection(registration) == NULL);
    lws_c_listen_result_delete(registration);
    CHECK(!LWS_Window_IsCreated(window));
    LWS_Window_delete(window);
    CHECK(LWS_PlatformContext_Shutdown(context) == Result_Success);
    LWS_PlatformContext_delete(context);
    puts("PASS: C calls, strings, events, ownership transfer, move-only tasks and results, native window lifecycle");
    return 0;
}
