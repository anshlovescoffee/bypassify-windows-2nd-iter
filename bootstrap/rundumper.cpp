/*
rundumper.cpp
Launcher for the dumper program that injects dumper into dwm.exe and then patches latest offsets into the client dll
*/

#include "shared.hpp"
#include "offsetidx.h"

#include <filesystem>
#include <fstream>
#include <vector>
#include <unordered_map>
#include <unordered_set>


bool ReadOffsets(uintptr_t* offsets)
{
    std::ifstream offsetFile("C:\\DwmDump\\offsets.blob", std::ios::in | std::ios::binary | std::ios::ate);
    size_t offsetFileSize = 0;

    if (offsetFile.fail())
    {
        std::cout << "Unable to open offset blob" << std::endl;
        return false;
    }
    offsetFileSize = offsetFile.tellg();
    if (offsetFileSize != (sizeof(uintptr_t) * MAX_OFFSET))
    {
        std::cout << "WARNING! Offset blob is invalid" << std::endl;
        offsetFile.close();
        return false;
    }
    offsetFile.seekg(0, std::ios::beg);
    offsetFile.read(reinterpret_cast<char*>(offsets), offsetFileSize);
    offsetFile.close();
    return true;
}

bool PatchClient()
{
    uintptr_t offsets[MAX_OFFSET];
    size_t clientFileSize = 0;
    std::fstream clientFile;
    char* clientData = nullptr;
    PIMAGE_DOS_HEADER pDosHeader = nullptr;
    PIMAGE_NT_HEADERS pNtHeaders = nullptr;
    PIMAGE_OPTIONAL_HEADER pOptionalHeader = nullptr;
    PIMAGE_SECTION_HEADER pSectionHeader = nullptr;
    PIMAGE_EXPORT_DIRECTORY pExportDirectory = nullptr;
    DWORD* pExportAddressTable = nullptr;
    DWORD vaExportDirectory = 0;
    std::vector<PIMAGE_SECTION_HEADER> sectionList;
    std::unordered_map<std::string, char*> symbolToFilePointer; // maps where the data corresponding to each export symbol lives in the dll file
    DWORD vaOffsetTable = 0;
    uintptr_t* pOffsetTable = 0;
    bool patchSuccess = false;

    if (!ReadOffsets(offsets))
    {
        return false;
    }

    clientFile = std::fstream("client.dll", std::ios::in | std::ios::binary | std::ios::ate);
    
    if (clientFile.fail())
    {
        std::cout << "Unable to get client file data" << std::endl;
        return false;
    }
    clientFileSize = clientFile.tellg();
    clientData = new char[clientFileSize];
    if (!clientData)
    {
        std::cout << "not enough memory" << std::endl;
        clientFile.close();
        return false;
    }
    clientFile.seekg(0, std::ios::beg);
    clientFile.read(clientData, clientFileSize);
    clientFile.close();

    pDosHeader = reinterpret_cast<PIMAGE_DOS_HEADER>(clientData);
    if (pDosHeader->e_magic != 0x5a4d)
    {
        std::cout << "Invalid DOS header in client.dll" << std::endl;
        delete[] clientData;
        return false;
    }

    pNtHeaders = reinterpret_cast<PIMAGE_NT_HEADERS>(clientData + pDosHeader->e_lfanew);
    if (pNtHeaders->Signature != 0x4550)
    {
        std::cout << "Invalid NT Header in client.dll" << std::endl;
        delete[] clientData;
        return false;
    }
    
    pOptionalHeader = &pNtHeaders->OptionalHeader;
    pSectionHeader = IMAGE_FIRST_SECTION(pNtHeaders);
    vaExportDirectory = pOptionalHeader->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
    printf("Export directory virtual address: 0x%x\n", vaExportDirectory);
    for (int i = 0; i < pNtHeaders->FileHeader.NumberOfSections; ++i, ++pSectionHeader)
    {
        if (!pSectionHeader->SizeOfRawData)
        {
            continue;
        }
        if ((vaExportDirectory >= pSectionHeader->VirtualAddress) && (vaExportDirectory < (pSectionHeader->VirtualAddress + pSectionHeader->SizeOfRawData)))
        {
            pExportDirectory = reinterpret_cast<PIMAGE_EXPORT_DIRECTORY>(clientData + pSectionHeader->PointerToRawData + vaExportDirectory - pSectionHeader->VirtualAddress);
            pExportAddressTable = reinterpret_cast<DWORD*>(clientData + pSectionHeader->PointerToRawData + pExportDirectory->AddressOfFunctions - pSectionHeader->VirtualAddress);
            vaOffsetTable = pExportAddressTable[0]; // client.dll only exports one entry so the one export is guaranteed to be the offset table
            printf("Offset Table: client.dll+0x%x\n", vaOffsetTable);
            break;
        }
    }

    if (!vaOffsetTable)
    {
        std::cout << "Unable to find offset table" << std::endl;
        delete[] clientData;
        return false;
    }

    pSectionHeader = IMAGE_FIRST_SECTION(pNtHeaders);
    for (int i = 0; i < pNtHeaders->FileHeader.NumberOfSections; ++i, ++pSectionHeader)
    {
        if (!pSectionHeader->SizeOfRawData)
        {
            continue;
        }
        if ((vaOffsetTable >= pSectionHeader->VirtualAddress) && (vaOffsetTable < (pSectionHeader->VirtualAddress + pSectionHeader->SizeOfRawData)))
        {
            pOffsetTable = reinterpret_cast<uintptr_t*>(clientData + pSectionHeader->PointerToRawData + vaOffsetTable - pSectionHeader->VirtualAddress);
            memcpy(pOffsetTable, offsets, sizeof(offsets));
            std::cout << "new offsets successfully copied" << std::endl;
            patchSuccess = true;
            break;
        }
    }

    if (!patchSuccess)
    {
        std::cout << "Unable to patch client.dll" << std::endl;
        delete[] clientData;
        return false;
    }

    clientFile = std::fstream("client.dll", std::ios::out | std::ios::binary);
    if (clientFile.fail())
    {
        std::cout << "Unable to write to client.dll" << std::endl;
        delete[] clientData;
        return false;
    }
    clientFile.write(clientData, clientFileSize);
    clientFile.close();
    return true;
}

