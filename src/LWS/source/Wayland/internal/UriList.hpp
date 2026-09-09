#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace LWS::internal
{
    inline std::vector<std::filesystem::path> parseUriList(std::string_view text)
    {
        constexpr std::string_view filePrefix = "file://";
        constexpr std::string_view localHost = "localhost";
        const auto equalsIgnoreCase = [](std::string_view left, std::string_view right)
        {
            return left.size() == right.size() && std::ranges::equal(left, right, [](unsigned char a, unsigned char b)
                                                                     { return std::tolower(a) == std::tolower(b); });
        };
        const auto hexValue = [](char value) -> int
        {
            if (value >= '0' && value <= '9')
                return value - '0';
            if (value >= 'a' && value <= 'f')
                return value - 'a' + 10;
            if (value >= 'A' && value <= 'F')
                return value - 'A' + 10;
            return -1;
        };

        std::vector<std::filesystem::path> paths;
        while (!text.empty())
        {
            const size_t lineEnd = text.find('\n');
            std::string_view line = text.substr(0, lineEnd);
            text = lineEnd == std::string_view::npos ? std::string_view{} : text.substr(lineEnd + 1);
            if (line.ends_with('\r'))
                line.remove_suffix(1);
            if (line.empty() || line.front() == '#' || line.size() < filePrefix.size() ||
                !equalsIgnoreCase(line.substr(0, filePrefix.size()), filePrefix))
                continue;

            line.remove_prefix(filePrefix.size());
            if (!line.starts_with('/') && line.size() > localHost.size() &&
                equalsIgnoreCase(line.substr(0, localHost.size()), localHost))
            {
                line.remove_prefix(localHost.size());
            }
            if (line.empty() || line.front() != '/')
                continue;

            std::string path;
            path.reserve(line.size());
            bool valid = true;
            for (size_t index = 0; valid && index < line.size(); ++index)
            {
                if (line[index] != '%')
                {
                    valid = line[index] != '\0';
                    path.push_back(line[index]);
                    continue;
                }

                valid = index + 2 < line.size();
                if (valid)
                {
                    const int high = hexValue(line[index + 1]);
                    const int low = hexValue(line[index + 2]);
                    valid = high >= 0 && low >= 0;
                    if (valid)
                    {
                        const char decoded = static_cast<char>(high * 16 + low);
                        valid = decoded != '\0';
                        path.push_back(decoded);
                        index += 2;
                    }
                }
            }
            if (valid)
                paths.emplace_back(std::move(path));
        }
        return paths;
    }
}  // namespace LWS::internal
