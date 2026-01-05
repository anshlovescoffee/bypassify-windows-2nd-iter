/*
offsets.cpp
Value definition for all dwmcore.dll offsets
*/

#include "offsets.hpp"

// unsigned long long OffsetTable[MAX_OFFSET] = {1};

unsigned long long OffsetTable[MAX_OFFSET] = {
    0x28, // OffsetGetDevice
    0xc0, // OffsetGetPhysicalBackBuffer
    0x98, // OffsetGetD3D11Resource
    0x20, // OffsetIsPrimaryMonitor
    0x1ae000, // OffsetPresent
    0x1d88d0, // OffsetPresentNeeded1
    0x1d8904, // OffsetPresentNeeded2
    0x10e3fc, // OffsetScheduleCompositionPass
    0x1f5180, // OffsetIsOverlayPrevented
    0x218, // OffsetD3D11Device
    0x0, // OffsetOverlayMonitorTarget
    0x3fd7b9 // OffsetForceDirtyRendering
};