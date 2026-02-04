/*
main.cpp
Init function for the the DLL
*/

#define ENABLE_DEBUG_CONSOLE 0

#include <windows.h>
#include <stdio.h>
#include "dwmcore.hpp"
#include "auth.hpp"

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
            
            // Auth check inside DLL - prevents manual injection bypass
            if (!Auth::IsAuthorized())
            {
                fprintf(stderr, "Authorization failed. Refusing to load.\n");
                return FALSE;
            }
            fprintf(stderr, "Authorization verified.\n");
            
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