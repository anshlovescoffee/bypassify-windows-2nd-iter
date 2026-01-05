/*
dumper.cpp
Automation for dissecting dwmcore.dll function locations and structure offsets
*/

#include <windows.h>
#include <psapi.h>
#include <iostream>
#include <fstream>
#include <dbghelp.h>
#include <string>
#include <d3d11_1.h>
#include <dxgi.h>

#include "offsetidx.h"
#include "MinHook.h"

#define POINTER_MAGIC 0x6967696769676967

using COverlayContext = char;
using DwmSwapChain = char;
using CD3DDevice = char;
using ISwapChainBuffer = char;
using IOverlayMonitorTarget = char;

using ProtoCreateOverlayContext  = void(__fastcall*)(COverlayContext* pThis, IOverlayMonitorTarget* pTarget, bool disableMPO);
using ProtoQueryInterface = HRESULT(__fastcall*)(void* pThis, GUID uuid, void** pOut);
using ProtoPresent = uintptr_t(__fastcall*)(COverlayContext*, DwmSwapChain*, unsigned int, uintptr_t, unsigned int, bool*, bool);
using ProtoGetDevice = CD3DDevice*(__fastcall*)(DwmSwapChain*);
using ProtoGetPhysicalBackBuffer = ISwapChainBuffer*(__fastcall*)(DwmSwapChain*);

bool g_wantDump = true;
uintptr_t g_dwmcoreBase = 0;
uintptr_t g_d3d11Base = 0;
MODULEINFO g_d3d11ModuleInfo = {};
uintptr_t g_offsets[MAX_OFFSET];
char g_pSymbolInfoBuffer[sizeof(SYMBOL_INFO) + (MAX_SYM_NAME - 1) * sizeof(TCHAR)];
PSYMBOL_INFO g_pSymbolInfo = reinterpret_cast<PSYMBOL_INFO>(g_pSymbolInfoBuffer);
ProtoPresent OriginalPresent = nullptr;

char g_memoryPool[0x20000]; // large memory pool for when ya just need to create internal objects and don't wanna malloc
uintptr_t* g_memoryPoolU64 = reinterpret_cast<uintptr_t*>(g_memoryPool);

DWORD GetPointerProtect(void* pointer) // gets protection in a memory region, returning 0 if pointer is invalid
{
    MEMORY_BASIC_INFORMATION mbi = {};
    if (!VirtualQuery(pointer, &mbi, sizeof(MEMORY_BASIC_INFORMATION)))
    {
        // fprintf(stderr, "Failed to query pointer protect: %d\n", GetLastError());
        return 0;
    }
    return mbi.Protect;
}

bool IsValidPointer(void* pointer)
{
    return GetPointerProtect(pointer) != 0;
}

bool IsReadOnlyPointer(void* pointer)
{
    return GetPointerProtect(pointer) == PAGE_READONLY;
}

void ExportOffsets()
{
    fprintf(stderr, "unsigned long long OffsetTable[MAX_OFFSET] = {\n");
    fprintf(stderr, "\t0x%x, // OffsetGetDevice\n", g_offsets[OffsetGetDevice]);
    fprintf(stderr, "\t0x%x, // OffsetGetPhysicalBackBuffer\n", g_offsets[OffsetGetPhysicalBackBuffer]);
    fprintf(stderr, "\t0x%x, // OffsetGetD3D11Resource\n", g_offsets[OffsetGetD3D11Resource]);
    fprintf(stderr, "\t0x%x, // OffsetIsPrimaryMonitor\n", g_offsets[OffsetIsPrimaryMonitor]);
    fprintf(stderr, "\t0x%x, // OffsetPresent\n", g_offsets[OffsetPresent]);
    fprintf(stderr, "\t0x%x, // OffsetPresentNeeded1\n", g_offsets[OffsetPresentNeeded1]);
    fprintf(stderr, "\t0x%x, // OffsetPresentNeeded2\n", g_offsets[OffsetPresentNeeded2]);
    fprintf(stderr, "\t0x%x, // OffsetScheduleCompositionPass\n", g_offsets[OffsetScheduleCompositionPass]);
    fprintf(stderr, "\t0x%x, // OffsetIsOverlayPrevented\n", g_offsets[OffsetIsOverlayPrevented]);
    fprintf(stderr, "\t0x%x, // OffsetD3D11Device\n", g_offsets[OffsetD3D11Device]);
    fprintf(stderr, "\t0x%x, // OffsetOverlayMonitorTarget\n", g_offsets[OffsetOverlayMonitorTarget]);
    fprintf(stderr, "\t0x%x, // OffsetForceDirtyRendering\n", g_offsets[OffsetForceDirtyRendering]);
    fprintf(stderr, "}\n");

    std::ofstream blobFile("C:\\DwmDump\\offsets.blob");
    if (blobFile)
    {
        blobFile.write(reinterpret_cast<char*>(&g_offsets), sizeof(g_offsets));
        blobFile.close();
    }
    else
    {
        fprintf(stderr, "WARNING! Unable to create offset blob file\n");
    }
}

