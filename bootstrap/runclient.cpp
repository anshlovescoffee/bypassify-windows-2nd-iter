/*
runclient.cpp
Launcher for the dwm overlay, manually mapping the overlay into memory
Checks HWID authorization before injecting
*/

#include "shared.hpp"
#include "auth.hpp"

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
    
    // HWID authorization check
    Auth::AuthResult authResult = Auth::CheckAuthorization();
    
    if (authResult == Auth::AuthResult::ConnectionError)
    {
        MessageBoxA(NULL,
            "Connection Error\n\n"
            "Unable to reach the authorization server.\n"
            "Please check your internet connection and try again.",
            "Bypassify - Connection Error",
            MB_OK | MB_ICONERROR);
        return 1;
    }
    
    if (authResult == Auth::AuthResult::NotAuthorized)
    {
        std::string hwid = Auth::GetHardwareId();
        
        // Copy HWID to clipboard
        if (OpenClipboard(NULL))
        {
            EmptyClipboard();
            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, hwid.size() + 1);
            if (hMem)
            {
                memcpy(GlobalLock(hMem), hwid.c_str(), hwid.size() + 1);
                GlobalUnlock(hMem);
                SetClipboardData(CF_TEXT, hMem);
            }
            CloseClipboard();
        }
        
        std::string msg = "This device is not authorized.\n\n"
                          "Your HWID has been copied to your clipboard:\n" + hwid + "\n\n"
                          "Please send this HWID to an administrator to get access.";
        
        MessageBoxA(NULL, msg.c_str(), "Bypassify - Not Authorized", MB_OK | MB_ICONWARNING);
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