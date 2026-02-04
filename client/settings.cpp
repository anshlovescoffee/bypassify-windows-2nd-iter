/*
settings.cpp
Settings management implementation
*/

#include "settings.hpp"
#include "client.hpp"
#include "imgui.h"
#include <fstream>
#include <shlobj.h>
#include <direct.h>

#pragma comment(lib, "shell32.lib")

using namespace Settings;

static AppSettings g_settings;
static bool g_initialized = false;

// ============================================
// Hotkey Implementation
// ============================================

std::string Hotkey::ToString() const
{
    std::string result;
    
    if (ctrl) result += "Ctrl+";
    if (shift) result += "Shift+";
    if (alt) result += "Alt+";
    
    // Convert virtual key to string
    switch (key)
    {
        case VK_RETURN: result += "Enter"; break;
        case VK_SPACE: result += "Space"; break;
        case VK_TAB: result += "Tab"; break;
        case VK_ESCAPE: result += "Esc"; break;
        case VK_BACK: result += "Backspace"; break;
        case VK_DELETE: result += "Delete"; break;
        case VK_INSERT: result += "Insert"; break;
        case VK_HOME: result += "Home"; break;
        case VK_END: result += "End"; break;
        case VK_PRIOR: result += "PageUp"; break;
        case VK_NEXT: result += "PageDown"; break;
        case VK_UP: result += "Up"; break;
        case VK_DOWN: result += "Down"; break;
        case VK_LEFT: result += "Left"; break;
        case VK_RIGHT: result += "Right"; break;
        case VK_F1: result += "F1"; break;
        case VK_F2: result += "F2"; break;
        case VK_F3: result += "F3"; break;
        case VK_F4: result += "F4"; break;
        case VK_F5: result += "F5"; break;
        case VK_F6: result += "F6"; break;
        case VK_F7: result += "F7"; break;
        case VK_F8: result += "F8"; break;
        case VK_F9: result += "F9"; break;
        case VK_F10: result += "F10"; break;
        case VK_F11: result += "F11"; break;
        case VK_F12: result += "F12"; break;
        case VK_OEM_1: result += ";"; break;
        case VK_OEM_2: result += "/"; break;
        case VK_OEM_3: result += "`"; break;
        case VK_OEM_4: result += "["; break;
        case VK_OEM_5: result += "\\"; break;
        case VK_OEM_6: result += "]"; break;
        case VK_OEM_7: result += "'"; break;
        case VK_OEM_PLUS: result += "="; break;
        case VK_OEM_MINUS: result += "-"; break;
        case VK_OEM_COMMA: result += ","; break;
        case VK_OEM_PERIOD: result += "."; break;
        default:
            if (key >= 'A' && key <= 'Z')
            {
                result += (char)key;
            }
            else if (key >= '0' && key <= '9')
            {
                result += (char)key;
            }
            else
            {
                result += "?";
            }
            break;
    }
    
    return result;
}

bool Hotkey::IsPressed() const
{
    bool ctrlPressed = (Client::GetKeyStateFromHook(VK_LCONTROL) & 0x80) || (Client::GetKeyStateFromHook(VK_RCONTROL) & 0x80);
    bool shiftPressed = (Client::GetKeyStateFromHook(VK_LSHIFT) & 0x80) || (Client::GetKeyStateFromHook(VK_RSHIFT) & 0x80);
    bool altPressed = (Client::GetKeyStateFromHook(VK_LMENU) & 0x80) || (Client::GetKeyStateFromHook(VK_RMENU) & 0x80);
    bool keyPressed = Client::GetKeyStateFromHook(key) & 0x80;
    
    // Check modifiers match exactly
    bool ctrlMatch = (ctrl == ctrlPressed);
    bool shiftMatch = (shift == shiftPressed);
    bool altMatch = (alt == altPressed);
    
    return ctrlMatch && shiftMatch && altMatch && keyPressed;
}

bool Hotkey::IsJustPressed(bool& lastState) const
{
    bool currentlyPressed = IsPressed();
    bool justPressed = currentlyPressed && !lastState;
    lastState = currentlyPressed;
    return justPressed;
}

// ============================================
// Settings File I/O
// ============================================

