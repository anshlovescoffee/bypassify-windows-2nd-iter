/*
input.cpp
Implements all mouse, keyboard, and text inputs for the imgui client
*/

#include <stdio.h>
#include <windows.h>
#include "client.hpp"
#include "settings.hpp"
#include "imgui_internal.h"
#include "imgui_impl_win32.h"

using namespace Client;

HHOOK m_hKeyboardHook = NULL;
HHOOK m_hMouseHook = NULL;
bool m_inputInitialized = false;
BYTE m_keyboardState[256];
HKL m_keyboardLayout = GetKeyboardLayout(0);
const char* m_pSelectionSource = nullptr;
int m_selectionLeft = 0;
int m_selectionRight = 0;
bool m_lastInputBlocked = false;

bool IsKeyToggleKey(int vk)
{
    return vk == VK_CAPITAL || vk == VK_NUMLOCK || vk == VK_SCROLL || vk == VK_KANA || vk == VK_HANGUL;
}

bool IsKeyBlockable(int vk)
{
    if (vk >= '0' && vk <= 'Z')
    {
        return true;
    }
    if (vk >= VK_NUMPAD0 && vk <= VK_F24)
    {
        return true;
    }
    if (vk == VK_OEM_1 || vk == VK_OEM_2 || vk == VK_OEM_3)
    {
        return true;
    }
    if (vk >= VK_OEM_4 && vk <= VK_OEM_102)
    {
        return true;
    }
    if (vk == VK_SPACE || vk == VK_TAB || vk == VK_BACK || vk == VK_RETURN)
    {
        return true;
    }
    return false;
}

bool TrackKeyPressForKey(int vk)
{
    if (vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_SHIFT)
    {
        return true;
    }
    return false;
}

// Check if the current key state matches a hotkey definition
static bool MatchesHotkey(const Settings::Hotkey& hk, int vkCode)
{
    // The vkCode being pressed must match the hotkey's main key
    if (hk.key == 0) return false;
    if (vkCode != hk.key) return false;
    
    // Check modifier state
    bool ctrlDown = (m_keyboardState[VK_CONTROL] & 0x80) != 0;
    bool shiftDown = (m_keyboardState[VK_SHIFT] & 0x80) != 0;
    bool altDown = (m_keyboardState[VK_LMENU] & 0x80) || (m_keyboardState[VK_RMENU] & 0x80);
    
    return (hk.ctrl == ctrlDown) && (hk.shift == shiftDown) && (hk.alt == altDown);
}

// Check if vkCode + current modifiers matches any app hotkey
static bool IsAppHotkey(int vkCode)
{
    const Settings::AppSettings& s = Settings::Get();
    
    if (MatchesHotkey(s.hotkeyScreenshot, vkCode)) return true;
    if (MatchesHotkey(s.hotkeySend, vkCode)) return true;
    if (MatchesHotkey(s.hotkeyToggle, vkCode)) return true;
    if (MatchesHotkey(s.hotkeySettings, vkCode)) return true;
    if (MatchesHotkey(s.hotkeyQuit, vkCode)) return true;
    if (MatchesHotkey(s.hotkeyMoveUp, vkCode)) return true;
    if (MatchesHotkey(s.hotkeyMoveDown, vkCode)) return true;
    if (MatchesHotkey(s.hotkeyMoveLeft, vkCode)) return true;
    if (MatchesHotkey(s.hotkeyMoveRight, vkCode)) return true;
    if (MatchesHotkey(s.hotkeyScrollUp, vkCode)) return true;
    if (MatchesHotkey(s.hotkeyScrollDown, vkCode)) return true;
    
    return false;
}

