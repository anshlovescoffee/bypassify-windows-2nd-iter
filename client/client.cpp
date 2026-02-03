/*
client.cpp
Implements the GUI for the overlay with GPT integration
*/

#define IMGUI_IMPL_WIN32_DISABLE_GAMEPAD

#include <stdio.h>
#include <Shlwapi.h>

#include "client.hpp"
#include "imagehelper.hpp"
#include "gpthelper.hpp"
#include "settings.hpp"
#include "settings_ui.hpp"
#include "imgui_internal.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

using namespace Client;

// window related variables
std::mutex Client::g_clientLock;
ID3D11Device1* m_pDevice = nullptr;
ID3D11DeviceContext1* m_pDeviceContext = nullptr;
HWND m_hWnd = FindWindowA("Progman", "Program Manager");
bool m_initialized;
float m_dpiScale = 0.0f;

// input related variables
bool m_enableInputBlocking = true;
bool m_inputBlockingActive = false;
bool m_enableClipboard = true;

// image subsystem related data
bool m_wantScreenshot = false;
static int m_screenshotState = 0;  // 0=idle, 1=hiding overlay, 2=take screenshot

// ============================================
// GPT Integration Variables
// ============================================
static const int MAX_GPT_SCREENSHOTS = 6;
static std::vector<ID3D11ShaderResourceView*> m_gptScreenshots;
static std::string m_gptError;
static bool m_gptRequestPending = false;
static bool m_overlayVisible = true;
static bool m_showGptWindow = true;
static float m_responseScrollOffset = 0.0f;  // Scroll offset for chat area
static bool m_autoScrollToBottom = false;     // Auto-scroll when new message arrives

// Chat message types
enum class ChatRole { User, Assistant, Error };

struct ChatMessage
{
    ChatRole role;
    std::string content;
};

static std::vector<ChatMessage> m_chatHistory;

// Hotkey state tracking
static bool m_lastScreenshotState = false;
static bool m_lastSendState = false;
static bool m_lastToggleState = false;
static bool m_lastSettingsState = false;
static bool m_lastQuitState = false;
static bool m_hasQuit = false;  // When true, stops ALL processing

// Movement step size in pixels
static const float MOVE_STEP = 5.0f;
static const float SCROLL_STEP = 5.0f;  // Scroll step in pixels

// Reactivation event - signaled by runclient to bring back the overlay
static HANDLE m_reactivateEvent = NULL;

// ============================================
// GPT Functions
// ============================================
void SendToGpt()
{
    const Settings::AppSettings& settings = Settings::Get();
    
    if (m_gptScreenshots.empty())
    {
        m_chatHistory.push_back({ ChatRole::Error, "No screenshots captured. Press " + settings.hotkeyScreenshot.ToString() + " to capture screenshots first." });
        m_autoScrollToBottom = true;
        return;
    }
    
    if (strlen(settings.apiKey) == 0)
    {
        m_chatHistory.push_back({ ChatRole::Error, "API key not set. Press " + settings.hotkeySettings.ToString() + " to open settings." });
        m_autoScrollToBottom = true;
        return;
    }
    
    if (m_gptRequestPending)
    {
        return;
    }
    
    // Convert screenshots to base64
    std::vector<std::string> base64Images;
    for (auto* screenshot : m_gptScreenshots)
    {
        std::string b64 = ImageHelper::ImageToBase64(screenshot, ImageHelper::IMAGE_CODEC::PNG);
        if (!b64.empty())
        {
            base64Images.push_back(b64);
        }
    }
    
    if (base64Images.empty())
    {
        m_chatHistory.push_back({ ChatRole::Error, "Failed to convert screenshots to base64." });
        m_autoScrollToBottom = true;
        return;
    }
    
    // Add user message to chat
    int screenshotCount = (int)m_gptScreenshots.size();
    std::string userMsg = "[" + std::to_string(screenshotCount) + " Screenshot" + (screenshotCount > 1 ? "s" : "") + "]";
    m_chatHistory.push_back({ ChatRole::User, userMsg });
    m_autoScrollToBottom = true;
    
    m_gptRequestPending = true;
    m_gptError.clear();
    
    // Set API key and send
    GPTHelper::SetApiKey(settings.apiKey);
    
    GPTHelper::SendRequestAsync(settings.prompt, base64Images, [](const std::string& response)
    {
        m_gptRequestPending = false;
        
        // Clear screenshots after request completes
        for (auto* screenshot : m_gptScreenshots)
        {
            screenshot->Release();
        }
        m_gptScreenshots.clear();
        
        if (response.empty())
        {
            std::string err = GPTHelper::GetLastError();
            if (err.empty()) err = "Unknown error occurred";
            m_chatHistory.push_back({ ChatRole::Error, err });
        }
        else
        {
            m_chatHistory.push_back({ ChatRole::Assistant, response });
        }
        m_autoScrollToBottom = true;
    });
}

