#include "Game.hpp"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace
{
    std::filesystem::path executableDirectory(const char* programName)
    {
#ifdef _WIN32
        std::wstring path(MAX_PATH, L'\0');
        for (;;)
        {
            const auto count = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
            if (count == 0)
                break;
            if (count < path.size())
            {
                path.resize(count);
                return std::filesystem::path(path).parent_path();
            }
            path.resize(path.size() * 2);
        }
#endif
        return std::filesystem::absolute(programName).parent_path();
    }
}

int main(int argc, char* argv[])
{
    try
    {
        const auto assets = executableDirectory(argv[0]) / "assets" / "arcade";
        const bool startMuted = argc > 1 && std::string_view(argv[1]) == "--mute";
        Game game(assets, startMuted);
        game.run();
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Error al iniciar el juego: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
