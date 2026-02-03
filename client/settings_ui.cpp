/*
settings_ui.cpp
Settings UI window implementation
*/

#include "settings_ui.hpp"
#include "settings.hpp"
#include "imgui.h"
#include <windows.h>

static bool g_isOpen = false;
static bool g_waitingForHotkey = false;
static int g_editingHotkey = -1; // 0=screenshot, 1=send, 2=toggle, 3=settings, 4=quit
static int g_captureFrameDelay = 0; // Wait frames before capturing to avoid triggering action

// Helper to get key name
static const char* GetKeyName(int vk)
{
    static char buf[32];
    
    switch (vk)
    {
        case VK_RETURN: return "Enter";
        case VK_SPACE: return "Space";
        case VK_TAB: return "Tab";
        case VK_ESCAPE: return "Esc";
        case VK_BACK: return "Backspace";
        case VK_DELETE: return "Delete";
        case VK_INSERT: return "Insert";
        case VK_HOME: return "Home";
        case VK_END: return "End";
        case VK_PRIOR: return "PageUp";
        case VK_NEXT: return "PageDown";
        case VK_UP: return "Up";
        case VK_DOWN: return "Down";
        case VK_LEFT: return "Left";
        case VK_RIGHT: return "Right";
        case VK_F1: return "F1";
        case VK_F2: return "F2";
        case VK_F3: return "F3";
        case VK_F4: return "F4";
        case VK_F5: return "F5";
        case VK_F6: return "F6";
        case VK_F7: return "F7";
        case VK_F8: return "F8";
        case VK_F9: return "F9";
        case VK_F10: return "F10";
        case VK_F11: return "F11";
        case VK_F12: return "F12";
        default:
            if (vk >= 'A' && vk <= 'Z')
            {
                buf[0] = (char)vk;
                buf[1] = 0;
                return buf;
            }
            else if (vk >= '0' && vk <= '9')
            {
                buf[0] = (char)vk;
                buf[1] = 0;
                return buf;
            }
            return "?";
    }
}

// Detect pressed key for hotkey capture
static int DetectPressedKey()
{
    // Check A-Z
    for (int k = 'A'; k <= 'Z'; k++)
    {
        if (GetAsyncKeyState(k) & 0x8000) return k;
    }
    
    // Check 0-9
    for (int k = '0'; k <= '9'; k++)
    {
        if (GetAsyncKeyState(k) & 0x8000) return k;
    }
    
    // Check F1-F12
    for (int k = VK_F1; k <= VK_F12; k++)
    {
        if (GetAsyncKeyState(k) & 0x8000) return k;
    }
    
    // Check special keys
    if (GetAsyncKeyState(VK_RETURN) & 0x8000) return VK_RETURN;
    if (GetAsyncKeyState(VK_SPACE) & 0x8000) return VK_SPACE;
    if (GetAsyncKeyState(VK_TAB) & 0x8000) return VK_TAB;
    if (GetAsyncKeyState(VK_BACK) & 0x8000) return VK_BACK;
    if (GetAsyncKeyState(VK_DELETE) & 0x8000) return VK_DELETE;
    if (GetAsyncKeyState(VK_INSERT) & 0x8000) return VK_INSERT;
    if (GetAsyncKeyState(VK_HOME) & 0x8000) return VK_HOME;
    if (GetAsyncKeyState(VK_END) & 0x8000) return VK_END;
    if (GetAsyncKeyState(VK_PRIOR) & 0x8000) return VK_PRIOR;
    if (GetAsyncKeyState(VK_NEXT) & 0x8000) return VK_NEXT;
    if (GetAsyncKeyState(VK_UP) & 0x8000) return VK_UP;
    if (GetAsyncKeyState(VK_DOWN) & 0x8000) return VK_DOWN;
    if (GetAsyncKeyState(VK_LEFT) & 0x8000) return VK_LEFT;
    if (GetAsyncKeyState(VK_RIGHT) & 0x8000) return VK_RIGHT;
    
    // Check OEM keys
    if (GetAsyncKeyState(VK_OEM_1) & 0x8000) return VK_OEM_1;     // ;
    if (GetAsyncKeyState(VK_OEM_2) & 0x8000) return VK_OEM_2;     // /
    if (GetAsyncKeyState(VK_OEM_3) & 0x8000) return VK_OEM_3;     // `
    if (GetAsyncKeyState(VK_OEM_4) & 0x8000) return VK_OEM_4;     // [
    if (GetAsyncKeyState(VK_OEM_5) & 0x8000) return VK_OEM_5;     // backslash
    if (GetAsyncKeyState(VK_OEM_6) & 0x8000) return VK_OEM_6;     // ]
    if (GetAsyncKeyState(VK_OEM_7) & 0x8000) return VK_OEM_7;     // '
    if (GetAsyncKeyState(VK_OEM_PLUS) & 0x8000) return VK_OEM_PLUS;   // =
    if (GetAsyncKeyState(VK_OEM_MINUS) & 0x8000) return VK_OEM_MINUS; // -
    if (GetAsyncKeyState(VK_OEM_COMMA) & 0x8000) return VK_OEM_COMMA; // ,
    if (GetAsyncKeyState(VK_OEM_PERIOD) & 0x8000) return VK_OEM_PERIOD; // .
    
    return 0;
}

