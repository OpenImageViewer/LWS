#pragma once
#include <LWS/interfaces/backends.hpp>
#include <filesystem>

namespace LWS
{
    namespace Platform
    {
        void init();
        void shutdown();
        void runMessageLoop();
        bool processMessages();

        bool isKeyPressed(KeyCode key);
        bool isKeyToggled(KeyCode key);
        Point getMousePosition();
        void moveMouse(Point delta);

        void browseToFile(const std::filesystem::path& path);
    }
}