std::string Settings::GetSettingsPath()
{
    char path[MAX_PATH] = {0};
    
    // Try to get AppData using environment variable (works better in DWM context)
    DWORD len = GetEnvironmentVariableA("APPDATA", path, MAX_PATH);
    
    if (len > 0 && len < MAX_PATH)
    {
        std::string settingsDir = std::string(path) + "\\Redacted";
        _mkdir(settingsDir.c_str());
        return settingsDir + "\\settings.dat";
    }
    
    // Try SHGetFolderPath as fallback
    if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, path)))
    {
        std::string settingsDir = std::string(path) + "\\Redacted";
        _mkdir(settingsDir.c_str());
        return settingsDir + "\\settings.dat";
    }
    
    // Try local appdata
    len = GetEnvironmentVariableA("LOCALAPPDATA", path, MAX_PATH);
    if (len > 0 && len < MAX_PATH)
    {
        std::string settingsDir = std::string(path) + "\\Redacted";
        _mkdir(settingsDir.c_str());
        return settingsDir + "\\settings.dat";
    }
    
    // Try ProgramData (accessible by SYSTEM)
    len = GetEnvironmentVariableA("ProgramData", path, MAX_PATH);
    if (len > 0 && len < MAX_PATH)
    {
        std::string settingsDir = std::string(path) + "\\Redacted";
        _mkdir(settingsDir.c_str());
        return settingsDir + "\\settings.dat";
    }
    
    // Last fallback - current directory
    return "C:\\Redacted_settings.dat";
}

// Simple binary serialization
bool Settings::Save()
{
    std::string path = GetSettingsPath();
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    
    if (!file.is_open())
    {
        return false;
    }
    
    // Write magic number and version
    const char magic[] = "BPFY";
    int version = 5;  // Version 5: added model cycle hotkey + selected model ID
    
    file.write(magic, 4);
    file.write(reinterpret_cast<const char*>(&version), sizeof(version));
    
    // Write settings
    file.write(g_settings.apiKey, sizeof(g_settings.apiKey));
    file.write(g_settings.prompt, sizeof(g_settings.prompt));
    
    int themeInt = static_cast<int>(g_settings.theme);
    file.write(reinterpret_cast<const char*>(&themeInt), sizeof(themeInt));
    
    // Write hotkeys
    file.write(reinterpret_cast<const char*>(&g_settings.hotkeyScreenshot), sizeof(Hotkey));
    file.write(reinterpret_cast<const char*>(&g_settings.hotkeySend), sizeof(Hotkey));
    file.write(reinterpret_cast<const char*>(&g_settings.hotkeyToggle), sizeof(Hotkey));
    file.write(reinterpret_cast<const char*>(&g_settings.hotkeySettings), sizeof(Hotkey));
    file.write(reinterpret_cast<const char*>(&g_settings.hotkeyQuit), sizeof(Hotkey));
    
    // Write movement hotkeys (version 3)
    file.write(reinterpret_cast<const char*>(&g_settings.hotkeyMoveUp), sizeof(Hotkey));
    file.write(reinterpret_cast<const char*>(&g_settings.hotkeyMoveDown), sizeof(Hotkey));
    file.write(reinterpret_cast<const char*>(&g_settings.hotkeyMoveLeft), sizeof(Hotkey));
    file.write(reinterpret_cast<const char*>(&g_settings.hotkeyMoveRight), sizeof(Hotkey));
    
    // Write scroll hotkeys (version 4)
    file.write(reinterpret_cast<const char*>(&g_settings.hotkeyScrollUp), sizeof(Hotkey));
    file.write(reinterpret_cast<const char*>(&g_settings.hotkeyScrollDown), sizeof(Hotkey));
    
    // Write model cycle hotkey + selected model ID (version 5)
    file.write(reinterpret_cast<const char*>(&g_settings.hotkeyModelCycle), sizeof(Hotkey));
    file.write(g_settings.selectedModelId, sizeof(g_settings.selectedModelId));
    
    // Write appearance
    file.write(reinterpret_cast<const char*>(&g_settings.transparency), sizeof(float));
    file.write(reinterpret_cast<const char*>(&g_settings.windowWidth), sizeof(float));
    file.write(reinterpret_cast<const char*>(&g_settings.windowHeight), sizeof(float));
    file.write(reinterpret_cast<const char*>(&g_settings.windowPosX), sizeof(float));
    file.write(reinterpret_cast<const char*>(&g_settings.windowPosY), sizeof(float));
    
    file.flush();
    file.close();
    return file.good();
}

