#include "injector.hpp"

// implementer of all functions that the injector implements

BOOL WINAPI Inject(LPCSTR procName, std::string dllPath, INJECTCONFIG config) {
    // getting a handle to process
    DWORD dwPid = getProcessId(procName);
    if (dwPid == 0) {
        std::cout << "process not open award" << std::endl;
        return TRUE;
    }
    HANDLE hProcess = getProcessHandle(dwPid);
    if (hProcess == INVALID_HANDLE_VALUE) {
        std::cout << "failed to get handle award" << std::endl;
        return TRUE;
    }

    // initializing some parameters and parsing exports
    LPVOID dllBase = ManualMap(hProcess, dllPath, config);
    printf("DLL base: 0x%llx\n", dllBase);
    if (dllBase == NULL) {
        std::cout << "Failed to inject DLL award" << std::endl;
        return TRUE;
    }
    return FALSE;
}

BOOL WINAPI StandardMap(LPCSTR procName, std::string dllPath)
{
    // getting a handle to process
    DWORD dwPid = getProcessId(procName);
    FARPROC loadLibraryBase = GetProcAddress(GetModuleHandleA("kernel32.dll"), "LoadLibraryA");

    if (dwPid == 0) {
        std::cout << "process not open award" << std::endl;
        return TRUE;
    }
    HANDLE hProcess = getProcessHandle(dwPid);
    if (hProcess == NULL) {
        std::cout << "failed to get handle award" << std::endl;
        return TRUE;
    }

    LPVOID pParam = VirtualAllocEx(hProcess, NULL, dllPath.size() + 1, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!pParam)
    {
        std::cout << "failed to allocate parameters award" << std::endl;
        return TRUE;
    }

    if (NtWriteVirtualMemory(hProcess, pParam, (LPVOID)dllPath.c_str(), dllPath.size() + 1, NULL))
    {
        std::cout << "Failed to write parameters award" << std::endl;
        VirtualFreeEx(hProcess, pParam, 0, MEM_RELEASE);
        return TRUE;
    }

    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)loadLibraryBase, pParam, 0, NULL);
    if (!hThread)
    {
        std::cout << "Failed to create thread award\n" << std::endl;
        VirtualFreeEx(hProcess, pParam, 0, MEM_RELEASE);
        return TRUE;
    }

    DWORD exitCode = 0;
    WaitForSingleObject(hThread, INFINITE);
    VirtualFreeEx(hProcess, pParam, 0, MEM_RELEASE);
    if (!GetExitCodeThread(hThread, &exitCode))
    {
        std::cout << "Failed to get thread exit code award" << std::endl;
        return TRUE;
    }

    return exitCode == 0;
}

/*
void FixCwd()
{
    WCHAR exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    PathCchRemoveFileSpec(exePath, MAX_PATH);
    SetCurrentDirectoryW(exePath);
}

char currentDirectory[MAX_PATH + 1];
int main() {
    FixCwd();
    //DebugMap("C:\\Users\\chaosium\\ajeagle\\ajsploit\\build\\ajsploitinternal.dll", {0});
    if (!GetCurrentDirectoryA(MAX_PATH, currentDirectory)) {
        std::cout << "failed to get current directory award" << std::endl;
        return 1;
    }
    INJECTCONFIG config = {0};
    config.EraseHeader = 1;
    config.EnableSEH = 1;
    config.EnableTLS = 1;
    return Inject("dwm.exe", std::string(currentDirectory) + "\\client.dll", config);
}
*/