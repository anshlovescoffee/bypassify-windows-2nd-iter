/*
offsetidx.h
All of the indexes for offsets in the offset table for the client
*/

#pragma once

#define OffsetGetDevice 0 // IDeviceResource::VTable::GetDevice
#define OffsetGetPhysicalBackBuffer 1 // IDeviceResource::VTable::GetPhysicalBackBuffer
#define OffsetGetD3D11Resource 2 // ISwapChainBuffer::VTable::GetD3D11Resource
#define OffsetIsPrimaryMonitor 3 // IOverlayMonitorTarget::VTable::IsPrimaryMonitor
#define OffsetPresent 4 // COverlayContext::Present
#define OffsetPresentNeeded1 5 // CDDisplayRenderTarget::PresentNeeded
#define OffsetPresentNeeded2 6 // CLegacyRenderTarget::PresentNeeded
#define OffsetScheduleCompositionPass 7 // ScheduleCompositionPass
#define OffsetIsOverlayPrevented 8 // CGlobalCompositionSurfaceInfo::IsOverlayPrevented
#define OffsetD3D11Device 9 // CD3DDevice.pD3D11Device
#define OffsetOverlayMonitorTarget 10 // COverlayContext.pOverlayMonitorTarget
#define OffsetForceDirtyRendering 11 // CCommonRegistryData.ForceFullDirtyRendering
#define MAX_OFFSET 12