/*
mathrender.hpp
Renders LaTeX math blocks as D3D11 textures via codecogs API
Supports inline math (same line as text) and block math (centered, own line)
*/

#pragma once

#include <d3d11.h>
#include <string>
#include <vector>

namespace MathRender
{
    enum class SegmentType { Text, Math };
    
    struct Segment
    {
        SegmentType type;
        std::string text;
        ID3D11ShaderResourceView* texture;
        int texWidth;
        int texHeight;
        bool renderFailed;
        bool isInline;          // true for $...$ and \(...\), false for $$...$$, \[...\], \begin{}
    };
    
    struct ParsedMessage
    {
        std::vector<Segment> segments;
    };
    
    void Initialize(ID3D11Device* pDevice);
    ParsedMessage Parse(const std::string& response);
    
    // msgIndex used for dispatch tracking. fontHeight = ImGui line height. isDark = theme.
    bool RenderPending(ParsedMessage& msg, size_t msgIndex, float fontHeight, bool isDark = true);
    
    void Release(ParsedMessage& msg);
    ID3D11ShaderResourceView* CreateTextureFromPNG(const void* pData, size_t dataSize, int& outWidth, int& outHeight);
    std::vector<uint8_t> DownloadMathPNG(const std::string& latex, int fontSize, const std::string& fgColor);
}