// Check if a hotkey combo is already used by another action
static bool IsHotkeyDuplicate(bool ctrl, bool shift, bool alt, int key, int currentIndex)
{
    Settings::AppSettings& settings = Settings::GetMutable();
    
    // Array of all hotkeys to check against (excluding quit which is fixed)
    Settings::Hotkey* hotkeys[] = {
        &settings.hotkeyScreenshot,
        &settings.hotkeySend,
        &settings.hotkeyToggle,
        &settings.hotkeySettings,
        &settings.hotkeyMoveUp,
        &settings.hotkeyMoveDown,
        &settings.hotkeyMoveLeft,
        &settings.hotkeyMoveRight,
        &settings.hotkeyScrollUp,
        &settings.hotkeyScrollDown
    };
    
    for (int i = 0; i < 10; i++)
    {
        if (i == currentIndex) continue; // Skip the one we're editing
        
        if (hotkeys[i]->ctrl == ctrl &&
            hotkeys[i]->shift == shift &&
            hotkeys[i]->alt == alt &&
            hotkeys[i]->key == key)
        {
            return true; // Duplicate found
        }
    }
    
    // Also check against fixed quit hotkey (Ctrl+Q)
    if (ctrl && !shift && !alt && key == 'Q')
    {
        return true;
    }
    
    return false;
}

static void DrawHotkeyButton(const char* label, Settings::Hotkey& hotkey, int index)
{
    ImGui::Text("%s:", label);
    ImGui::SameLine(150);
    
    if (g_waitingForHotkey && g_editingHotkey == index)
    {
        ImGui::Button("Press key...", ImVec2(150, 0));
        
        // Wait a few frames before accepting input to prevent immediate trigger
        if (g_captureFrameDelay > 0)
        {
            g_captureFrameDelay--;
            return;
        }
        
        // Check for modifier keys
        bool ctrl = (GetAsyncKeyState(VK_LCONTROL) & 0x8000) || (GetAsyncKeyState(VK_RCONTROL) & 0x8000);
        bool shift = (GetAsyncKeyState(VK_LSHIFT) & 0x8000) || (GetAsyncKeyState(VK_RSHIFT) & 0x8000);
        bool alt = (GetAsyncKeyState(VK_LMENU) & 0x8000) || (GetAsyncKeyState(VK_RMENU) & 0x8000);
        
        // Detect main key
        int key = DetectPressedKey();
        
        // Allow single keys OR modifier combos (but not just modifiers alone)
        if (key != 0)
        {
            // Check for duplicate before accepting
            if (!IsHotkeyDuplicate(ctrl, shift, alt, key, index))
            {
                hotkey.ctrl = ctrl;
                hotkey.shift = shift;
                hotkey.alt = alt;
                hotkey.key = key;
                g_waitingForHotkey = false;
                g_editingHotkey = -1;
            }
            // If duplicate, just ignore and keep waiting for a different key
        }
        
        // Cancel on Escape
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)
        {
            g_waitingForHotkey = false;
            g_editingHotkey = -1;
        }
    }
    else
    {
        std::string hotkeyStr = hotkey.ToString();
        if (ImGui::Button(hotkeyStr.c_str(), ImVec2(150, 0)))
        {
            g_waitingForHotkey = true;
            g_editingHotkey = index;
            g_captureFrameDelay = 10; // Wait 10 frames before accepting input
        }
    }
}

void SettingsUI::Initialize()
{
    g_isOpen = false;
    g_waitingForHotkey = false;
    g_editingHotkey = -1;
}

