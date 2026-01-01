/*
main.cpp
Init function for the the DLL
*/

#define ENABLE_DEBUG_CONSOLE 1

#include <windows.h>
#include <stdio.h>
#include "dwmcore.hpp"

BOOL WINAPI DllMain(HMODULE hDll, DWORD dwReason, LPVOID pReserved)
{
    switch (dwReason)
    {
        case DLL_PROCESS_ATTACH:
            if (ENABLE_DEBUG_CONSOLE)
            {
                AllocConsole();
                SetConsoleTitleA("DWM Overlay Console");
                freopen("CONOUT$", "w", stderr);
            }
            if (!DwmCore::Init())
            {
                return FALSE;
            }
            fprintf(stderr, "injected or something\n");
            break;

        case DLL_THREAD_ATTACH:
            break;

        case DLL_THREAD_DETACH:
            break;

        case DLL_PROCESS_DETACH:
            break;
    }
    return TRUE;  // Successful DLL_PROCESS_ATTACH.
}