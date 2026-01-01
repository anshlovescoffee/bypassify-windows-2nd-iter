/*
dwmcore.cpp
Provides dwmcore hooking services to the client
*/

#define WIN32_LEAN_AND_MEAN

#include <stdio.h>
#include <windows.h>
#include "MinHook.h"
#include "dwmcore.hpp"
#include "client.hpp"
#include "offsets.hpp"

using namespace DwmCore;

#define CLIENT_FALLBACK_SCALING 1.0f
#define CLIENT_TARGET_VIRTUAL_HEIGHT 900.0f

uintptr_t dwmcoreBase = 0; // address where dwmcore was loaded
ProtoPresent OriginalPresent = nullptr;
ProtoPresentNeeded OriginalPresentNeeded1 = nullptr;
ProtoPresentNeeded OriginalPresentNeeded2 = nullptr;
ProtoScheduleCompositionPass ScheduleCompositionPass = nullptr;
void* OriginalIsOverlayPrevented = nullptr;

// hooking COverlayContext::Present as overlay context has info on what monitor a swapchain belongs to
HRESULT __fastcall HookPresent(COverlayContext* pSelf, DwmSwapChain* pDwmSwapChain, unsigned int a3, __int64 a4, unsigned int a5, bool* a6, bool disableMPO)
{
    if (pDwmSwapChain)
    {
        CD3DDevice* pDwmDevice = GetDevice(pDwmSwapChain);
        ISwapChainBuffer* pDwmBuffer = GetPhysicalBackBuffer(pDwmSwapChain);
        ID3D11Device1* pDxDevice = nullptr; // DO NOT RELEASE THE DEVICE AS TRAVERSING THE DWM DEVICE TO GET THE D3D DEVICE DOES NOT INCREASE REF COUNT
        ID3D11Resource* pDxBuffer = nullptr;
        ID3D11Texture2D* pDxTexture = nullptr;
        IOverlayMonitorTarget* pMonitorTarget = nullptr;
        D3D11_TEXTURE2D_DESC dxTextureDesc = {};
        float dpiScaling = 1; // fall back to a scale of 1 if unable to query the scale of the monitor

        if (pDwmDevice)
        {
            pDxDevice = GetD3D11Device(pDwmDevice);
        }
        if (pDwmBuffer)
        {
            pDxBuffer = GetD3D11Resource(pDwmBuffer);
        }

        if (pSelf)
        {
            pMonitorTarget = GetOverlayMonitorTarget(pSelf);
            if (pMonitorTarget && IsPrimaryMonitor(pMonitorTarget))
            {
                std::lock_guard<std::mutex> guard(Client::g_clientLock);
                
                if (!Client::IsInitialized())
                {
                    fprintf(stderr, "Initializing client\n");
                    pDxBuffer->QueryInterface(__uuidof(ID3D11Texture2D), (void**)(&pDxTexture));
                    if (pDxTexture)
                    {
                        pDxTexture->GetDesc(&dxTextureDesc);
                        dpiScaling = dxTextureDesc.Height / CLIENT_TARGET_VIRTUAL_HEIGHT;
                        pDxTexture->Release();
                    }
                    Client::Initialize(pDxDevice, dpiScaling);
                }
                Client::NextFrame(pDxBuffer);
            }
        }
        
        if (pDxBuffer)
        {
            pDxBuffer->Release();
        }
    }
    return OriginalPresent(pSelf, pDwmSwapChain, a3, a4, a5, a6, disableMPO);
}

bool __fastcall HookPresentNeeded1(void* pSelf)
{
    bool result = OriginalPresentNeeded1(pSelf);
    ScheduleCompositionPass(0, 0xFFFFFFFF); // you need to do tell DWM explicitly to draw a new frame
    result = true;
    return result;
}

bool __fastcall HookPresentNeeded2(void* pSelf)
{
    bool result = OriginalPresentNeeded2(pSelf);
    ScheduleCompositionPass(0, 0xFFFFFFFF); // you need to do tell DWM explicitly to draw a new frame
    result = true;
    return result;
}

bool __fastcall HookIsOverlayPrevented(void* pSelf, __int64 a2)
{
    return true;
}

