/*
gpthelper.hpp
Header for GPT API integration
*/

#pragma once

#include <string>
#include <vector>
#include <functional>

namespace GPTHelper
{
    struct GPTMessage
    {
        std::string role;      // "user", "assistant", or "system"
        std::string content;   // text content
        std::vector<std::string> images; // base64 encoded images (for user messages)
    };

    struct GPTConfig
    {
        std::string apiKey;
        std::string model = "gpt-4o";  // Default to GPT-4o for vision
        std::string endpoint = "https://api.openai.com/v1/chat/completions";
        int maxTokens = 10000;
        float temperature = 0.7f;
    };

    // Initialize the GPT helper with configuration
    bool Initialize(const GPTConfig& config);
    
    // Set API key (can be called after initialization)
    void SetApiKey(const std::string& apiKey);
    
    // Send a message with optional images and get a response
    // Returns empty string on failure
    std::string SendRequest(const std::string& prompt, const std::vector<std::string>& base64Images = {});
    
    // Async version - calls callback when done
    void SendRequestAsync(const std::string& prompt, 
                          const std::vector<std::string>& base64Images,
                          std::function<void(const std::string&)> callback);
    
    // Check if a request is currently in progress
    bool IsRequestPending();
    
    // Get last error message
    std::string GetLastError();
    
    // Cleanup
    void Shutdown();
}