LRESULT CALLBACK KeyboardHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION)
    {
        KBDLLHOOKSTRUCT* p = (KBDLLHOOKSTRUCT*)lParam;
        ImGuiIO& io = ImGui::GetIO();
        WCHAR inputBuffer[8];
        int inputSize = 0;

        // ctrl+c related variables
        char* inputText = nullptr; // input text buffer for the clipboard
        ImGuiInputTextState inputState = {};
        int selectionLength = 0;

        // need to convert hook scan code into ToUnicodeEx scan code
        UINT scanCode = p->scanCode & 0x00FF;
        if (p->scanCode & 0x01000000)
        {
            scanCode |= 0xE000;
        }

        // need to manually override VK_SHIFT as windows doesn't track VK_SHIFT in LL keyboard hooks for some reason
        if (m_keyboardState[VK_LSHIFT] || m_keyboardState[VK_RSHIFT])
        {
            m_keyboardState[VK_SHIFT] = 0x80;
        }
        else
        {
            m_keyboardState[VK_SHIFT] = 0x0;
        }

        // same idea with CTRL keys as there are two of them
        if (m_keyboardState[VK_LCONTROL] || m_keyboardState[VK_RCONTROL])
        {
            m_keyboardState[VK_CONTROL] = 0x80;
        }
        else
        {
            m_keyboardState[VK_CONTROL] = 0x0;
        }

        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)
        {
            if (IsKeyToggleKey(p->vkCode))
            {
                m_keyboardState[p->vkCode] ^= 0x1;
            }
            else
            {
                m_keyboardState[p->vkCode] = 0x80;
            }

            if (m_keyboardState[VK_CONTROL] && m_keyboardState['V'] && IsClipboardEnabled()) // handling text pasting
            {
                io.AddInputCharactersUTF8(ImGui::GetClipboardText());
            }
            else if (m_keyboardState[VK_CONTROL] && m_keyboardState['C'] && IsClipboardEnabled()) // handle text copying
            {
                if (io.WantTextInput && m_pSelectionSource)
                {
                    inputSize = m_selectionRight - m_selectionLeft;
                    inputText = static_cast<char*>(malloc(inputSize + 1));
                    if (inputText)
                    {
                        memcpy(inputText, m_pSelectionSource + m_selectionLeft, inputSize);
                        inputText[inputSize] = 0; // null terminator
                        ImGui::SetClipboardText(inputText);
                        free(inputText);
                        inputText = nullptr;
                    }
                }
            }
            else
            {
                inputSize = ToUnicodeEx(p->vkCode, scanCode, m_keyboardState, inputBuffer, 8, 0, m_keyboardLayout);
                for (int i = 0; i < inputSize; ++i)
                {
                    io.AddInputCharacterUTF16(inputBuffer[i]);
                }
            }

            io.AddKeyEvent(ImGui_ImplWin32_KeyEventToImGuiKey((WPARAM)p->vkCode, 0), true);
        }
        else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP)
        {
            if (!IsKeyToggleKey(p->vkCode))
            {
                m_keyboardState[p->vkCode] = 0;
            }
            io.AddKeyEvent(ImGui_ImplWin32_KeyEventToImGuiKey((WPARAM)p->vkCode, 0), false);
        }

        m_lastInputBlocked = IsInputBlockingEnabled() && io.WantCaptureKeyboard;
        if (m_lastInputBlocked)
        {
            return 1;
        }
        
        // Block our app hotkeys from reaching other applications
        if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)
        {
            if (IsAppHotkey(p->vkCode))
            {
                return 1;  // Swallow the keypress
            }
        }
    }
    return CallNextHookEx(m_hKeyboardHook, nCode, wParam, lParam);
}

LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    ImGuiIO& io = ImGui::GetIO();
    MSLLHOOKSTRUCT* p = (MSLLHOOKSTRUCT*)lParam;

    if (nCode == HC_ACTION)
    {
        switch (wParam)
        {
            case WM_LBUTTONDOWN:
                io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
                break;
            case WM_LBUTTONUP:
                io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
                break;
            case WM_RBUTTONDOWN:
                io.AddMouseButtonEvent(ImGuiMouseButton_Right, true);
            case WM_RBUTTONUP:
                io.AddMouseButtonEvent(ImGuiMouseButton_Right, false);
                break;
            case WM_MOUSEMOVE:
                break;
            case WM_MOUSEWHEEL:
                io.AddMouseWheelEvent(0.0f, (short)(HIWORD(p->mouseData)) / (float)WHEEL_DELTA);
                break;
        }
        if (IsInputBlockingEnabled() && io.WantCaptureMouse && wParam != WM_MOUSEMOVE) // don't want to freeze up the mouse so a mouse move check is used
        {
            return 1;
        }
    }
    return CallNextHookEx(m_hMouseHook, nCode, wParam, lParam);
}

BYTE Client::GetKeyStateFromHook(int vkCode)
{
    if (vkCode < 0 || vkCode > 255)
    {
        return 0;
    }
    return m_keyboardState[vkCode];
}

void Client::InputFunction()
{
    ImGuiContext* pContext = ImGui::GetCurrentContext();
    MSG msg;

    m_keyboardLayout = GetKeyboardLayout(0);

    if (pContext)
    { // context isn't available asynchronously so need to refresh selection stuff every frame
        m_pSelectionSource = pContext->InputTextState.TextA.Data;
        m_selectionLeft = pContext->InputTextState.GetSelectionEnd();
        m_selectionRight = pContext->InputTextState.GetSelectionStart();
    }
    PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE); // need to do this to make the low level keyboard hook get called
}

CLIENT_STATUS Client::InputInitialize()
{
    if (m_inputInitialized)
    {
        return CLIENT_STATUS::ALREADY_DONE;
    }
    m_hKeyboardHook = SetWindowsHookExA(WH_KEYBOARD_LL, KeyboardHookProc, GetModuleHandleA(NULL), 0);
    if (m_hKeyboardHook == NULL)
    {
        fprintf(stderr, "Failed to hook keyboard award\n");
        return CLIENT_STATUS::FAILURE;
    }
    m_hMouseHook = SetWindowsHookExA(WH_MOUSE_LL, MouseHookProc, GetModuleHandleA(NULL), 0);
    if (m_hMouseHook = NULL)
    {
        fprintf(stderr, "Failed to hook mouse award\n");
        return CLIENT_STATUS::FAILURE;
    }
    m_inputInitialized = true;
    GetKeyboardState(m_keyboardState);
    return CLIENT_STATUS::SUCCESS;
}

CLIENT_STATUS Client::InputUninitialize()
{
    if (!m_inputInitialized)
    {
        return CLIENT_STATUS::ALREADY_DONE;
    }
    UnhookWindowsHookEx(m_hKeyboardHook);
    m_inputInitialized = false;
    return CLIENT_STATUS::SUCCESS;
}