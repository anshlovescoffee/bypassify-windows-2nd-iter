/*
gpthelper.cpp
GPT API integration implementation using WinHTTP
*/

#include "gpthelper.hpp"
#include <windows.h>
#include <winhttp.h>
#include <thread>
#include <mutex>
#include <atomic>

#pragma comment(lib, "winhttp.lib")

using namespace GPTHelper;

static GPTConfig g_config;
static std::string g_lastError;
static std::atomic<bool> g_requestPending(false);
static std::mutex g_mutex;
static bool g_initialized = false;

// Simple JSON escape function
static std::string EscapeJson(const std::string& str)
{
    std::string result;
    result.reserve(str.length() * 2);
    
    for (char c : str)
    {
        switch (c)
        {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    result += buf;
                }
                else
                {
                    result += c;
                }
                break;
        }
    }
    return result;
}

// Build JSON request body
static std::string BuildRequestBody(const std::string& prompt, const std::vector<std::string>& base64Images)
{
    std::string json = "{";
    json += "\"model\":\"" + g_config.model + "\",";
    json += "\"max_tokens\":" + std::to_string(g_config.maxTokens) + ",";
    json += "\"temperature\":" + std::to_string(g_config.temperature) + ",";
    json += "\"messages\":[{\"role\":\"user\",\"content\":[";
    
    // Add text content
    json += "{\"type\":\"text\",\"text\":\"" + EscapeJson(prompt) + "\"}";
    
    // Add images if present
    for (const auto& img : base64Images)
    {
        json += ",{\"type\":\"image_url\",\"image_url\":{\"url\":\"data:image/png;base64," + img + "\",\"detail\":\"auto\"}}";
    }
    
    json += "]}]}";
    return json;
}

// Extract response text from JSON (simple parser)
static std::string ExtractResponse(const std::string& json)
{
    // Find "content":" in the response
    const char* contentKey = "\"content\":\"";
    size_t pos = json.find(contentKey);
    if (pos == std::string::npos)
    {
        // Try alternative format (content can also be null or in different format)
        contentKey = "\"content\": \"";
        pos = json.find(contentKey);
    }
    
    if (pos == std::string::npos)
    {
        // Check for error message
        const char* errorKey = "\"message\":\"";
        pos = json.find(errorKey);
        if (pos != std::string::npos)
        {
            pos += strlen(errorKey);
            size_t endPos = json.find("\"", pos);
            if (endPos != std::string::npos)
            {
                g_lastError = "API Error: " + json.substr(pos, endPos - pos);
            }
        }
        return "";
    }
    
    pos += strlen(contentKey);
    
    // Find the end of the content string (handle escaped quotes)
    std::string result;
    bool escaped = false;
    
    for (size_t i = pos; i < json.length(); ++i)
    {
        char c = json[i];
        
        if (escaped)
        {
            switch (c)
            {
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                case '"': result += '"'; break;
                case '\\': result += '\\'; break;
                default: result += c; break;
            }
            escaped = false;
        }
        else if (c == '\\')
        {
            escaped = true;
        }
        else if (c == '"')
        {
            break; // End of string
        }
        else
        {
            result += c;
        }
    }
    
    return result;
}

bool GPTHelper::Initialize(const GPTConfig& config)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_config = config;
    g_initialized = true;
    g_lastError.clear();
    return true;
}

void GPTHelper::SetApiKey(const std::string& apiKey)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_config.apiKey = apiKey;
}

