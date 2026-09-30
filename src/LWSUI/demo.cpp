#include "demo/Showcase.hpp"
#include <exception>
#include <iostream>

int main(int argc, char** argv)
{
    try
    {
        return LWSUI::demo::RunShowcase(argc, argv);
    }
    catch (const std::exception& error)
    {
        std::cerr << "LWSUI showcase: " << error.what() << '\n';
        return 1;
    }
}