void DrawGptWindow()
{
    if (!m_showGptWindow)
    {
        return;
    }
    
    const Settings::AppSettings& settings = Settings::Get();
    
    // Set window size and position from settings
    ImGui::SetNextWindowSize(ImVec2(settings.windowWidth, settings.windowHeight), ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(settings.windowPosX, settings.windowPosY), ImGuiCond_Always);
    
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMouseInputs | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoTitleBar;
    
    if (ImGui::Begin("##BypassifyMain", nullptr, windowFlags))
    {
        // Calculate alpha from transparency setting
        float alpha = 1.0f - settings.transparency;
        if (alpha < 0.15f) alpha = 0.15f;
        
        // Centered title
        std::string windowTitle = "Bypassify v1.1.0 - Pro Plan - " + settings.hotkeyQuit.ToString() + " to Quit";
        float titleWidth = ImGui::CalcTextSize(windowTitle.c_str()).x;
        ImGui::SetCursorPosX((settings.windowWidth - titleWidth) * 0.5f);
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, alpha), "%s", windowTitle.c_str());
        
        ImGui::Separator();
        
        // Screenshot count
        ImGui::Text("Screenshots: %d / %d", (int)m_gptScreenshots.size(), MAX_GPT_SCREENSHOTS);
        
        ImGui::Separator();
        
        // Chat area - takes up all remaining space minus hotkey bar
        float hotkeyTextHeight = ImGui::GetTextLineHeight();
        ImVec2 availSize = ImGui::GetContentRegionAvail();
        float chatHeight = availSize.y - hotkeyTextHeight;
        
        ImGui::BeginChild("ChatArea", ImVec2(0, chatHeight), true, ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMouseInputs);
        
        // Apply scroll offset
        ImGui::SetScrollY(m_responseScrollOffset);
        
        float chatWidth = ImGui::GetContentRegionAvail().x;
        
        if (m_chatHistory.empty() && !m_gptRequestPending)
        {
            // Placeholder when no messages
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, alpha), 
                "Take screenshots with %s, then press %s to send.",
                settings.hotkeyScreenshot.ToString().c_str(),
                settings.hotkeySend.ToString().c_str());
        }
        else
        {
            for (size_t i = 0; i < m_chatHistory.size(); i++)
            {
                const ChatMessage& msg = m_chatHistory[i];
                
                ImGui::PushTextWrapPos(chatWidth);
                
                if (msg.role == ChatRole::User)
                {
                    // User message - right-aligned, colored
                    ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, alpha), "You:");
                    ImGui::TextColored(ImVec4(0.8f, 0.9f, 1.0f, alpha), "  %s", msg.content.c_str());
                }
                else if (msg.role == ChatRole::Assistant)
                {
                    // AI response
                    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, alpha), "AI:");
                    ImGui::TextWrapped("  %s", msg.content.c_str());
                }
                else if (msg.role == ChatRole::Error)
                {
                    // Error message
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, alpha), "Error: %s", msg.content.c_str());
                }
                
                ImGui::PopTextWrapPos();
                
                // Spacing between messages
                if (i < m_chatHistory.size() - 1)
                {
                    ImGui::Spacing();
                    ImGui::Separator();
                    ImGui::Spacing();
                }
            }
            
            // Show "thinking" indicator
            if (m_gptRequestPending)
            {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, alpha), "AI is thinking...");
            }
        }
        
        // Auto-scroll to bottom when new message arrives
        if (m_autoScrollToBottom)
        {
            m_responseScrollOffset = ImGui::GetScrollMaxY();
            m_autoScrollToBottom = false;
        }
        
        // Clamp scroll offset to max scroll
        float maxScroll = ImGui::GetScrollMaxY();
        if (m_responseScrollOffset > maxScroll) m_responseScrollOffset = maxScroll;
        
        ImGui::EndChild();
        
        // Centered hotkey instructions at bottom
        std::string hotkeyInfo = settings.hotkeyScreenshot.ToString() + ": Screenshot | " +
                                  settings.hotkeySend.ToString() + ": Send | " +
                                  settings.hotkeyToggle.ToString() + ": Hide | " +
                                  settings.hotkeySettings.ToString() + ": Settings";
        float hotkeyWidth = ImGui::CalcTextSize(hotkeyInfo.c_str()).x;
        ImGui::SetCursorPosX((settings.windowWidth - hotkeyWidth) * 0.5f);
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, alpha), "%s", hotkeyInfo.c_str());
    }
    ImGui::End();
}

