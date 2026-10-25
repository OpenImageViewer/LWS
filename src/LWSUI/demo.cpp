#include "demo/Showcase.hpp"
#include <exception>
#include <string_view>

int main(int argc, char** argv)
{
    try
    {
        return LWSUI::demo::RunShowcase(argc, argv);
    }
    catch (const std::exception& error)
    {
        bool interactive = true;
        for (int i = 1; i < argc; ++i)
            if (std::string_view(argv[i]) == "--smoke" || std::string_view(argv[i]) == "--snapshot" ||
                std::string_view(argv[i]) == "--snapshot-narrow")
                interactive = false;
        LWSUI::demo::ReportShowcaseError(error.what(), interactive);
        return 1;
    }
}
