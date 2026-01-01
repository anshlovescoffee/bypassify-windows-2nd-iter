/*
offsets.hpp
All offsets that are required to be dumped in order for the dwm hook to work
*/

#pragma once

extern "C"
{
    #define u64 unsigned long long

    // VTable offsets
    __declspec(dllexport) extern u64 OffsetGetDevice; // IDeviceResource::VTable::GetDevice
    __declspec(dllexport) extern u64 OffsetGetPhysicalBackBuffer; // IDeviceResource::VTable::GetPhysicalBackBuffer
    __declspec(dllexport) extern u64 OffsetGetD3D11Resource; // ISwapChainBuffer::VTable::GetD3D11Resource
    __declspec(dllexport) extern u64 OffsetIsPrimaryMonitor; // IOverlayMonitorTarget::VTable::IsPrimaryMonitor

    // Static offsets (from dwmcore.dll base)
    __declspec(dllexport) extern u64 OffsetPresent; // COverlayContext::Present
    __declspec(dllexport) extern u64 OffsetPresentNeeded1; // CDDisplayRenderTarget::PresentNeeded
    __declspec(dllexport) extern u64 OffsetPresentNeeded2; // CLegacyRenderTarget::PresentNeeded
    __declspec(dllexport) extern u64 OffsetScheduleCompositionPass; // ScheduleCompositionPass
    __declspec(dllexport) extern u64 OffsetIsOverlayPrevented; // CGlobalCompositionSurfaceInfo::IsOverlayPrevented

    // Struct data offsets; no way to easily dump but are mostly stable
    __declspec(dllexport) extern u64 OffsetD3D11Device; // CD3DDevice.pD3D11Device
    __declspec(dllexport) extern u64 OffsetOverlayMonitorTarget; // COverlayContext.pOverlayMonitorTarget
    __declspec(dllexport) extern u64 OffsetForceDirtyRendering; // CCommonRegistryData.ForceFullDirtyRendering
}