std::string GPTHelper::SendRequest(const std::string& prompt, const std::vector<std::string>& base64Images)
{
    if (!g_initialized)
    {
        g_lastError = "GPTHelper not initialized";
        return "";
    }
    
    if (g_config.apiKey.empty())
    {
        g_lastError = "API key not set";
        return "";
    }
    
    g_requestPending = true;
    std::string result;
    
    HINTERNET hSession = NULL;
    HINTERNET hConnect = NULL;
    HINTERNET hRequest = NULL;
    
    // Initialize WinHTTP
    hSession = WinHttpOpen(L"DWM Overlay GPT/1.0",
                           WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                           WINHTTP_NO_PROXY_NAME,
                           WINHTTP_NO_PROXY_BYPASS, 0);
    
    if (!hSession)
    {
        g_lastError = "Failed to open WinHTTP session";
        g_requestPending = false;
        return "";
    }
    
    // Connect to server
    hConnect = WinHttpConnect(hSession, L"api.openai.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    
    if (!hConnect)
    {
        g_lastError = "Failed to connect to API server";
        WinHttpCloseHandle(hSession);
        g_requestPending = false;
        return "";
    }
    
    // Create request
    hRequest = WinHttpOpenRequest(hConnect, L"POST", L"/v1/chat/completions",
                                  NULL, WINHTTP_NO_REFERER,
                                  WINHTTP_DEFAULT_ACCEPT_TYPES,
                                  WINHTTP_FLAG_SECURE);
    
    if (!hRequest)
    {
        g_lastError = "Failed to create HTTP request";
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        g_requestPending = false;
        return "";
    }
    
    // Build headers
    std::wstring authHeader = L"Authorization: Bearer ";
    for (char c : g_config.apiKey)
    {
        authHeader += static_cast<wchar_t>(c);
    }
    
    std::wstring contentType = L"Content-Type: application/json";
    
    WinHttpAddRequestHeaders(hRequest, authHeader.c_str(), -1, WINHTTP_ADDREQ_FLAG_ADD);
    WinHttpAddRequestHeaders(hRequest, contentType.c_str(), -1, WINHTTP_ADDREQ_FLAG_ADD);
    
    // Build request body
    std::string body = BuildRequestBody(prompt, base64Images);
    
    // Send request
    BOOL bResults = WinHttpSendRequest(hRequest,
                                       WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                       (LPVOID)body.c_str(), (DWORD)body.length(),
                                       (DWORD)body.length(), 0);
    
    if (!bResults)
    {
        g_lastError = "Failed to send request";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        g_requestPending = false;
        return "";
    }
    
    // Receive response
    bResults = WinHttpReceiveResponse(hRequest, NULL);
    
    if (!bResults)
    {
        g_lastError = "Failed to receive response";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        g_requestPending = false;
        return "";
    }
    
    // Read response data
    std::string response;
    DWORD dwSize = 0;
    DWORD dwDownloaded = 0;
    
    do
    {
        dwSize = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &dwSize))
        {
            break;
        }
        
        if (dwSize == 0)
        {
            break;
        }
        
        char* pszBuffer = new char[dwSize + 1];
        ZeroMemory(pszBuffer, dwSize + 1);
        
        if (WinHttpReadData(hRequest, pszBuffer, dwSize, &dwDownloaded))
        {
            response.append(pszBuffer, dwDownloaded);
        }
        
        delete[] pszBuffer;
    } while (dwSize > 0);
    
    // Parse response
    result = ExtractResponse(response);
    
    if (result.empty() && g_lastError.empty())
    {
        g_lastError = "Failed to parse response";
    }
    
    // Cleanup
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    
    g_requestPending = false;
    return result;
}

void GPTHelper::SendRequestAsync(const std::string& prompt,
                                  const std::vector<std::string>& base64Images,
                                  std::function<void(const std::string&)> callback)
{
    if (g_requestPending)
    {
        callback("");
        return;
    }
    
    std::thread([prompt, base64Images, callback]()
    {
        std::string result = SendRequest(prompt, base64Images);
        callback(result);
    }).detach();
}

bool GPTHelper::IsRequestPending()
{
    return g_requestPending;
}

std::string GPTHelper::GetLastError()
{
    return g_lastError;
}

void GPTHelper::Shutdown()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_initialized = false;
    g_config = GPTConfig();
    g_lastError.clear();
}