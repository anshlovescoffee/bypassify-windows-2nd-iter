/*
runclient.cpp
Launcher for the dwm overlay, manually mapping the overlay into memory
*/

#pragma comment(linker, "/SUBSYSTEM:windows /ENTRY:mainCRTStartup")

#include "shared.hpp"

int main()
{
    // Check if overlay is already running by trying to open the reactivate event
    // Use Global\ prefix for cross-session access (dwm.exe runs in Session 0)
    HANDLE hEvent = OpenEventA(EVENT_MODIFY_STATE, FALSE, "Global\\RedactedOverlayReactivate");
    if (hEvent)
    {
        // Already running - signal it to reactivate and exit
        SetEvent(hEvent);
        CloseHandle(hEvent);
        return 0;
    }
    
    // Not running - inject the DLL
    char currentDirectory[MAX_PATH];
    INJECTCONFIG injectConfig = {1, 1, 1};
    FixCwd();
    if (!GetCurrentDirectoryA(MAX_PATH, currentDirectory))
    {
        return 1;
    }
    if (Inject("dwm.exe", std::string(currentDirectory) + "\\client.dll", injectConfig))
    {
        return 1;
    }
    return 0;
}