int modCounter = 0;
void DwmCore::TestDraw(ID3D11Device1* pDxDevice, ID3D11Resource* pDxBuffer)
{
    if (!pDxDevice || !pDxBuffer)
    {
        return;
    }
    if (modCounter == 0)
    {
        ID3D11RenderTargetView* pRenderTargetView = nullptr;
        ID3D11DeviceContext1* pDeviceContext = nullptr;
        const float testColor[4] = {0.1f, 0.1f, 0.1f, 0.1f};

        pDxDevice->CreateRenderTargetView(pDxBuffer, NULL, &pRenderTargetView);
        pDxDevice->GetImmediateContext1(&pDeviceContext);

        if (pRenderTargetView && pDeviceContext)
        {
            pDeviceContext->ClearRenderTargetView(pRenderTargetView, testColor);
        }
        if (pRenderTargetView)
        {
            pRenderTargetView->Release();
        }
        if (pDeviceContext)
        {
            pDeviceContext->Release();
        }
    }
    ++modCounter;
    modCounter %= 6;
}

bool DwmCore::Init()
{
    dwmcoreBase = reinterpret_cast<uintptr_t>(GetModuleHandleA("dwmcore.dll"));
    ScheduleCompositionPass = reinterpret_cast<ProtoScheduleCompositionPass>(dwmcoreBase + OffsetScheduleCompositionPass);
    *(bool*)(dwmcoreBase + OffsetForceDirtyRendering) = true;
    if (MH_Initialize() != MH_OK)
    {
        fprintf(stderr, "Unable to initialize MinHook\n");
        return false;
    }
    if (MH_CreateHook((void*)(dwmcoreBase + OffsetPresent), (void*)HookPresent, (void**)(&OriginalPresent)) != MH_OK)
    {
        fprintf(stderr, "Unable to initialize COverlayContext::Present hook\n");
        return false;
    }
    if (MH_CreateHook((void*)(dwmcoreBase + OffsetPresentNeeded1), (void*)HookPresentNeeded1, (void**)(&OriginalPresentNeeded1)) != MH_OK)
    {
        fprintf(stderr, "Unable to initialize COverlayContext::PresentNeeded hook\n");
        return false;
    }
    if (MH_CreateHook((void*)(dwmcoreBase + OffsetPresentNeeded2), (void*)HookPresentNeeded2, (void**)(&OriginalPresentNeeded2)) != MH_OK)
    {
        fprintf(stderr, "Unable to initialize COverlayContext::PresentNeeded hook\n");
        return false;
    }
    if (MH_CreateHook((void*)(dwmcoreBase + OffsetIsOverlayPrevented), (void*)HookIsOverlayPrevented, (void**)(&OriginalIsOverlayPrevented)) != MH_OK)
    {
        fprintf(stderr, "Unable to initialize CGlobalCompositionSurfaceInfo::IsOverlayPrevented hook\n");
        return false;
    }
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK)
    {
        fprintf(stderr, "Unable to enable hooks\n");
        return false;
    }
    return true;
}

// vtable and internal struct related functions
CD3DDevice* DwmCore::GetDevice(DwmSwapChain* pSwapChain)
{
    uintptr_t vTable = *(uintptr_t*)(pSwapChain);
    ProtoGetDevice function = *(ProtoGetDevice*)(vTable + OffsetGetDevice);
    return function(pSwapChain);
}

ISwapChainBuffer* DwmCore::GetPhysicalBackBuffer(DwmSwapChain* pSwapChain)
{
    uintptr_t vTable = *(uintptr_t*)(pSwapChain);
    ProtoGetPhysicalBackBuffer function = *(ProtoGetPhysicalBackBuffer*)(vTable + OffsetGetPhysicalBackBuffer);
    return function(pSwapChain);
}

ID3D11Resource* DwmCore::GetD3D11Resource(ISwapChainBuffer* pSwapChainBuffer)
{
    uintptr_t vTable = *(uintptr_t*)(pSwapChainBuffer);
    ProtoGetD3D11Resource function = *(ProtoGetD3D11Resource*)(vTable + OffsetGetD3D11Resource);
    return function(pSwapChainBuffer);
}

bool DwmCore::IsPrimaryMonitor(IOverlayMonitorTarget* pTarget)
{
    uintptr_t vTable = *(uintptr_t*)(pTarget);
    ProtoIsPrimaryMonitor function = *(ProtoIsPrimaryMonitor*)(vTable + OffsetIsPrimaryMonitor);
    return function(pTarget);
}

ID3D11Device1* DwmCore::GetD3D11Device(CD3DDevice* pDevice)
{
    return *(ID3D11Device1**)(pDevice + OffsetD3D11Device);
}

IOverlayMonitorTarget* DwmCore::GetOverlayMonitorTarget(COverlayContext* pContext)
{
    return *(IOverlayMonitorTarget**)(pContext + OffsetOverlayMonitorTarget);
}