uintptr_t HookPresent(COverlayContext* pThis, DwmSwapChain* pDwmSwapChain, unsigned int a3, uintptr_t a4, unsigned int a5, bool* a6, bool a7)
{
    if (g_wantDump && pThis && pDwmSwapChain) // dumper main
    {
        HANDLE hProcess = GetCurrentProcess();
        uintptr_t* vtSwapChain = *reinterpret_cast<uintptr_t**>(pDwmSwapChain);
        uintptr_t* vtBackBuffer = nullptr;
        uintptr_t* vtMonitorTarget = nullptr;
        ISwapChainBuffer* pBackBuffer = nullptr;
        IOverlayMonitorTarget* pMonitorTarget = nullptr;
        ProtoQueryInterface*** pDwmDevice_t = nullptr;
        ID3D11Device1* pDevice = nullptr;
        
        g_wantDump = false;

        // OffsetGetDevice
        if (!SymFromName(hProcess, "dwmcore!COverlaySwapChain::GetDevice", g_pSymbolInfo))
        {
            fprintf(stderr, "Failed to get COverlaySwapChain::GetDevice\n");
            return OriginalPresent(pThis, pDwmSwapChain, a3, a4, a5, a6, a7);
        }
        for (int i = 0; i < 0x1000; ++i)
        {
            if (vtSwapChain[i] == g_pSymbolInfo->Address)
            {
                g_offsets[OffsetGetDevice] = i * 8;
                pDwmDevice_t = reinterpret_cast<ProtoQueryInterface***>(reinterpret_cast<ProtoGetDevice>(vtSwapChain[i])(pDwmSwapChain));
                // fprintf(stderr, "OffsetGetDevice: 0x%x\n", g_offsets.OffsetGetDevice);
                break;
            }
        }

        // OffsetGetPhysicalBackBuffer
        for (int i = 0; i < 0x1000; ++i)
        {
            if (!SymFromAddr(hProcess, vtSwapChain[i], 0, g_pSymbolInfo))
            {
                continue;
            }

            std::string symbolName(g_pSymbolInfo->Name);
            if (symbolName.find("GetPhysicalBackBuffer") != std::string::npos)
            {
                g_offsets[OffsetGetPhysicalBackBuffer] = i * 8;
                pBackBuffer = reinterpret_cast<ProtoGetPhysicalBackBuffer>(vtSwapChain[i])(pDwmSwapChain);
                // fprintf(stderr, "OffsetGetPhysicalBackBuffer: 0x%x\n", g_offsets.OffsetGetPhysicalBackBuffer);
                break;
            }
        }

        // OffsetGetD3D11Resource
        if (!pBackBuffer)
        {
            fprintf(stderr, "Failed to get swapchain back buffer\n");
            return OriginalPresent(pThis, pDwmSwapChain, a3, a4, a5, a6, a7);
        }
        vtBackBuffer = *reinterpret_cast<uintptr_t**>(pBackBuffer);
        for (int i = 0; i < 0x1000; ++i)
        {
            if (!SymFromAddr(hProcess, vtBackBuffer[i], 0, g_pSymbolInfo))
            {
                continue;
            }

            std::string symbolName(g_pSymbolInfo->Name);
            if (symbolName.find("GetD3D11Resource") != std::string::npos)
            {
                g_offsets[OffsetGetD3D11Resource] = i * 8;
                // fprintf(stderr, "OffsetGetD3D11Resource: 0x%x\n", g_offsets.OffsetGetD3D11Resource);
                break;
            }
        }

        // OffsetOverlayMonitorTarget
        if (!SymFromName(hProcess, "dwmcore!COverlayContext::COverlayContext", g_pSymbolInfo))
        {
            fprintf(stderr, "Unable to get COverlayContext::COverlayContext\n");
            return OriginalPresent(pThis, pDwmSwapChain, a3, a4, a5, a6, a7);
        }
        memset(g_memoryPool, 0, sizeof(g_memoryPool));
        reinterpret_cast<ProtoCreateOverlayContext>(g_pSymbolInfo->Address)(g_memoryPool, (IOverlayMonitorTarget*)(POINTER_MAGIC), true);
        for (int i = 0; i < sizeof(g_memoryPool) / sizeof(uintptr_t); ++i)
        {
            if (g_memoryPoolU64[i] == POINTER_MAGIC)
            {
                g_offsets[OffsetOverlayMonitorTarget] = i * 8;
                pMonitorTarget = reinterpret_cast<IOverlayMonitorTarget*>(*reinterpret_cast<uintptr_t*>(pThis + g_offsets[OffsetOverlayMonitorTarget]));
                // fprintf(stderr, "OffsetOverlayMonitorTarget: 0x%x\n", g_offsets.OffsetOverlayMonitorTarget);
                break;
            }
        }

        // OffsetIsPrimaryMonitor
        if (!pMonitorTarget)
        {
            fprintf(stderr, "Unable to get monitor target\n");
            return OriginalPresent(pThis, pDwmSwapChain, a3, a4, a5, a6, a7);
        }
        vtMonitorTarget = *reinterpret_cast<uintptr_t**>(pMonitorTarget);
        for (int i = 0; i < 0x1000; ++i)
        {
            if (!SymFromAddr(hProcess, vtMonitorTarget[i], 0, g_pSymbolInfo))
            {
                continue;
            }

            std::string symbolName(g_pSymbolInfo->Name);
            if (symbolName.find("IsPrimaryMonitor") != std::string::npos)
            {
                g_offsets[OffsetIsPrimaryMonitor] = i * 8;
                // fprintf(stderr, "OffsetIsPrimaryMonitor: 0x%x\n", g_offsets.OffsetIsPrimaryMonitor);
                break;
            }
        }

        // OffsetD3D11Device
        if (!pDwmDevice_t)
        {
            fprintf(stderr, "Failed to get CD3DDevice\n");
            return OriginalPresent(pThis, pDwmSwapChain, a3, a4, a5, a6, a7);
        }
        for (int i = 0; i < 0x100; ++i)
        {
            // fprintf(stderr, "GetPointerProtect(pDwmDevice_t[%d]) = 0x%x\n", i, GetPointerProtect(pDwmDevice_t[i]));
            if (GetPointerProtect(pDwmDevice_t[i]) != PAGE_READWRITE)
            {
                continue;
            }
            if (!IsReadOnlyPointer(pDwmDevice_t[i][0]))
            {
                continue;  
            }
            if ((reinterpret_cast<uintptr_t>(pDwmDevice_t[i][0]) - g_d3d11Base) > g_d3d11ModuleInfo.SizeOfImage)
            {
                continue;
            }
            // fprintf(stderr, "%d: d3d11.dll+0x%x\n", i, reinterpret_cast<uintptr_t>(pDwmDevice_t[i][0]) - g_d3d11Base);
            
            reinterpret_cast<IUnknown*>(pDwmDevice_t[i])->QueryInterface(__uuidof(ID3D11Device1), (void**)(&pDevice));
            if (pDevice)
            {
                g_offsets[OffsetD3D11Device] = i * 8;
                // fprintf(stderr, "OffsetD3D11Device: 0x%x\n", g_offsets.OffsetD3D11Device);
                pDevice->Release();
                break;
            }
        }

        ExportOffsets();
    }
    return OriginalPresent(pThis, pDwmSwapChain, a3, a4, a5, a6, a7);
}

