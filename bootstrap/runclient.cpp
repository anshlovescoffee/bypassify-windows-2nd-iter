/*
runclient.cpp
Launcher for the dwm overlay, manually mapping the overlay into memory
*/

#include "shared.hpp"

int main()
{
    char currentDirectory[MAX_PATH];
    INJECTCONFIG injectConfig = {1, 1, 1};
    FixCwd();
    if (!GetCurrentDirectoryA(MAX_PATH, currentDirectory))
    {
        std::cout << "failed to get current directory" << std::endl;
        return 1;
    }
    if (Inject("dwm.exe", std::string(currentDirectory) + "\\client.dll", injectConfig))
    {
        std::cout << "unable to load client" << std::endl;
        return 1;
    }
    std::cout << "client loaded" << std::endl;
    return 0;
}