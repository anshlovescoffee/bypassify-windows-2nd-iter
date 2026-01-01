/*
dumper.cpp
Automation for dissecting dwmcore.dll function locations and structure offsets
*/

#include <windows.h>
#include <iostream>
#include <fstream>
#include <dbghelp.h>
#include <pathcch.h>
#include <vector>
#include "offsetblob.h"

typedef struct {
    WORD offset : 12;
    WORD type : 4;
} RELOCATION_INFO;

typedef struct
{
    char* symbol;
    uintptr_t address;
} SymbolDump;

#define GET_DEVICE_INDEX 0
#define GET_PHYSICAL_BACK_BUFFER_INDEX 1
#define GET_D3D11_RESOURCE_INDEX 2
#define IS_PRIMARY_MONITOR_INDEX 3
#define IS_VALID_INDEX 4
#define SWAPCHAIN_BUFFER_QUERY_INDEX 5
#define RENDER_TARGET_QUERY_INDEX 6
#define PRESENT_INDEX 7
#define PRESENT_NEEDED_INDEX_1 8
#define PRESENT_NEEDED_INDEX_2 9
#define SCHEDULE_COMPOSITION_PASS_INDEX 10
#define IS_OVERLAY_PREVENTED_INDEX 11
#define FORCE_FULL_DIRTY_RENDERING_INDEX 12

#define IS_VALID_OFFSET 0x18 // IDeviceResource->IsValid

SymbolDump symbolsToDump[] = {
    {"dwmcore!COverlaySwapChain::GetDevice", 0}, // used for IDeviceResource::VTable::GetDevice
    {"dwmcore!CDDisplaySwapChain::GetPhysicalBackBuffer", 0}, // used for IDeviceResource::VTable::GetPhysicalBackBuffer
    {"dwmcore!CDDisplaySwapChainBuffer::GetD3D11Resource", 0}, // used for ISwapChainBuffer::VTable::GetD3D11Resource
    {"dwmcore!CDDisplayRenderTarget::IsPrimaryMonitor", 0}, // used for IOverlayMonitorTarget::VTable::IsPrimaryMonitor
    {"dwmcore!COverlaySwapChain::IsValid", 0}, // used to get DwmSwapChain::IDeviceResource vtable + 0x18
    {"dwmcore!CLegacySwapChainBuffer::QueryInterface", 0}, // used to get ISwapChainBuffer vtable base
    {"dwmcore!CDDARenderTarget::QueryInterface", 0}, // used to get IOverlayMonitorTarget vtable base
    {"dwmcore!COverlayContext::Present", 0},
    {"dwmcore!CDDisplayRenderTarget::PresentNeeded", 0},
    {"dwmcore!CLegacyRenderTarget::PresentNeeded", 0},
    {"dwmcore!ScheduleCompositionPass", 0},
    {"dwmcore!CGlobalCompositionSurfaceInfo::IsOverlayPrevented", 0},
    {"dwmcore!CCommonRegistryData::ForceFullDirtyRendering", 0}
};

uintptr_t minimum(uintptr_t a, uintptr_t b)
{
    return (a < b) ? a : b;
}

// making sure that the CWD is always inside the directory of the dumper no matter where it is run from
void FixCwd()
{
    WCHAR exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    PathCchRemoveFileSpec(exePath, MAX_PATH);
    SetCurrentDirectoryW(exePath);
}