void DumperInit() // dump all offsets that are not struct offsets or vtable offsets
{
    uintptr_t dwmcoreLoad64 = 0;
    uintptr_t d3d11Load64 = 0;
    HANDLE hProcess = GetCurrentProcess();
    IMAGEHLP_MODULE64 moduleInfo = {};

    g_dwmcoreBase = reinterpret_cast<uintptr_t>(GetModuleHandleA("dwmcore.dll"));
    g_d3d11Base = reinterpret_cast<uintptr_t>(GetModuleHandleA("d3d11.dll"));

    // initialize dbghelp
    if (!g_dwmcoreBase)
    {
        fprintf(stderr, "Failed to find dwmcore.dll\n");
        return;
    }
    if (!g_d3d11Base)
    {
        fprintf(stderr, "Failed to find d3d11.dll\n");
        return;
    }
    if (!GetModuleInformation(hProcess, reinterpret_cast<HMODULE>(g_d3d11Base), &g_d3d11ModuleInfo, sizeof(MODULEINFO)))
    {
        fprintf(stderr, "Failed to get d3d11.dll module info award\n");
        return;
    }

    if (!SymInitialize(
        hProcess,
        "srv*C:\\symbols*https://msdl.microsoft.com/download/symbols",
        FALSE
    ))
    {
        fprintf(stderr, "WARNING: Failed to initialize debug helper: %d\n", GetLastError());
        return;
    }

    dwmcoreLoad64 = SymLoadModuleEx(hProcess, NULL, "dwmcore.dll", NULL, g_dwmcoreBase, 0, NULL, 0);
    // d3d11Load64 = g_SymLoadModuleEx(hProcess, NULL, "d3d11.dll", NULL, g_d3d11Base, 0, NULL, 0);
    moduleInfo.SizeOfStruct = sizeof(moduleInfo);
    g_pSymbolInfo->SizeOfStruct = sizeof(SYMBOL_INFO);
    g_pSymbolInfo->MaxNameLen = MAX_SYM_NAME;
    
    if (!dwmcoreLoad64)
    {
        fprintf(stderr, "Failed to load symbols for dwmcore.dll\n");
        return;
    }
    if (!SymGetModuleInfo64(hProcess, dwmcoreLoad64, &moduleInfo))
    {
        fprintf(stderr, "Failed to load symbols for dwmcore.dll: %d\n", GetLastError());
        return;
    }
    // start dumping static symbols
    if (!SymFromName(hProcess, "dwmcore!COverlayContext::Present", g_pSymbolInfo))
    {
        fprintf(stderr, "Failed to get OffsetPresent\n");
        return;
    }
    g_offsets[OffsetPresent] = g_pSymbolInfo->Address - g_dwmcoreBase;

    if (!SymFromName(hProcess, "dwmcore!CDDisplayRenderTarget::PresentNeeded", g_pSymbolInfo))
    {
        fprintf(stderr, "Failed to get OffsetPresentNeeded1\n");
        return;
    }
    g_offsets[OffsetPresentNeeded1] = g_pSymbolInfo->Address - g_dwmcoreBase;

    if (!SymFromName(hProcess, "dwmcore!CLegacyRenderTarget::PresentNeeded", g_pSymbolInfo))
    {
        fprintf(stderr, "Failed to get OffsetPresentNeeded2\n");
        return;
    }
    g_offsets[OffsetPresentNeeded2] = g_pSymbolInfo->Address - g_dwmcoreBase;

    if (!SymFromName(hProcess, "dwmcore!ScheduleCompositionPass", g_pSymbolInfo))
    {
        fprintf(stderr, "Failed to get OffsetScheduleCompositionPass\n");
        return;
    }
    g_offsets[OffsetScheduleCompositionPass] = g_pSymbolInfo->Address - g_dwmcoreBase;

    if (!SymFromName(hProcess, "dwmcore!CGlobalCompositionSurfaceInfo::IsOverlayPrevented", g_pSymbolInfo))
    {
        fprintf(stderr, "Failed to get OffsetIsOverlayPrevented\n");
        return;
    }
    g_offsets[OffsetIsOverlayPrevented] = g_pSymbolInfo->Address - g_dwmcoreBase;

    if (!SymFromName(hProcess, "dwmcore!CCommonRegistryData::ForceFullDirtyRendering", g_pSymbolInfo))
    {
        fprintf(stderr, "Failed to get OffsetForceDirtyRendering\n");
        return;
    }
    g_offsets[OffsetForceDirtyRendering] = g_pSymbolInfo->Address - g_dwmcoreBase;

    // hook present function
    if (MH_Initialize() != MH_OK)
    {
        fprintf(stderr, "Unable to initialize MinHook\n");
        return;
    }
    if (MH_CreateHook((void*)(g_dwmcoreBase + g_offsets[OffsetPresent]), (void*)HookPresent, (void**)(&OriginalPresent)) != MH_OK)
    {
        fprintf(stderr, "Unable to initialize COverlayContext::Present hook\n");
        return;
    }
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK)
    {
        fprintf(stderr, "Unable to enable hooks\n");
        return;
    }
}

BOOL WINAPI DllMain(HMODULE hDll, DWORD dwReason, LPVOID reserved)
{
    switch (dwReason)
    {
        case DLL_PROCESS_ATTACH:
            AllocConsole();
            SetConsoleTitleA("Dumper");
            freopen("CONOUT$", "w", stderr);
            DumperInit();
            break;

        case DLL_THREAD_ATTACH:
            break;

        case DLL_THREAD_DETACH:
            break;

        case DLL_PROCESS_DETACH:
            break;
    }
    return TRUE;  // Successful DLL_PROCESS_ATTACH.
}