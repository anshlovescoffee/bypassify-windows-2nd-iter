/*
client.cpp
Implements the GUI for the overlay
*/

#define IMGUI_IMPL_WIN32_DISABLE_GAMEPAD
#define QUIT_POPUP_TITLE "Warning##Quit"

#include <stdio.h>
#include <unordered_set>
#include <Shlwapi.h>

#include "client.hpp"
#include "imagehelper.hpp"
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
bool m_wantScreenshot = false; // whether to screenshot the next frame
float m_screenshotListHeight = 300.0f;
std::vector<ID3D11ShaderResourceView*> m_screenshots;
std::unordered_set<int> m_selectedScreenshots;

// export menu
char m_exportFolder[1000];
wchar_t m_exportFolderW[1000];
wchar_t m_exportFileW[1000];
char* m_exportFormatList[] = {".bmp", ".gif", ".heif", ".ico", ".jpg", ".png", ".tiff", ".wmp"};
wchar_t* m_exportFormatListW[] = {L".bmp", L".gif", L".heif", L".ico", L".jpg", L".png", L".tiff", L".wmp"};
int m_exportFormat = 0;
bool m_showExportScreen = false;

void DrawQuitWarning()
{
    if (ImGui::BeginPopup(QUIT_POPUP_TITLE))
    {
        ImGui::Text("A system restart is recommended after quitting. Are you sure you want to quit?");
        if (ImGui::Button("Yes"))
        {
            Client::Uninitialize(); // do our best to cleanup before leaving
            TerminateProcess(GetCurrentProcess(), 0);
        }
        ImGui::SameLine();
        if (ImGui::Button("No"))
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void DrawScreenshots()
{
    D3D11_TEXTURE2D_DESC desc = {};
    ImGuiStyle& style = ImGui::GetStyle();
    float shrinkFactor = 1.0f; // using this to make sure all images have consistent downscaling in the gui

    if (ImGui::Begin("Screenshots"))
    {
        ImGui::Text("DWM Overlay Demo by https://github.com/chaosium43");
        ImGui::Text("Application FPS: %.3f", ImGui::GetIO().Framerate);
        ImGui::Checkbox("Enable Input Blocking", &m_enableInputBlocking);
        ImGui::Checkbox("Enable Clipboard", &m_enableClipboard);
        
        if (ImGui::BeginChild("Images", ImVec2(0.0f, m_screenshotListHeight), 0, ImGuiWindowFlags_AlwaysVerticalScrollbar))
        {
            for (int i = 0; i < m_screenshots.size(); ++i)
            {
                if (!ImageHelper::GetImageDesc(m_screenshots[i], desc))
                {
                    continue;
                }

                ImGui::PushID(i);
                if (m_selectedScreenshots.count(i))
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.5f, 1.0f, 1.0f));
                    ImGui::Text("%d", i + 1);
                    ImGui::PopStyleColor();
                }
                else
                {
                    ImGui::Text("%d", i + 1);
                }
                
                shrinkFactor = desc.Width / (ImGui::GetContentRegionAvail().x - ImGui::GetItemRectSize().x - style.FramePadding.x * 4.0f);
                ImGui::SameLine();
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
                if (ImGui::ImageButton(
                    "Screenshot",
                    (ImTextureID)m_screenshots[i],
                    ImVec2(desc.Width / shrinkFactor, desc.Height / shrinkFactor)
                )) // rendering, selecting, and deselecting each button
                {
                    if (m_selectedScreenshots.count(i))
                    {
                        m_selectedScreenshots.erase(i);
                    }
                    else
                    {
                        m_selectedScreenshots.insert(i);
                    }
                }
                ImGui::PopStyleVar();
                ImGui::PopID();
            }
        }
        ImGui::EndChild();

        if (ImGui::Button("Take Screenshot"))
        {
            m_wantScreenshot = true;
        }

        ImGui::SameLine();
        if (ImGui::Button("Select All"))
        {
            for (int i = 0; i < m_screenshots.size(); ++i)
            {
                m_selectedScreenshots.insert(i);
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Deselect All"))
        {
            m_selectedScreenshots.clear();
        }

        ImGui::SameLine();
        if (ImGui::Button("Delete")) // very inefficient algorithm but nobody's taking more than 10 screenshots so it doesn't matter
        {
            for (int i = m_screenshots.size() - 1; i >= 0; --i)
            {
                if (m_selectedScreenshots.count(i))
                {
                    m_screenshots[i]->Release();
                    m_screenshots.erase(std::next(m_screenshots.begin(), i));
                }
            }
            m_selectedScreenshots.clear();
        }

        ImGui::SameLine();
        if (ImGui::Button("Export"))
        {
            m_showExportScreen = true;
        }

        ImGui::SameLine();
        if (ImGui::Button("Quit"))
        {
            ImGui::OpenPopup(QUIT_POPUP_TITLE);
        }
        DrawQuitWarning();
        
        m_screenshotListHeight += ImGui::GetContentRegionAvail().y; // dynamically adjust preview child window height based on space availability
    }
    ImGui::End();
}

void DrawExportScreen()
{
    if (ImGui::Begin("Export"))
    {
        ImGui::PushItemWidth(-FLT_MIN);

        ImGui::Text("Folder");
        ImGui::InputText("##Folder", m_exportFolder, sizeof(m_exportFolder));

        ImGui::Text("Format");
        ImGui::Combo("##Format", &m_exportFormat, m_exportFormatList, sizeof(m_exportFormatList) / sizeof(char*));
        
        if (ImGui::Button("Export"))
        {
            MultiByteToWideChar(CP_UTF8, 0, m_exportFolder, -1, m_exportFolderW, sizeof(m_exportFolderW) / sizeof(wchar_t)); // convert folder string to utf16
            for (int image : m_selectedScreenshots)
            {
                swprintf( // build export path for each file
                    m_exportFileW,
                    sizeof(m_exportFileW) / sizeof(wchar_t),
                    L"%ls\\screenshot-%d%ls",
                    m_exportFolderW,
                    image,
                    m_exportFormatListW[m_exportFormat]
                );
                ImageHelper::SaveImageToFile(m_screenshots[image], (ImageHelper::IMAGE_CODEC)m_exportFormat, m_exportFileW); // export file
            }
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Close"))
        {
            m_showExportScreen = false;
        }

        ImGui::PopItemWidth();
    }
    ImGui::End();
}

void DrawMenu()
{
    DrawScreenshots();

    if (m_showExportScreen)
    {
        DrawExportScreen();
    }
}

CLIENT_STATUS Client::Initialize(ID3D11Device1* pDevice, float dpiScale)
{
    ImFontConfig fontConfig = {};

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
    ImGuiStyle& style = ImGui::GetStyle();

    style.ScaleAllSizes(dpiScale);  
    io.FontGlobalScale = dpiScale;
    
    ImGui_ImplWin32_Init(m_hWnd);
    ImGui_ImplDX11_Init(m_pDevice, m_pDeviceContext);

    m_initialized = true;
    m_dpiScale = dpiScale;
    
    return InputInitialize();
}

CLIENT_STATUS Client::Uninitialize()
{
    if (!m_initialized)
    {
        return CLIENT_STATUS::ALREADY_DONE;
    }

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

    // freeing all captured screenshots
    for (ID3D11ShaderResourceView* item : m_screenshots)
    {
        item->Release();
    }
    m_screenshots.clear();
    m_selectedScreenshots.clear();
    
    return InputUninitialize();
}

CLIENT_STATUS Client::NextFrame(ID3D11Resource* pDxBuffer) // function does NOT auto release the buffer please release manually after this function call
{
    ID3D11RenderTargetView* pRenderTargetView = nullptr;
    CLIENT_STATUS status = CLIENT_STATUS::SUCCESS;
    ImGuiIO& io = ImGui::GetIO();
    ImGuiStyle& style = ImGui::GetStyle();

    if (!m_initialized)
    {
        return CLIENT_STATUS::FAILURE;
    }

    // need to call this every frame to drain the input message queue
    // try to call this as early as possible to reduce input lag
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
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }

    if (m_wantScreenshot) // take screenshot of whole desktop
    {
        ID3D11ShaderResourceView* screenshot = ImageHelper::CaptureImage(m_pDevice, pDxBuffer);
        int prevScreenshotCount = m_screenshots.size();

        if (screenshot)
        {
            m_screenshots.push_back(screenshot);
            m_wantScreenshot = false;
        }
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