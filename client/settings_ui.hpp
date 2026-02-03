/*
settings_ui.hpp
Settings UI window for Bypassify overlay
*/

#pragma once

namespace SettingsUI
{
    // Initialize settings UI
    void Initialize();
    
    // Draw settings window (call every frame)
    void Draw();
    
    // Check if settings window is open
    bool IsOpen();
    
    // Open/close settings window
    void SetOpen(bool open);
    
    // Toggle settings window
    void Toggle();
    
    // Check if currently capturing a hotkey
    bool IsCapturingHotkey();
}