bool Settings::Load()
{
    std::string path = GetSettingsPath();
    std::ifstream file(path, std::ios::binary);
    
    if (!file.is_open())
    {
        return false;
    }
    
    // Read and verify magic number
    char magic[4];
    file.read(magic, 4);
    
    if (magic[0] != 'B' || magic[1] != 'P' || magic[2] != 'F' || magic[3] != 'Y')
    {
        file.close();
        return false;
    }
    
    // Read version
    int version;
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    
    if (version < 1 || version > 5)
    {
        file.close();
        return false;
    }
    
    // Read settings
    file.read(g_settings.apiKey, sizeof(g_settings.apiKey));
    file.read(g_settings.prompt, sizeof(g_settings.prompt));
    
    int themeInt;
    file.read(reinterpret_cast<char*>(&themeInt), sizeof(themeInt));
    g_settings.theme = static_cast<Theme>(themeInt);
    
    // Read hotkeys
    file.read(reinterpret_cast<char*>(&g_settings.hotkeyScreenshot), sizeof(Hotkey));
    file.read(reinterpret_cast<char*>(&g_settings.hotkeySend), sizeof(Hotkey));
    file.read(reinterpret_cast<char*>(&g_settings.hotkeyToggle), sizeof(Hotkey));
    file.read(reinterpret_cast<char*>(&g_settings.hotkeySettings), sizeof(Hotkey));
    
    // Version 2 added quit hotkey
    if (version >= 2)
    {
        file.read(reinterpret_cast<char*>(&g_settings.hotkeyQuit), sizeof(Hotkey));
    }
    
    // Version 3 added movement hotkeys and position
    if (version >= 3)
    {
        file.read(reinterpret_cast<char*>(&g_settings.hotkeyMoveUp), sizeof(Hotkey));
        file.read(reinterpret_cast<char*>(&g_settings.hotkeyMoveDown), sizeof(Hotkey));
        file.read(reinterpret_cast<char*>(&g_settings.hotkeyMoveLeft), sizeof(Hotkey));
        file.read(reinterpret_cast<char*>(&g_settings.hotkeyMoveRight), sizeof(Hotkey));
    }
    
    // Version 4 added scroll hotkeys
    if (version >= 4)
    {
        file.read(reinterpret_cast<char*>(&g_settings.hotkeyScrollUp), sizeof(Hotkey));
        file.read(reinterpret_cast<char*>(&g_settings.hotkeyScrollDown), sizeof(Hotkey));
    }
    
    // Version 5 added model cycle hotkey + selected model ID
    if (version >= 5)
    {
        file.read(reinterpret_cast<char*>(&g_settings.hotkeyModelCycle), sizeof(Hotkey));
        file.read(g_settings.selectedModelId, sizeof(g_settings.selectedModelId));
    }
    
    // Read appearance
    file.read(reinterpret_cast<char*>(&g_settings.transparency), sizeof(float));
    file.read(reinterpret_cast<char*>(&g_settings.windowWidth), sizeof(float));
    file.read(reinterpret_cast<char*>(&g_settings.windowHeight), sizeof(float));
    
    // Version 3 added position
    if (version >= 3)
    {
        file.read(reinterpret_cast<char*>(&g_settings.windowPosX), sizeof(float));
        file.read(reinterpret_cast<char*>(&g_settings.windowPosY), sizeof(float));
    }
    
    file.close();
    return true;
}

// ============================================
// Settings Access
// ============================================

void Settings::Initialize()
{
    if (g_initialized) return;
    
    // Set defaults
    g_settings = AppSettings();
    
    // Try to load saved settings
    Load();
    
    g_initialized = true;
}

const AppSettings& Settings::Get()
{
    return g_settings;
}

AppSettings& Settings::GetMutable()
{
    return g_settings;
}

// ============================================
// Theme Management
// ============================================

