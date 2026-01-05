/*
dwmcore.hpp
Contains dwmcore related services
*/

#pragma once

#include <dxgi.h>
#include <d3d11_1.h>
#include <d2d1_1.h>

namespace DwmCore
{
    // dummy types used for annotation, typed as char so that offsets are not applied by a multiplier when added to pointers
    using CD3DDevice = char;
    using ISwapChainBuffer = char;
    using DwmSwapChain = char;
    using COverlayContext = char;
    using IOverlayMonitorTarget = char;

    // vtable function prototypes
    using ProtoGetDevice = CD3DDevice*(__fastcall*)(DwmSwapChain*);
    using ProtoGetPhysicalBackBuffer = ISwapChainBuffer*(__fastcall*)(DwmSwapChain*);
    using ProtoGetD3D11Resource = ID3D11Resource*(__fastcall*)(ISwapChainBuffer*);
    using ProtoIsPrimaryMonitor = bool(__fastcall*)(IOverlayMonitorTarget*);

    // static function prototypes
    using ProtoPresent = HRESULT(__fastcall*)(COverlayContext*, DwmSwapChain*, unsigned int, __int64, unsigned int, bool*, bool);
    using ProtoPresentNeeded = bool(__fastcall*)(void*);
    using ProtoScheduleCompositionPass = ULONG(__fastcall*)(unsigned int, unsigned int);

    // calling functions that live in a vtable
    CD3DDevice* GetDevice(DwmSwapChain* pSwapChain);
    ISwapChainBuffer* GetPhysicalBackBuffer(DwmSwapChain* pSwapChain);
    ID3D11Resource* GetD3D11Resource(ISwapChainBuffer* pSwapChainBuffer);
    bool IsPrimaryMonitor(IOverlayMonitorTarget* pTarget);

    // general getter functions
    IUnknown* GetD3D11Device(CD3DDevice* pDevice);
    IOverlayMonitorTarget* GetOverlayMonitorTarget(COverlayContext* pContext);
    
    void TestDraw(ID3D11Device1* pDxDevice, ID3D11Resource* pDxBuffer);
    bool Init();
}