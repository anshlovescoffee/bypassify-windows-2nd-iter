/*
client.hpp
Definition of main imgui client layer for the overlay
*/

#pragma once

#include <d3d11_1.h>
#include <windows.h>
#include <mutex>
#include <string>
#include <vector>
#include "imgui.h"

namespace Client
{
    enum class CLIENT_STATUS
    {
        SUCCESS,
        FAILURE,
        INVALID_PARAM,
        ALREADY_DONE
    };

    extern std::mutex g_clientLock;

    CLIENT_STATUS Initialize(ID3D11Device1* pDevice, float dpiScale);
    CLIENT_STATUS Uninitialize();
    CLIENT_STATUS NextFrame(ID3D11Resource* pDxBuffer);
    CLIENT_STATUS InputInitialize();
    CLIENT_STATUS InputUninitialize();
    
    void InputFunction();
    bool IsInitialized();
    bool IsInputBlockingEnabled();
    bool IsClipboardEnabled();
    BYTE GetKeyStateFromHook(int vkCode);
}