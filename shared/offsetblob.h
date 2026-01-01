/*
offsetblob.h
Serialized data format definition for offset data
*/

#define u64 unsigned long long

struct OffsetBlob
{
    // VTable offsets
    u64 OffsetGetDevice; // IDeviceResource::VTable::GetDevice
    u64 OffsetGetPhysicalBackBuffer; // IDeviceResource::VTable::GetPhysicalBackBuffer
    u64 OffsetGetD3D11Resource; // ISwapChainBuffer::VTable::GetD3D11Resource
    u64 OffsetIsPrimaryMonitor; // IOverlayMonitorTarget::VTable::IsPrimaryMonitor

    // Static offsets (from dwmcore.dll base)
    u64 OffsetPresent; // COverlayContext::Present
    u64 OffsetPresentNeeded1; // CDDisplayRenderTarget::PresentNeeded
    u64 OffsetPresentNeeded2; // CLegacyRenderTarget::PresentNeeded
    u64 OffsetScheduleCompositionPass; // ScheduleCompositionPass
    u64 OffsetIsOverlayPrevented; // CGlobalCompositionSurfaceInfo::IsOverlayPrevented

    // Struct data offsets; no way to easily dump but are mostly stable
    u64 OffsetD3D11Device; // CD3DDevice.pD3D11Device
    u64 OffsetOverlayMonitorTarget; // COverlayContext.pOverlayMonitorTarget
    u64 OffsetForceDirtyRendering; // CCommonRegistryData.ForceFullDirtyRendering
};