void SettingsUI::Draw()
{
    if (!g_isOpen) return;
    
    Settings::AppSettings& settings = Settings::GetMutable();
    
    ImGui::SetNextWindowSize(ImVec2(450, 500), ImGuiCond_FirstUseEver);
    
    // NoCollapse, NoScrollbar - passing nullptr removes X button
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar;
    
    if (ImGui::Begin("Settings", nullptr, flags))
    {
        // ============================================
        // API Settings
        // ============================================
        if (ImGui::CollapsingHeader("API Settings", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Text("OpenAI API Key:");
            ImGui::PushItemWidth(-1);
            ImGui::InputText("##SettingsApiKey", settings.apiKey, sizeof(settings.apiKey), ImGuiInputTextFlags_Password);
            ImGui::PopItemWidth();
            
            ImGui::Spacing();
            
            ImGui::Text("Default Prompt:");
            ImGui::PushItemWidth(-1);
            ImGui::InputTextMultiline("##SettingsPrompt", settings.prompt, sizeof(settings.prompt), ImVec2(-1, 80));
            ImGui::PopItemWidth();
        }
        
        ImGui::Spacing();
        
        // ============================================
        // Theme Settings
        // ============================================
        if (ImGui::CollapsingHeader("Theme", ImGuiTreeNodeFlags_DefaultOpen))
        {
            int themeInt = static_cast<int>(settings.theme);
            
            ImGui::RadioButton("Dark", &themeInt, 0);
            ImGui::SameLine();
            ImGui::RadioButton("Light", &themeInt, 1);
            ImGui::SameLine();
            ImGui::RadioButton("System Default", &themeInt, 2);
            
            if (themeInt != static_cast<int>(settings.theme))
            {
                settings.theme = static_cast<Settings::Theme>(themeInt);
                Settings::ApplyTheme(settings.theme);
            }
        }
        
        ImGui::Spacing();
        
        // ============================================
        // Hotkey Settings
        // ============================================
        if (ImGui::CollapsingHeader("Hotkeys", ImGuiTreeNodeFlags_DefaultOpen))
        {
            // Calculate alpha from transparency
            float alpha = 1.0f - settings.transparency;
            if (alpha < 0.15f) alpha = 0.15f;
            
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, alpha), "Click a hotkey to change it. Press Escape to cancel.");
            ImGui::Spacing();
            
            DrawHotkeyButton("Screenshot", settings.hotkeyScreenshot, 0);
            DrawHotkeyButton("Send to GPT", settings.hotkeySend, 1);
            DrawHotkeyButton("Toggle Overlay", settings.hotkeyToggle, 2);
            DrawHotkeyButton("Open Settings", settings.hotkeySettings, 3);
            DrawHotkeyButton("Move Up", settings.hotkeyMoveUp, 4);
            DrawHotkeyButton("Move Down", settings.hotkeyMoveDown, 5);
            DrawHotkeyButton("Move Left", settings.hotkeyMoveLeft, 6);
            DrawHotkeyButton("Move Right", settings.hotkeyMoveRight, 7);
            DrawHotkeyButton("Scroll Up", settings.hotkeyScrollUp, 8);
            DrawHotkeyButton("Scroll Down", settings.hotkeyScrollDown, 9);
        }
        
        ImGui::Spacing();
        
        // ============================================
        // Appearance Settings
        // ============================================
        if (ImGui::CollapsingHeader("Appearance", ImGuiTreeNodeFlags_DefaultOpen))
        {
            // Transparency slider (0% to 85%)
            float transparencyPercent = settings.transparency * 100.0f;
            ImGui::Text("Transparency:");
            ImGui::SameLine();
            ImGui::PushItemWidth(-1);
            if (ImGui::SliderFloat("##Transparency", &transparencyPercent, 0.0f, 85.0f, "%.0f%%"))
            {
                settings.transparency = transparencyPercent / 100.0f;
                Settings::ApplyTheme(settings.theme); // Reapply theme with new transparency
            }
            ImGui::PopItemWidth();
            
            ImGui::Spacing();
            
            // Window size
            ImGui::Text("Window Size:");
            
            ImGui::Text("Width:");
            ImGui::SameLine(60);
            ImGui::PushItemWidth(150);
            ImGui::SliderFloat("##Width", &settings.windowWidth, 300.0f, 800.0f, "%.0f");
            ImGui::PopItemWidth();
            
            ImGui::Text("Height:");
            ImGui::SameLine(60);
            ImGui::PushItemWidth(150);
            ImGui::SliderFloat("##Height", &settings.windowHeight, 300.0f, 700.0f, "%.0f");
            ImGui::PopItemWidth();
        }
        
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        
        // Save and close button
        if (ImGui::Button("Save Settings and Close", ImVec2(180, 0)))
        {
            // Cancel any pending hotkey capture first
            g_waitingForHotkey = false;
            g_editingHotkey = -1;
            
            Settings::Save();
            g_isOpen = false;
        }
    }
    ImGui::End();
}

bool SettingsUI::IsOpen()
{
    return g_isOpen;
}

void SettingsUI::SetOpen(bool open)
{
    g_isOpen = open;
    if (!open)
    {
        g_waitingForHotkey = false;
        g_editingHotkey = -1;
    }
}

void SettingsUI::Toggle()
{
    // Only open, don't close (use the button to close)
    if (!g_isOpen)
    {
        SetOpen(true);
    }
}

bool SettingsUI::IsCapturingHotkey()
{
    return g_waitingForHotkey;
}