// ============================================
// Hotkey Processing
// ============================================
void ProcessHotkeys()
{
    // Don't process anything if we've quit
    if (m_hasQuit) return;
    
    // Don't process hotkeys while user is setting a new hotkey
    if (SettingsUI::IsCapturingHotkey())
    {
        // Reset all states so hotkeys don't trigger when capture ends
        m_lastScreenshotState = true;
        m_lastSendState = true;
        m_lastToggleState = true;
        m_lastSettingsState = true;
        m_lastQuitState = true;
        return;
    }
    
    Settings::AppSettings& settings = Settings::GetMutable();
    
    // Quit hotkey - stops all processing
    if (settings.hotkeyQuit.IsJustPressed(m_lastQuitState))
    {
        Settings::Save();  // Save settings before quitting
        m_hasQuit = true;
        m_overlayVisible = false;
        m_showGptWindow = false;
        return;  // Stop processing immediately
    }
    
    // Screenshot hotkey
    if (settings.hotkeyScreenshot.IsJustPressed(m_lastScreenshotState))
    {
        if (m_gptScreenshots.size() < MAX_GPT_SCREENSHOTS)
        {
            m_wantScreenshot = true;
        }
    }
    
    // Send hotkey
    if (settings.hotkeySend.IsJustPressed(m_lastSendState))
    {
        SendToGpt();
    }
    
    // Toggle overlay hotkey
    if (settings.hotkeyToggle.IsJustPressed(m_lastToggleState))
    {
        m_overlayVisible = !m_overlayVisible;
    }
    
    // Settings hotkey
    if (settings.hotkeySettings.IsJustPressed(m_lastSettingsState))
    {
        SettingsUI::Toggle();
    }
    
    // Movement hotkeys - use IsPressed to allow holding
    // Get screen dimensions
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    
    if (settings.hotkeyMoveUp.IsPressed())
    {
        settings.windowPosY -= MOVE_STEP;
    }
    if (settings.hotkeyMoveDown.IsPressed())
    {
        settings.windowPosY += MOVE_STEP;
    }
    if (settings.hotkeyMoveLeft.IsPressed())
    {
        settings.windowPosX -= MOVE_STEP;
    }
    if (settings.hotkeyMoveRight.IsPressed())
    {
        settings.windowPosX += MOVE_STEP;
    }
    
    // Clamp to screen edges
    if (settings.windowPosX < 0) settings.windowPosX = 0;
    if (settings.windowPosY < 0) settings.windowPosY = 0;
    if (settings.windowPosX + settings.windowWidth > screenWidth)
        settings.windowPosX = screenWidth - settings.windowWidth;
    if (settings.windowPosY + settings.windowHeight > screenHeight)
        settings.windowPosY = screenHeight - settings.windowHeight;
    
    // Scroll hotkeys - allow holding
    if (settings.hotkeyScrollUp.IsPressed())
    {
        m_responseScrollOffset -= SCROLL_STEP;
        if (m_responseScrollOffset < 0) m_responseScrollOffset = 0;
    }
    if (settings.hotkeyScrollDown.IsPressed())
    {
        m_responseScrollOffset += SCROLL_STEP;
    }
}

