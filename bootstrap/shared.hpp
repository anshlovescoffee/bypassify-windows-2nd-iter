/*
shared.hpp
Shared code for all launchers
*/

#include <iostream>
#include <string>
#include <windows.h>
#include <pathcch.h>

#include "injector.hpp"

// making sure that the CWD is always inside the directory of the dumper no matter where it is run from
void FixCwd()
{
    WCHAR exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    PathCchRemoveFileSpec(exePath, MAX_PATH);
    SetCurrentDirectoryW(exePath);
}