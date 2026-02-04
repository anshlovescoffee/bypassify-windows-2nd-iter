/*
gpthelper.hpp
Header for GPT API integration via backend proxy
Supports multiple models fetched from backend, model cycling
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

    // Model info as returned by the backend /api/models endpoint
    struct ModelInfo
    {
        std::string id;            // e.g. "gpt-4o", "claude-sonnet-4-20250514"
        std::string displayName;   // e.g. "GPT 4o", "Claude Sonnet 4.5"
        std::string provider;      // "openai", "anthropic", "google"
        bool supportsVision = true;
    };

    struct GPTConfig
    {
        std::string model = "gpt-4o";  // Default model ID
        std::string backendHost = "secure-bypassify-backend-production.up.railway.app";
        int maxTokens = 10000;
        float temperature = 0.7f;
    };

    // Initialize the GPT helper with configuration
    bool Initialize(const GPTConfig& config);
    
    // Send a message with optional images and get a response
    // Routes through the backend proxy with HWID auth
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

    // ============================================
    // Model Management
    // ============================================

    // Fetch available models from backend (blocking). Returns true on success.
    bool FetchModels();

    // Async version - fetches models in background
    void FetchModelsAsync(std::function<void(bool)> callback = nullptr);

    // Get list of available models (populated after FetchModels)
    const std::vector<ModelInfo>& GetModels();

    // Get the currently selected model info (returns nullptr if none)
    const ModelInfo* GetCurrentModel();

    // Get the currently selected model ID string
    const std::string& GetCurrentModelId();

    // Get the currently selected model display name
    std::string GetCurrentModelDisplayName();

    // Set the current model by ID. Returns true if the model exists.
    bool SetModel(const std::string& modelId);

    // Cycle to the next model in the list. Returns the new model's display name.
    std::string CycleModel();

    // Get the index of the currently selected model (for UI)
    int GetCurrentModelIndex();

    // Set model by index. Returns true if valid index.
    bool SetModelByIndex(int index);
    
    // Cleanup
    void Shutdown();
}