#pragma once
#include <LLUtils/StringDefs.h>
#include <string_view>

namespace LWS
{
    /// Platform-native string type: std::wstring on Windows, std::string on Linux.
    using string_type = LLUtils::native_string_type;
    using char_type = LLUtils::native_char_type;
    using string_view_type = std::basic_string_view<char_type>;
}