int main()
{
    char currentDirectory[MAX_PATH];
    FixCwd();
    if (!GetCurrentDirectoryA(MAX_PATH, currentDirectory))
    {
        std::cout << "failed to get current directory" << std::endl;
        return 1;
    }

    if (std::filesystem::exists("C:\\DwmDump")) // clean old dump files
    {
        if (std::filesystem::remove_all("C:\\DwmDump") == -1)
        {
            std::cout << "WARNING: Unable to clear old dumps" << std::endl;
        }
    }
    if (!std::filesystem::create_directory("C:\\DwmDump")) // initialize dump folder
    {
        std::cout << "failed to initialize dump folder" << std::endl;
        return 1;
    }

    if (StandardMap("dwm.exe", std::string(currentDirectory) + "\\symsrv.dll"))
    {
        std::cout << "Unable to inject symsrv.dll" << std::endl;
        return 1;
    }
    if (StandardMap("dwm.exe", std::string(currentDirectory) + "\\dbghelp.dll"))
    {
        std::cout << "Unable to inject dbghelp.dll" << std::endl;
        return 1;
    }
    if (StandardMap("dwm.exe", std::string(currentDirectory) + "\\dumper.dll"))
    {
        std::cout << "Unable to inject dumper" << std::endl;
        return 1;
    }

    Sleep(1000);
    if (!std::filesystem::exists("C:\\DwmDump\\offsets.blob"))
    {
        std::cout << "Dump did not succeed!" << std::endl;
        return 1;
    }

    std::cout << "Dump succeeded!" << std::endl;

    if (PatchClient())
    {
        std::cout << "Successfully updated client.dll with new offsets!" << std::endl;
    }
    else
    {
        std::cout << "Unable to update client.dll" << std::endl;
    }

    std::cout << "Press enter to exit program..." << std::endl;
    std::cin.get();
    TerminateProcess(getProcessHandle(getProcessId("dwm.exe")), 0);
    return 0;
}