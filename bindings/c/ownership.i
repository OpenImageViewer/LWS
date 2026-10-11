// SWIG sees standard-library values as opaque objects.
namespace std {
    template<class T, class E> class expected;
}
%feature("valuewrapper") std::expected<LWS::Cursor, LWS::Result>;
%feature("valuewrapper") std::expected<LWS::WindowIcon, LWS::Result>;
%feature("valuewrapper") std::expected<LWS::EventConnection, LWS::Result>;
%typemap(out) SWIGTYPE "$result = (SwigObj*)new $1_ltype(SWIG_STD_MOVE(*(&$1)));"
%typemap(in) std::move_only_function<void()> "$1 = std::move(*($1_ltype *)$input);"
%typemap(in) LWS::UnhandledExceptionHandler "$1 = std::move(*($1_ltype *)$input);"
%typemap(in) LWS::EventCallback "$1 = std::move(*($1_ltype *)$input);"
%typemap(in) std::unique_ptr<LWSUI::Control> "$1 = std::move(*($1_ltype *)$input);"
%typemap(in) std::unique_ptr<LWSUI::MenuBar> "$1 = std::move(*($1_ltype *)$input);"
%typemap(in) LWS::Win32::PlatformCallback "$1 = std::move(*($1_ltype *)$input);"

// Resolve native Windows strings without parsing the external utility headers.
namespace std { class wstring; }
namespace LLUtils {
    using native_string_type = std::wstring;
    using native_char_type = wchar_t;
}
%naturalvar std::wstring;
%insert("cheader") %{
#include <wchar.h>
%}
%insert("header") %{
static wchar_t* lws_c_copy_wide(const std::wstring& text)
{
    const auto bytes = (text.size() + 1) * sizeof(wchar_t);
    auto* copy = static_cast<wchar_t*>(std::malloc(bytes));
    if (copy) std::memcpy(copy, text.c_str(), bytes);
    return copy;
}
%}
%typemap(ctype) std::wstring, const std::wstring & "const wchar_t *"
%typemap(in) std::wstring "$1 = $input ? $input : L\"\";"
%typemap(in) const std::wstring & (std::wstring temporary) {
    temporary = $input ? $input : L"";
    $1 = &temporary;
}
%typemap(out) std::wstring "$result = lws_c_copy_wide(*(&$1));"
%typemap(out) const std::wstring & "$result = lws_c_copy_wide(*$1);"
