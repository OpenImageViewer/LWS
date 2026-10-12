#include "lws_c_support.h"
#include <stdio.h>
#include <string.h>

struct Greeting {
    LWSUI_TextBox* name;
    LWSUI_Label* label;
    unsigned clicks;
};

static void say_hello(void* user)
{
    struct Greeting* greeting = user;
    const char* name = LWSUI_TextBox_Text(greeting->name);
    char message[256];
    snprintf(message, sizeof(message), "Hello, %s!", name && *name ? name : "World");
    LWSUI_Label_SetText(greeting->label, message);
    lws_c_string_free(name);
    ++greeting->clicks;
}

static void quit(void* user) { LWS_PlatformContext_RequestQuit(user); }

// Add consumes the control. The empty ownership handle must still be freed.
static void add(LWSUI_StackPanel* stack, SWIGTYPE_p_std__unique_ptrT_LWSUI__Control_t* child)
{
    LWSUI_StackPanel_Add(stack, child);
    lws_c_control_owner_delete(child);
}

#define REQUIRE(expression) do { if (!(expression)) { fprintf(stderr, "Failed: %s\n", #expression); goto cleanup; } } while (0)

int main(void)
{
    int result = 1, initialized = 0;
    LWS_PlatformContext* platform = NULL;
    LWS_Window* window = NULL;
    LWSUI_UIHost* host = NULL;
    lws_c_event_connection* clicked = NULL;
    SWIGTYPE_p_std__expectedT_LWS__EventConnection_LWS__Result_t* closing = NULL;
    struct Greeting greeting = {0};

    REQUIRE(LWS_Win32_BootstrapProcess() == Result_Success);
    platform = LWS_PlatformContext_new();
    LWS_PlatformConfig* platform_config = LWS_PlatformConfig_new();
    LWS_PlatformConfig_backend_set(platform_config, BackendId_Win32);
    enum Result initialization = LWS_PlatformContext_Init(platform, platform_config);
    LWS_PlatformConfig_delete(platform_config);
    REQUIRE(initialization == Result_Success);
    initialized = 1;

    window = LWS_Window_new(platform);
    LWS_WindowConfig* config = LWS_WindowConfig_new();
    LWS_LogicalSize* size = LWS_WindowConfig_clientSize_get(config);
    LWS_LogicalSize_x_set(size, 420);
    LWS_LogicalSize_y_set(size, 240);
    lws_c_window_config_set_styles(config, WindowStyle_Caption | WindowStyle_CloseButton | WindowStyle_ResizableBorder);
    LWS_WindowConfig_eraseBackground_set(config, false);
    enum Result creation = LWS_Window_Create_rcLWS_WindowConfig(window, config);
    LWS_WindowConfig_delete(config);
    REQUIRE(creation == Result_Success);
    REQUIRE(LWS_Window_SetTitle(window, L"LWSUI / Hello World in C") == Result_Success);

    host = LWSUI_UIHost_new_rLWS_Window(window);
    LWSUI_UIHost_SetRedrawOnResize(host, true);
    LWSUI_StackPanel* stack = LWSUI_StackPanel_new();
    LWSUI_StackPanel_spacing_set(stack, 12);
    greeting.label = LWSUI_Label_new_std_string("Hello, World!");
    greeting.name = LWSUI_TextBox_new();
    LWSUI_TextBox_SetPlaceholder(greeting.name, "Your name");
    LWSUI_Button* button = LWSUI_Button_new_std_string("Say hello");
    add(stack, lws_c_label_take(greeting.label));
    add(stack, lws_c_text_box_take(greeting.name));
    add(stack, lws_c_button_take(button));
    SWIGTYPE_p_std__unique_ptrT_LWSUI__Control_t* content = lws_c_stack_panel_take(stack);
    LWSUI_Panel* panel = LWSUI_Panel_new_std_unique_ptr_Sl_LWSUI_Control_Sg_(content);
    lws_c_control_owner_delete(content);
    LWSUI_Insets* padding = LWSUI_Insets_new_f(20);
    LWSUI_Panel_padding_set(panel, padding);
    LWSUI_Insets_delete(padding);
    content = lws_c_panel_take(panel);
    LWSUI_UIHost_SetRoot(host, content);
    lws_c_control_owner_delete(content);

    clicked = lws_c_event0_connect(LWSUI_Button_OnClick_get(button), say_hello, &greeting);
    REQUIRE(clicked != NULL);
    SWIGTYPE_p_std__move_only_functionT_LWS__EventResponse_fLWS__AnyEvent_const_RF_t* callback = lws_c_window_close_callback_new(quit, platform);
    closing = LWS_Window_Listen(window, callback);
    lws_c_window_callback_delete(callback);
    REQUIRE(lws_c_listen_result_ok(closing));
    REQUIRE(LWS_Window_SetVisible(window, true) == Result_Success);
    LWSUI_UIHost_Update(host);

    {
        REQUIRE(LWS_Window_IsVisible(window));
        LWSUI_TextBox_SetText(greeting.name, "C programmer");
        LWSUI_Rect* bounds = LWSUI_Button_Bounds(button);
        REQUIRE(LWSUI_Rect_width_get(bounds) > 0 && LWSUI_Rect_height_get(bounds) > 0);
        LWSUI_Input* input = LWSUI_Input_new();
        LWSUI_Input_x_set(input, LWSUI_Rect_x_get(bounds) + 5);
        LWSUI_Input_y_set(input, LWSUI_Rect_y_get(bounds) + 5);
        LWSUI_Input_kind_set(input, InputKind_Down);
        LWSUI_UIHost_Route(host, input);
        LWSUI_Input_kind_set(input, InputKind_Up);
        LWSUI_UIHost_Route(host, input);
        LWSUI_Input_delete(input);
        const char* text = LWSUI_Label_Text(greeting.label);
        int greeted = text && strcmp(text, "Hello, C programmer!") == 0;
        lws_c_string_free(text);
        REQUIRE(greeted && greeting.clicks == 1);
        LWSUI_UIHost_Update(host);
        REQUIRE(lws_c_window_request_close(window) == Result_Success);
    }
    REQUIRE(LWS_PlatformContext_RunMessageLoop(platform) == LoopResult_Quit);
    result = 0;

cleanup:
    lws_c_event_connection_delete(clicked);
    lws_c_listen_result_delete(closing);
    if (host) LWSUI_UIHost_delete(host);
    if (window) {
        if (LWS_Window_IsCreated(window)) LWS_Window_Destroy(window);
        LWS_Window_delete(window);
    }
    if (initialized && LWS_PlatformContext_Shutdown(platform) != Result_Success) result = 1;
    if (platform) LWS_PlatformContext_delete(platform);
    return result;
}