int main()
{
    uintptr_t dwmcoreBase = reinterpret_cast<uintptr_t>(LoadLibraryA("dwmcore.dll"));
    HANDLE hProcess = GetCurrentProcess();
    PIMAGE_DOS_HEADER dosHeader = NULL;
    PIMAGE_NT_HEADERS ntHeaders = NULL;
    DWORD64 dwmcoreLoad64 = NULL;
    IMAGEHLP_MODULE64 moduleInfo = {};
    SYMBOL_INFO symbolInfo = {};
    OffsetBlob dumpedOffsets = {};

    uintptr_t vtDeviceResource = 0; // IDeviceResource vtable base
    std::vector<uintptr_t> swapchainBufferVtBaseCandidates; // ISwapchainBuffer vtable base candidates
    std::vector<uintptr_t> monitorTargetVtBaseCandidates; // IOverlayMonitorTarget vtable base candidates

    std::vector<uintptr_t> getDeviceFunctionRefs; // all references to COverlaySwapChain::GetDevice
    uintptr_t getPhysicalBackBufferRef = 0; // CDDisplaySwapChain::GetPhysicalBackBuffer reference in the vtable entries
    uintptr_t d3d11ResourceFunctionRef = 0; // CDDisplaySwapChainBuffer::GetD3D11Resource reference in the vtable entries
    uintptr_t isPrimaryMonitorRef = 0; // CDDisplayRenderTarget::IsPrimaryMonitor reference in the vtable entries

    bool isFirstRef = true; // used for vtable offset dumping

    // load dwmcore.dll
    printf("dwmcore.dll base: 0x%llx\n", dwmcoreBase);
    if (!dwmcoreBase)
    {
        std::cout << "Failed to load dwmcore.dll" << std::endl;
        return 1;
    }

    // initialize dbghelp engine
    if (!SymInitialize(
        hProcess,
        "srv*C:\\symbols*https://msdl.microsoft.com/download/symbols",
        FALSE
    ))
    {
        printf("WARNING: Failed to initialize debug helper: %d\n", GetLastError());
        return 1;
    }
    dwmcoreLoad64 = SymLoadModuleEx(hProcess, NULL, "dwmcore.dll", NULL, dwmcoreBase, 0, NULL, 0);
    moduleInfo.SizeOfStruct = sizeof(moduleInfo);
    if (!dwmcoreLoad64)
    {
        std::cout << "Failed to load symbols for dwmcore.dll" << std::endl;
        return 1;
    }
    if (!SymGetModuleInfo64(hProcess, dwmcoreLoad64, &moduleInfo))
    {
        printf("Failed to load symbols for dwmcore.dll: %d\n", GetLastError());
        return 1;
    }

    // doing a preliminary dump of symbols to find offsets
    symbolInfo.SizeOfStruct = sizeof(symbolInfo);
    for (int i = 0; i < sizeof(symbolsToDump) / sizeof(SymbolDump); ++i)
    {
        if (!SymFromName(hProcess, symbolsToDump[i].symbol, &symbolInfo))
        {
            printf("WARNING! Unable to dump symbol %s\n", symbolsToDump[i].symbol);
        }
        else
        {
            symbolsToDump[i].address = symbolInfo.Address;
            // printf("%s: 0x%llx\n", symbol.symbol, symbol.address);
        }
    }

    // vtable entries are all in the relocation directory so iterate through .reloc to find vtable function pointers
    dosHeader = (PIMAGE_DOS_HEADER)(dwmcoreBase);
    ntHeaders = (PIMAGE_NT_HEADERS)(dwmcoreBase + dosHeader->e_lfanew);

    for (
        PIMAGE_BASE_RELOCATION relocBase = (PIMAGE_BASE_RELOCATION)(dwmcoreBase + ntHeaders->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress);
        relocBase->VirtualAddress != 0;
        relocBase = reinterpret_cast<PIMAGE_BASE_RELOCATION>(reinterpret_cast<uintptr_t>(relocBase) + relocBase->SizeOfBlock)
    )
    {
        UINT relocations = (relocBase->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(RELOCATION_INFO);
        RELOCATION_INFO* relocation = reinterpret_cast<RELOCATION_INFO*>(relocBase + 1);
        for (UINT i = 0; i < relocations; ++i, ++relocation)
        {
            if ((relocation->type) == IMAGE_REL_BASED_DIR64)
            {
                uintptr_t addyu64 = dwmcoreBase + relocBase->VirtualAddress + relocation->offset;
                uintptr_t* addy = reinterpret_cast<uintptr_t*>(addyu64);
                uintptr_t addyValue = *addy;
                if (addyValue == symbolsToDump[IS_VALID_INDEX].address) // found VtDeviceResource base + IS_VALID_OFFSET
                {
                    vtDeviceResource = addyu64 - IS_VALID_OFFSET;
                }
                else if (addyValue == symbolsToDump[SWAPCHAIN_BUFFER_QUERY_INDEX].address)
                {
                    swapchainBufferVtBaseCandidates.push_back(addyu64);
                }
                else if (addyValue == symbolsToDump[RENDER_TARGET_QUERY_INDEX].address)
                {
                    monitorTargetVtBaseCandidates.push_back(addyu64);
                }
                else if (addyValue == symbolsToDump[GET_DEVICE_INDEX].address)
                {
                    getDeviceFunctionRefs.push_back(addyu64);
                }
                else if (addyValue == symbolsToDump[GET_PHYSICAL_BACK_BUFFER_INDEX].address)
                {
                    getPhysicalBackBufferRef = addyu64;
                }
                else if (addyValue == symbolsToDump[GET_D3D11_RESOURCE_INDEX].address)
                {
                    d3d11ResourceFunctionRef = addyu64;
                }
                else if (addyValue == symbolsToDump[IS_PRIMARY_MONITOR_INDEX].address)
                {
                    isPrimaryMonitorRef = addyu64;
                }
                // printf("Relocation dwmcore.dll+0x%x: 0x%llx\n", relocBase->VirtualAddress + relocation->offset, *addy);
            }
            else
            {
                // printf("WARNING! Relocation is not Dir64\n");
            }
        }
    }

    // dumping vtable data (OffsetGetDevice)
    isFirstRef = true;
    for (uintptr_t getDeviceCandidate : getDeviceFunctionRefs)
    {
        if (getDeviceCandidate < vtDeviceResource)
        {
            continue;
        }
        if (isFirstRef)
        {
            dumpedOffsets.OffsetGetDevice = getDeviceCandidate - vtDeviceResource;
            isFirstRef = false;
        }
        else
        {
            dumpedOffsets.OffsetGetDevice = minimum(dumpedOffsets.OffsetGetDevice, getDeviceCandidate - vtDeviceResource);
        }
    }

    // dumping vtable data (OffsetGetPhysicalBackBuffer)
    dumpedOffsets.OffsetGetPhysicalBackBuffer = getPhysicalBackBufferRef - vtDeviceResource;

    // dumping vtable data (OffsetGetD3D11Resource)
    isFirstRef = true;
    for (uintptr_t swapchainBufferVtCandidate : swapchainBufferVtBaseCandidates)
    {
        if (d3d11ResourceFunctionRef < swapchainBufferVtCandidate)
        {
            continue;
        }
        if (isFirstRef)
        {
            dumpedOffsets.OffsetGetD3D11Resource = d3d11ResourceFunctionRef - swapchainBufferVtCandidate;
            isFirstRef = false;
        }
        else
        {
            dumpedOffsets.OffsetGetD3D11Resource = minimum(dumpedOffsets.OffsetGetD3D11Resource, d3d11ResourceFunctionRef - swapchainBufferVtCandidate);
        }
    }

    // dumping vtable data (OffsetIsPrimaryMonitor)
    isFirstRef = true;
    for (uintptr_t monitorTargetVtCandidate : monitorTargetVtBaseCandidates)
    {
        if (isPrimaryMonitorRef < monitorTargetVtCandidate)
        {
            continue;
        }
        if (isFirstRef)
        {
            dumpedOffsets.OffsetIsPrimaryMonitor = isPrimaryMonitorRef - monitorTargetVtCandidate;
            isFirstRef = false;
        }
        else
        {
            dumpedOffsets.OffsetIsPrimaryMonitor = minimum(dumpedOffsets.OffsetIsPrimaryMonitor, isPrimaryMonitorRef - monitorTargetVtCandidate);
        }
    }

    // dumping static function offsets
    dumpedOffsets.OffsetPresent = symbolsToDump[PRESENT_INDEX].address - dwmcoreBase;
    dumpedOffsets.OffsetPresentNeeded1 = symbolsToDump[PRESENT_NEEDED_INDEX_1].address - dwmcoreBase;
    dumpedOffsets.OffsetPresentNeeded2 = symbolsToDump[PRESENT_NEEDED_INDEX_2].address - dwmcoreBase;
    dumpedOffsets.OffsetScheduleCompositionPass = symbolsToDump[SCHEDULE_COMPOSITION_PASS_INDEX].address - dwmcoreBase;
    dumpedOffsets.OffsetIsOverlayPrevented = symbolsToDump[IS_OVERLAY_PREVENTED_INDEX].address - dwmcoreBase;
    dumpedOffsets.OffsetForceDirtyRendering = symbolsToDump[FORCE_FULL_DIRTY_RENDERING_INDEX].address - dwmcoreBase;

    // undumpable stuff that's mostly stable
    dumpedOffsets.OffsetD3D11Device = 0x228;
    dumpedOffsets.OffsetOverlayMonitorTarget = 0x0;

    // display result of dumping
    printf("OffsetGetDevice: 0x%llx\n", dumpedOffsets.OffsetGetDevice);
    printf("OffsetGetPhysicalBackBuffer: 0x%llx\n", dumpedOffsets.OffsetGetPhysicalBackBuffer);
    printf("OffsetGetD3D11Resource: 0x%llx\n", dumpedOffsets.OffsetGetD3D11Resource);
    printf("OffsetIsPrimaryMonitor: 0x%llx\n", dumpedOffsets.OffsetIsPrimaryMonitor);
    printf("OffsetPresent: 0x%llx\n", dumpedOffsets.OffsetPresent);
    printf("OffsetPresentNeeded1: 0x%llx\n", dumpedOffsets.OffsetPresentNeeded1);
    printf("OffsetPresentNeeded2: 0x%llx\n", dumpedOffsets.OffsetPresentNeeded2);
    printf("OffsetScheduleCompositionPass: 0x%llx\n", dumpedOffsets.OffsetScheduleCompositionPass);
    printf("OffsetIsOverlayPrevented: 0x%llx\n", dumpedOffsets.OffsetIsOverlayPrevented);
    printf("OffsetD3D11Device: 0x%llx\n", dumpedOffsets.OffsetD3D11Device);
    printf("OffsetOverlayMonitorTarget: 0x%llx\n", dumpedOffsets.OffsetOverlayMonitorTarget);
    printf("OffsetForceDirtyRendering: 0x%llx\n", dumpedOffsets.OffsetForceDirtyRendering);

    FixCwd();

    // write offsets to blob file
    std::ofstream blobFile("offsets.blob");
    if (blobFile)
    {
        blobFile.write(reinterpret_cast<char*>(&dumpedOffsets), sizeof(dumpedOffsets));
        blobFile.close();
    }
    else
    {
        std::cout << "WARNING! Unable to create offset blob file" << std::endl;
    }

    // write a pretty offset dump
    std::ofstream offsetFile("offsets.cpp");
    if (offsetFile)
    {
        offsetFile << "/*" << std::endl;
        offsetFile << "offsets.cpp" << std::endl;
        offsetFile << "Value definition for all dwmcore.dll offsets" << std::endl;
        offsetFile << "*/" << std::endl;
        offsetFile << std::endl;
        offsetFile << "#include \"offsets.hpp\"" << std::endl;
        offsetFile << std::endl;
        offsetFile << "u64 OffsetGetDevice = 0x" << std::hex << dumpedOffsets.OffsetGetDevice << "; // IDeviceResource::VTable::GetDevice" << std::endl;
        offsetFile << "u64 OffsetGetPhysicalBackBuffer = 0x" << std::hex << dumpedOffsets.OffsetGetPhysicalBackBuffer << "; // IDeviceResource::VTable::GetPhysicalBackBuffer" << std::endl;
        offsetFile << "u64 OffsetGetD3D11Resource = 0x" << std::hex << dumpedOffsets.OffsetGetD3D11Resource << "; // ISwapChainBuffer::VTable::GetD3D11Resource" << std::endl;
        offsetFile << "u64 OffsetIsPrimaryMonitor = 0x" << std::hex << dumpedOffsets.OffsetIsPrimaryMonitor << "; // IOverlayMonitorTarget::VTable::IsPrimaryMonitor" << std::endl;
        offsetFile << "u64 OffsetPresent = 0x" << std::hex << dumpedOffsets.OffsetPresent << "; // COverlayContext::Present" << std::endl;
        offsetFile << "u64 OffsetPresentNeeded1 = 0x" << std::hex << dumpedOffsets.OffsetPresentNeeded1 << "; // CDDisplayRenderTarget::PresentNeeded" << std::endl;
        offsetFile << "u64 OffsetPresentNeeded2 = 0x" << std::hex << dumpedOffsets.OffsetPresentNeeded2 << "; // CLegacyRenderTarget::PresentNeeded" << std::endl;
        offsetFile << "u64 OffsetScheduleCompositionPass = 0x" << std::hex << dumpedOffsets.OffsetScheduleCompositionPass << "; // ScheduleCompositionPass" << std::endl;
        offsetFile << "u64 OffsetIsOverlayPrevented = 0x" << std::hex << dumpedOffsets.OffsetIsOverlayPrevented << "; // CGlobalCompositionSurfaceInfo::IsOverlayPrevented" << std::endl;
        offsetFile << "u64 OffsetD3D11Device = 0x" << std::hex << dumpedOffsets.OffsetD3D11Device << "; // CD3DDevice.pD3D11Device" << std::endl;
        offsetFile << "u64 OffsetOverlayMonitorTarget = 0x" << std::hex << dumpedOffsets.OffsetOverlayMonitorTarget << "; // COverlayContext.pOverlayMonitorTarget" << std::endl;
        offsetFile << "u64 OffsetForceDirtyRendering = 0x" << std::hex << dumpedOffsets.OffsetForceDirtyRendering << "; // CCommonRegistryData.ForceFullDirtyRendering" << std::endl;
        offsetFile.close();
    }
    else
    {
        std::cout << "WARNING! Unable to create offset source file" << std::endl;
    }

    return 0;
}