void DrawMenu()
{
    // Check if runclient signaled us to reactivate
    if (m_hasQuit && m_reactivateEvent)
    {
        if (WaitForSingleObject(m_reactivateEvent, 0) == WAIT_OBJECT_0)
        {
            // Reactivate the overlay
            m_hasQuit = false;
            m_overlayVisible = true;
            m_showGptWindow = true;
            
            // Reset all hotkey states to prevent immediate re-trigger
            m_lastScreenshotState = true;
            m_lastSendState = true;
            m_lastToggleState = true;
            m_lastSettingsState = true;
            m_lastQuitState = true;
            
            ResetEvent(m_reactivateEvent);  // Reset for next time
        }
    }
    
    // Skip ALL processing if we've quit
    if (m_hasQuit) return;
    
    // Draw UI first so hotkey capture state is updated
    if (m_overlayVisible)
    {
        DrawGptWindow();
        SettingsUI::Draw();
    }
    
    // Process hotkeys AFTER UI so capture state is current
    ProcessHotkeys();
}

// ============================================
// Style Setup
// ============================================
void SetupStyle(float dpiScale)
{
    ImGuiStyle& style = ImGui::GetStyle();
    
    // Apply theme from settings
    Settings::ApplyTheme(Settings::Get().theme);
    
    // Style settings
    style.WindowRounding = 10.0f;
    style.ChildRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.PopupRounding = 8.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding = 6.0f;
    style.TabRounding = 6.0f;
    
    style.ScrollbarSize = 0.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    
    style.ScaleAllSizes(dpiScale);
}

CLIENT_STATUS Client::Initialize(ID3D11Device1* pDevice, float dpiScale)
{
    if (m_initialized)
    {
        return CLIENT_STATUS::ALREADY_DONE;
    }
    if (!pDevice)
    {
        return CLIENT_STATUS::INVALID_PARAM;
    }
    if (!m_hWnd)
    {
        fprintf(stderr, "No window award\n");
        return CLIENT_STATUS::FAILURE;
    }

    m_pDevice = pDevice;
    pDevice->AddRef();
    m_pDevice->GetImmediateContext1(&m_pDeviceContext);
    
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    
    // Initialize settings system FIRST
    Settings::Initialize();
    SettingsUI::Initialize();
    
    // Load font
    float fontSize = 16.0f * dpiScale;
    
    ImFontConfig fontConfig = {};
    fontConfig.OversampleH = 2;
    fontConfig.OversampleV = 2;
    fontConfig.PixelSnapH = true;
    
    io.Fonts->AddFontDefault(&fontConfig);
    io.Fonts->Build();
    
    // Setup style with settings
    SetupStyle(dpiScale);
    
    ImGui_ImplWin32_Init(m_hWnd);
    ImGui_ImplDX11_Init(m_pDevice, m_pDeviceContext);

    m_initialized = true;
    m_dpiScale = dpiScale;
    
    // Create reactivation event (manual reset, initially non-signaled)
    // Use Global\ prefix for cross-session access (dwm.exe runs in Session 0)
    m_reactivateEvent = CreateEventA(NULL, TRUE, FALSE, "Global\\RedactedOverlayReactivate");
    
    // Initialize GPT Helper
    GPTHelper::GPTConfig gptConfig;
    gptConfig.model = "gpt-4o";
    gptConfig.maxTokens = 2048;
    GPTHelper::Initialize(gptConfig);
    
    return InputInitialize();
}

