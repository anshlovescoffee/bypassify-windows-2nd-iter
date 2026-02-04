/*
auth.hpp
Hardware ID authorization check via Supabase
*/

#pragma once

#include <string>

namespace Auth
{
    enum class AuthResult
    {
        Authorized,
        NotAuthorized,
        ConnectionError
    };
    
    // Get the machine's hardware ID (MachineGuid from registry)
    std::string GetHardwareId();
    
    // Check if the current machine is authorized
    AuthResult CheckAuthorization();
    
    // Simple bool version (for DLL where we don't need UI)
    bool IsAuthorized();
}