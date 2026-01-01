/*
offsets.cpp
Value definition for all dwmcore.dll offsets
*/

#include "offsets.hpp"

u64 OffsetGetDevice = 0x28; // IDeviceResource::VTable::GetDevice
u64 OffsetGetPhysicalBackBuffer = 0xc0; // IDeviceResource::VTable::GetPhysicalBackBuffer
u64 OffsetGetD3D11Resource = 0x98; // ISwapChainBuffer::VTable::GetD3D11Resource
u64 OffsetIsPrimaryMonitor = 0x20; // IOverlayMonitorTarget::VTable::IsPrimaryMonitor
u64 OffsetPresent = 0x1ae000; // COverlayContext::Present
u64 OffsetPresentNeeded1 = 0x1d88d0; // CDDisplayRenderTarget::PresentNeeded
u64 OffsetPresentNeeded2 = 0x1d8904; // CLegacyRenderTarget::PresentNeeded
u64 OffsetScheduleCompositionPass = 0x10e3fc; // ScheduleCompositionPass
u64 OffsetIsOverlayPrevented = 0x1f5180; // CGlobalCompositionSurfaceInfo::IsOverlayPrevented
u64 OffsetD3D11Device = 0x228; // CD3DDevice.pD3D11Device
u64 OffsetOverlayMonitorTarget = 0x0; // COverlayContext.pOverlayMonitorTarget
u64 OffsetForceDirtyRendering = 0x3fd7b9; // CCommonRegistryData.ForceFullDirtyRendering