CLIENT_STATUS Client::Uninitialize()
{
    if (!m_initialized)
    {
        return CLIENT_STATUS::ALREADY_DONE;
    }

    // Save settings before exit
    Settings::Save();

    if (m_pDevice)
    {
        m_pDevice->Release();
        m_pDevice = nullptr; 
    }
    if (m_pDeviceContext)
    {
        m_pDeviceContext->Release();
        m_pDeviceContext = nullptr;
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    m_initialized = false;
    
    // Free GPT screenshots
    for (ID3D11ShaderResourceView* item : m_gptScreenshots)
    {
        item->Release();
    }
    m_gptScreenshots.clear();
    
    GPTHelper::Shutdown();
    
    return InputUninitialize();
}

CLIENT_STATUS Client::NextFrame(ID3D11Resource* pDxBuffer)
{
    ID3D11RenderTargetView* pRenderTargetView = nullptr;
    CLIENT_STATUS status = CLIENT_STATUS::SUCCESS;
    ImGuiIO& io = ImGui::GetIO();

    if (!m_initialized)
    {
        return CLIENT_STATUS::FAILURE;
    }

    InputFunction();

    if (!pDxBuffer)
    {
        return CLIENT_STATUS::INVALID_PARAM;
    }
    if (m_pDevice->GetDeviceRemovedReason() != S_OK)
    {
        Uninitialize();
        return CLIENT_STATUS::FAILURE;
    }

    // Screenshot state machine
    // State 0: Normal operation
    // State 1: Overlay hidden, waiting one frame for screen to update
    // State 2: Take screenshot, restore overlay
    
    if (m_wantScreenshot && m_screenshotState == 0)
    {
        // Start screenshot sequence - hide overlay
        m_screenshotState = 1;
        m_wantScreenshot = false;
    }
    
    bool shouldRenderOverlay = m_overlayVisible && (m_screenshotState == 0);

    m_pDevice->CreateRenderTargetView(pDxBuffer, nullptr, &pRenderTargetView);
    status = (pRenderTargetView) ? CLIENT_STATUS::SUCCESS : CLIENT_STATUS::FAILURE;

    if (status == CLIENT_STATUS::SUCCESS)
    {
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        DrawMenu();
        m_pDeviceContext->OMSetRenderTargets(1, &pRenderTargetView, nullptr);
        ImGui::Render();
        
        if (shouldRenderOverlay)
        {
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }
    }

    // Handle screenshot states
    if (m_screenshotState == 1)
    {
        // Wait one frame for the screen to render without overlay
        m_screenshotState = 2;
    }
    else if (m_screenshotState == 2)
    {
        // Take the screenshot now (overlay not rendered)
        ID3D11ShaderResourceView* screenshot = ImageHelper::CaptureImage(m_pDevice, pDxBuffer);

        if (screenshot)
        {
            if (m_gptScreenshots.size() < MAX_GPT_SCREENSHOTS)
            {
                m_gptScreenshots.push_back(screenshot);
            }
            else
            {
                screenshot->Release();
            }
        }
        
        // Reset state - overlay will show again next frame
        m_screenshotState = 0;
    }

    if (pRenderTargetView)
    {
        pRenderTargetView->Release();
    }
        
    return status;
}

bool Client::IsInitialized()
{
    return m_initialized;
}

bool Client::IsInputBlockingEnabled()
{
    return m_enableInputBlocking;
}

bool Client::IsClipboardEnabled()
{
    return m_enableClipboard;
}