/*
settings.hpp
Settings management for Bypassify overlay
*/

#pragma once

#include <string>
#include <windows.h>

namespace Settings
{
    // Theme options
    enum class Theme
    {
        Dark = 0,
        Light = 1,
        System = 2
    };

    // Hotkey definition
    struct Hotkey
    {
        bool ctrl = false;
        bool shift = false;
        bool alt = false;
        int key = 0; // Virtual key code
        
        std::string ToString() const;
        bool IsPressed() const;
        bool IsJustPressed(bool& lastState) const;
    };

    // All application settings
    struct AppSettings
    {
        // API Settings
        char apiKey[256] = "";
        char prompt[1024] = "What do you see in these screenshots? Please describe them.";
        
        // Theme
        Theme theme = Theme::Dark;
        
        // Hotkeys
        Hotkey hotkeyScreenshot = { true, false, false, 'U' };      // Ctrl+U
        Hotkey hotkeySend = { true, false, false, VK_RETURN };      // Ctrl+Enter
        Hotkey hotkeyToggle = { true, false, false, 'B' };          // Ctrl+B
        Hotkey hotkeySettings = { true, true, false, 'S' };         // Ctrl+Shift+S
        Hotkey hotkeyQuit = { true, false, false, 'Q' };            // Ctrl+Q
        
        // Movement hotkeys
        Hotkey hotkeyMoveUp = { true, false, false, VK_UP };        // Ctrl+Up
        Hotkey hotkeyMoveDown = { true, false, false, VK_DOWN };    // Ctrl+Down
        Hotkey hotkeyMoveLeft = { true, false, false, VK_LEFT };    // Ctrl+Left
        Hotkey hotkeyMoveRight = { true, false, false, VK_RIGHT };  // Ctrl+Right
        
        // Scroll hotkeys
        Hotkey hotkeyScrollUp = { true, false, false, VK_OEM_4 };   // Ctrl+[ (VK_OEM_4 is '[')
        Hotkey hotkeyScrollDown = { true, false, false, VK_OEM_6 }; // Ctrl+] (VK_OEM_6 is ']')
        
        // Appearance
        float transparency = 0.0f;  // 0 to 0.85 (0% to 85% transparent)
        float windowWidth = 635.0f;
        float windowHeight = 360.0f;
        float windowPosX = 100.0f;
        float windowPosY = 100.0f;
    };

    // Initialize settings system
    void Initialize();
    
    // Get current settings (read-only)
    const AppSettings& Get();
    
    // Get mutable settings for editing
    AppSettings& GetMutable();
    
    // Save settings to file
    bool Save();
    
    // Load settings from file
    bool Load();
    
    // Get settings file path
    std::string GetSettingsPath();
    
    // Apply theme to ImGui
    void ApplyTheme(Theme theme);
    
    // Check if system is in dark mode
    bool IsSystemDarkMode();
}