bool Settings::IsSystemDarkMode()
{
    HKEY hKey;
    DWORD value = 1;
    DWORD size = sizeof(value);
    
    if (RegOpenKeyExA(HKEY_CURRENT_USER, 
        "Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        RegQueryValueExA(hKey, "AppsUseLightTheme", NULL, NULL, 
            reinterpret_cast<LPBYTE>(&value), &size);
        RegCloseKey(hKey);
    }
    
    return value == 0; // 0 = dark mode, 1 = light mode
}

void Settings::ApplyTheme(Theme theme)
{
    // Determine actual theme
    bool isDark = true;
    if (theme == Theme::Light)
    {
        isDark = false;
    }
    else if (theme == Theme::System)
    {
        isDark = IsSystemDarkMode();
    }
    
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;
    
    // transparency is 0-0.85 (0% to 85% transparent), so alpha = 1 - transparency
    float alpha = 1.0f - g_settings.transparency;
    if (alpha < 0.15f) alpha = 0.15f; // Minimum 15% opacity so it's still visible
    
    if (isDark)
    {
        // Dark theme with subtle purple accents + transparency
        colors[ImGuiCol_WindowBg]             = ImVec4(0.06f, 0.06f, 0.06f, alpha);
        colors[ImGuiCol_ChildBg]              = ImVec4(0.08f, 0.08f, 0.08f, alpha * 0.67f);
        colors[ImGuiCol_PopupBg]              = ImVec4(0.06f, 0.06f, 0.06f, alpha);
        
        colors[ImGuiCol_Border]               = ImVec4(0.35f, 0.25f, 0.45f, alpha * 0.5f);
        colors[ImGuiCol_BorderShadow]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        
        colors[ImGuiCol_FrameBg]              = ImVec4(0.12f, 0.11f, 0.14f, alpha);
        colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.18f, 0.15f, 0.22f, alpha);
        colors[ImGuiCol_FrameBgActive]        = ImVec4(0.22f, 0.18f, 0.28f, alpha);
        
        colors[ImGuiCol_TitleBg]              = ImVec4(0.08f, 0.07f, 0.10f, alpha);
        colors[ImGuiCol_TitleBgActive]        = ImVec4(0.12f, 0.10f, 0.16f, alpha);
        colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.08f, 0.07f, 0.10f, alpha * 0.75f);
        
        colors[ImGuiCol_Button]               = ImVec4(0.15f, 0.14f, 0.18f, alpha);
        colors[ImGuiCol_ButtonHovered]        = ImVec4(0.28f, 0.22f, 0.38f, alpha);
        colors[ImGuiCol_ButtonActive]         = ImVec4(0.38f, 0.28f, 0.52f, alpha);
        
        colors[ImGuiCol_Header]               = ImVec4(0.18f, 0.16f, 0.22f, alpha);
        colors[ImGuiCol_HeaderHovered]        = ImVec4(0.28f, 0.22f, 0.38f, alpha);
        colors[ImGuiCol_HeaderActive]         = ImVec4(0.38f, 0.28f, 0.52f, alpha);
        
        colors[ImGuiCol_Separator]            = ImVec4(0.30f, 0.25f, 0.40f, alpha * 0.5f);
        colors[ImGuiCol_SeparatorHovered]     = ImVec4(0.45f, 0.35f, 0.60f, alpha);
        colors[ImGuiCol_SeparatorActive]      = ImVec4(0.55f, 0.40f, 0.75f, alpha);
        
        colors[ImGuiCol_SliderGrab]           = ImVec4(0.40f, 0.30f, 0.55f, alpha);
        colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.50f, 0.38f, 0.68f, alpha);
        
        colors[ImGuiCol_CheckMark]            = ImVec4(0.55f, 0.40f, 0.75f, alpha);
        
        colors[ImGuiCol_Text]                 = ImVec4(0.92f, 0.92f, 0.92f, alpha);
        colors[ImGuiCol_TextDisabled]         = ImVec4(0.45f, 0.45f, 0.45f, alpha);
        colors[ImGuiCol_TextSelectedBg]       = ImVec4(0.35f, 0.25f, 0.50f, alpha * 0.5f);
    }
    else
    {
        // Light theme with subtle purple accents
        colors[ImGuiCol_WindowBg]             = ImVec4(0.95f, 0.95f, 0.96f, alpha);
        colors[ImGuiCol_ChildBg]              = ImVec4(0.92f, 0.92f, 0.94f, alpha * 0.67f);
        colors[ImGuiCol_PopupBg]              = ImVec4(0.98f, 0.98f, 0.98f, alpha);
        
        colors[ImGuiCol_Border]               = ImVec4(0.60f, 0.50f, 0.70f, alpha * 0.5f);
        colors[ImGuiCol_BorderShadow]         = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        
        colors[ImGuiCol_FrameBg]              = ImVec4(0.88f, 0.86f, 0.90f, alpha);
        colors[ImGuiCol_FrameBgHovered]       = ImVec4(0.82f, 0.78f, 0.88f, alpha);
        colors[ImGuiCol_FrameBgActive]        = ImVec4(0.76f, 0.70f, 0.84f, alpha);
        
        colors[ImGuiCol_TitleBg]              = ImVec4(0.85f, 0.82f, 0.90f, alpha);
        colors[ImGuiCol_TitleBgActive]        = ImVec4(0.78f, 0.72f, 0.88f, alpha);
        colors[ImGuiCol_TitleBgCollapsed]     = ImVec4(0.90f, 0.88f, 0.92f, alpha * 0.75f);
        
        colors[ImGuiCol_Button]               = ImVec4(0.82f, 0.78f, 0.88f, alpha);
        colors[ImGuiCol_ButtonHovered]        = ImVec4(0.70f, 0.62f, 0.82f, alpha);
        colors[ImGuiCol_ButtonActive]         = ImVec4(0.58f, 0.48f, 0.75f, alpha);
        
        colors[ImGuiCol_Header]               = ImVec4(0.82f, 0.78f, 0.88f, alpha);
        colors[ImGuiCol_HeaderHovered]        = ImVec4(0.70f, 0.62f, 0.82f, alpha);
        colors[ImGuiCol_HeaderActive]         = ImVec4(0.58f, 0.48f, 0.75f, alpha);
        
        colors[ImGuiCol_Separator]            = ImVec4(0.60f, 0.50f, 0.70f, alpha * 0.5f);
        colors[ImGuiCol_SeparatorHovered]     = ImVec4(0.50f, 0.40f, 0.65f, alpha);
        colors[ImGuiCol_SeparatorActive]      = ImVec4(0.45f, 0.35f, 0.60f, alpha);
        
        colors[ImGuiCol_SliderGrab]           = ImVec4(0.55f, 0.45f, 0.70f, alpha);
        colors[ImGuiCol_SliderGrabActive]     = ImVec4(0.45f, 0.35f, 0.62f, alpha);
        
        colors[ImGuiCol_CheckMark]            = ImVec4(0.45f, 0.35f, 0.60f, alpha);
        
        colors[ImGuiCol_Text]                 = ImVec4(0.10f, 0.10f, 0.10f, alpha);
        colors[ImGuiCol_TextDisabled]         = ImVec4(0.50f, 0.50f, 0.50f, alpha);
        colors[ImGuiCol_TextSelectedBg]       = ImVec4(0.60f, 0.50f, 0.75f, alpha * 0.5f);
    }
    
    // Common settings for both themes
    colors[ImGuiCol_ResizeGrip]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_ResizeGripHovered]    = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_ResizeGripActive]     = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    
    if (isDark)
    {
        colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.10f, 0.10f, 0.12f, alpha * 0.5f);
        colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.35f, 0.30f, 0.45f, alpha);
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.45f, 0.38f, 0.58f, alpha);
        colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.55f, 0.45f, 0.70f, alpha);
    }
    else
    {
        colors[ImGuiCol_ScrollbarBg]          = ImVec4(0.88f, 0.88f, 0.90f, alpha * 0.5f);
        colors[ImGuiCol_ScrollbarGrab]        = ImVec4(0.60f, 0.52f, 0.70f, alpha);
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.50f, 0.42f, 0.62f, alpha);
        colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.45f, 0.35f, 0.58f, alpha);
    }
}

bool Settings::IsCurrentThemeLight()
{
    if (g_settings.theme == Theme::Light) return true;
    if (g_settings.theme == Theme::System) return !IsSystemDarkMode();
    return false;
}