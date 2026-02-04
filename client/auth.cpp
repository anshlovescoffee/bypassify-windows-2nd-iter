/*
auth.cpp
Hardware ID authorization via Supabase REST API
Uses WinHTTP for network requests (no external dependencies)
*/

#include "auth.hpp"
#include <windows.h>
#include <winhttp.h>
#include <string>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "advapi32.lib")

// Supabase config - same as the C# AuthService
static const wchar_t* SUPABASE_HOST = L"yhpfodqpaalqrnxsprxi.supabase.co";
static const char* SUPABASE_ANON_KEY = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6InlocGZvZHFwYWFscXJueHNwcnhpIiwicm9sZSI6ImFub24iLCJpYXQiOjE3NjE4NTcwMzQsImV4cCI6MjA3NzQzMzAzNH0.FffTC6IgucOXbxXwJDvlib0NNeidvWcp121Fjd6SJoc";
static const char* TABLE_NAME = "windows_hwids";

std::string Auth::GetHardwareId()
{
    HKEY hKey = NULL;
    char value[256] = {0};
    DWORD valueSize = sizeof(value);
    DWORD type = REG_SZ;
    
    LONG result = RegOpenKeyExA(
        HKEY_LOCAL_MACHINE,
        "SOFTWARE\\Microsoft\\Cryptography",
        0,
        KEY_READ | KEY_WOW64_64KEY,
        &hKey
    );
    
    if (result != ERROR_SUCCESS)
    {
        return "";
    }
    
    result = RegQueryValueExA(hKey, "MachineGuid", NULL, &type, (LPBYTE)value, &valueSize);
    RegCloseKey(hKey);
    
    if (result != ERROR_SUCCESS)
    {
        return "";
    }
    
    return std::string(value);
}

Auth::AuthResult Auth::CheckAuthorization()
{
    std::string hwid = GetHardwareId();
    if (hwid.empty())
    {
        return AuthResult::NotAuthorized;
    }
    
    // Build query path: /rest/v1/windows_hwids?hwid=eq.<hwid>&select=id,hwid
    std::string path = "/rest/v1/";
    path += TABLE_NAME;
    path += "?hwid=eq.";
    path += hwid;
    path += "&select=id,hwid";
    
    // Convert path to wide string
    std::wstring wPath(path.begin(), path.end());
    
    // Build auth header
    std::string anonKey(SUPABASE_ANON_KEY);
    std::wstring apikeyHeader = L"apikey: ";
    apikeyHeader += std::wstring(anonKey.begin(), anonKey.end());
    
    std::wstring authHeader = L"Authorization: Bearer ";
    authHeader += std::wstring(anonKey.begin(), anonKey.end());
    
    // WinHTTP request
    HINTERNET hSession = NULL;
    HINTERNET hConnect = NULL;
    HINTERNET hRequest = NULL;
    AuthResult result = AuthResult::ConnectionError;
    
    hSession = WinHttpOpen(
        L"Bypassify/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );
    
    if (!hSession) return AuthResult::ConnectionError;
    
    hConnect = WinHttpConnect(hSession, SUPABASE_HOST, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect)
    {
        WinHttpCloseHandle(hSession);
        return AuthResult::ConnectionError;
    }
    
    hRequest = WinHttpOpenRequest(
        hConnect,
        L"GET",
        wPath.c_str(),
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE
    );
    
    if (!hRequest)
    {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return AuthResult::ConnectionError;
    }
    
    // Add headers
    WinHttpAddRequestHeaders(hRequest, apikeyHeader.c_str(), -1, WINHTTP_ADDREQ_FLAG_ADD);
    WinHttpAddRequestHeaders(hRequest, authHeader.c_str(), -1, WINHTTP_ADDREQ_FLAG_ADD);
    
    // Send
    BOOL bSend = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    
    if (bSend)
    {
        bSend = WinHttpReceiveResponse(hRequest, NULL);
    }
    
    if (!bSend)
    {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return AuthResult::ConnectionError;
    }
    
    // Check HTTP status
    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);
    
    if (statusCode == 200)
    {
        // Read response body
        std::string response;
        DWORD dwSize = 0;
        DWORD dwDownloaded = 0;
        
        do
        {
            dwSize = 0;
            WinHttpQueryDataAvailable(hRequest, &dwSize);
            if (dwSize == 0) break;
            
            char* buf = new char[dwSize + 1];
            ZeroMemory(buf, dwSize + 1);
            WinHttpReadData(hRequest, buf, dwSize, &dwDownloaded);
            response.append(buf, dwDownloaded);
            delete[] buf;
        } while (dwSize > 0);
        
        // Supabase returns JSON array: [] = not found, [{...}] = found
        size_t start = response.find('[');
        size_t end = response.rfind(']');
        if (start != std::string::npos && end != std::string::npos && end > start + 1)
        {
            result = AuthResult::Authorized;
        }
        else
        {
            result = AuthResult::NotAuthorized;
        }
    }
    else
    {
        result = AuthResult::ConnectionError;
    }
    
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    return result;
}

bool Auth::IsAuthorized()
{
    return CheckAuthorization() == AuthResult::Authorized;
}