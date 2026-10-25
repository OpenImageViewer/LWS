#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace LWS::internal
{
    inline std::optional<std::string> parseFileUri(std::string_view uri)
    {
        const auto equal = [](std::string_view a, std::string_view b)
        {
            return a.size() == b.size() && std::ranges::equal(a, b, [](unsigned char x, unsigned char y)
                                                              { return std::tolower(x) == std::tolower(y); });
        };
        if (uri.size() < 7 || !equal(uri.substr(0, 7), "file://"))
            return {};
        uri.remove_prefix(7);
        if (uri.size() >= 10 && equal(uri.substr(0, 9), "localhost") && uri[9] == '/')
            uri.remove_prefix(9);
        if (uri.empty() || uri.front() != '/')
            return {};
        auto hex = [](char c) -> int
        {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
            if (c >= 'A' && c <= 'F')
                return c - 'A' + 10;
            return -1;
        };
        std::string path;
        path.reserve(uri.size());
        for (size_t i = 0; i < uri.size(); ++i)
        {
            char c = uri[i];
            if (c == '%')
            {
                if (i + 2 >= uri.size())
                    return {};
                const int high = hex(uri[i + 1]), low = hex(uri[i + 2]);
                if (high < 0 || low < 0)
                    return {};
                c = char(high * 16 + low);
                i += 2;
            }
            if (!c)
                return {};
            path += c;
        }
        return path;
    }
    inline std::vector<std::filesystem::path> parseUriList(std::string_view text)
    {
        std::vector<std::filesystem::path> paths;
        while (!text.empty())
        {
            const size_t end = text.find('\n');
            auto line = text.substr(0, end);
            text = end == text.npos ? std::string_view{} : text.substr(end + 1);
            if (line.ends_with('\r'))
                line.remove_suffix(1);
            if (auto path = parseFileUri(line))
                paths.emplace_back(std::move(*path));
        }
        return paths;
    }
}  // namespace LWS::internal
