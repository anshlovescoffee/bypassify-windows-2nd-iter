/*
mathrender.cpp
Hybrid math rendering:
- Inline math ($...$, \(...\)) -> Unicode text (flows naturally with TextWrapped)
- Block math ($$...$$, \[...\], \begin{}) -> codecogs PNG images (centered on own line)

Thread safety:
- Worker threads ONLY download PNG data
- PNG -> texture happens ONLY on main/render thread
- Segments referenced by (msgIndex, segIndex), never raw pointers
*/

#include "mathrender.hpp"
#include <windows.h>
#include <winhttp.h>
#include <wincodec.h>
#include <thread>
#include <mutex>
#include <queue>
#include <set>
#include <map>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "windowscodecs.lib")

using namespace MathRender;

static ID3D11Device* g_pDevice = nullptr;

struct RenderResult
{
    size_t msgIndex;
    size_t segIndex;
    std::vector<uint8_t> pngData;
};

static std::mutex g_queueMutex;
static std::queue<RenderResult> g_resultQueue;

static std::mutex g_dispatchMutex;
static std::set<uint64_t> g_dispatched;

static uint64_t MakeKey(size_t m, size_t s) { return ((uint64_t)m << 32) | (uint64_t)(s & 0xFFFFFFFF); }
static bool IsDispatched(size_t m, size_t s) { std::lock_guard<std::mutex> l(g_dispatchMutex); return g_dispatched.count(MakeKey(m, s)) > 0; }
static void MarkDispatched(size_t m, size_t s) { std::lock_guard<std::mutex> l(g_dispatchMutex); g_dispatched.insert(MakeKey(m, s)); }

void MathRender::Initialize(ID3D11Device* pDevice) { g_pDevice = pDevice; }

// ============================================
// LaTeX -> Unicode conversion for inline math
// ============================================

static const std::map<std::string, std::string> g_greekLetters = {
    {"\\alpha", "\xCE\xB1"}, {"\\beta", "\xCE\xB2"}, {"\\gamma", "\xCE\xB3"},
    {"\\delta", "\xCE\xB4"}, {"\\epsilon", "\xCE\xB5"}, {"\\varepsilon", "\xCE\xB5"},
    {"\\zeta", "\xCE\xB6"}, {"\\eta", "\xCE\xB7"}, {"\\theta", "\xCE\xB8"},
    {"\\iota", "\xCE\xB9"}, {"\\kappa", "\xCE\xBA"}, {"\\lambda", "\xCE\xBB"},
    {"\\mu", "\xCE\xBC"}, {"\\nu", "\xCE\xBD"}, {"\\xi", "\xCE\xBE"},
    {"\\pi", "\xCF\x80"}, {"\\rho", "\xCF\x81"}, {"\\sigma", "\xCF\x83"},
    {"\\tau", "\xCF\x84"}, {"\\upsilon", "\xCF\x85"}, {"\\phi", "\xCF\x86"},
    {"\\varphi", "\xCF\x95"}, {"\\chi", "\xCF\x87"}, {"\\psi", "\xCF\x88"},
    {"\\omega", "\xCF\x89"},
    {"\\Gamma", "\xCE\x93"}, {"\\Delta", "\xCE\x94"}, {"\\Theta", "\xCE\x98"},
    {"\\Lambda", "\xCE\x9B"}, {"\\Xi", "\xCE\x9E"}, {"\\Pi", "\xCE\xA0"},
    {"\\Sigma", "\xCE\xA3"}, {"\\Phi", "\xCE\xA6"}, {"\\Psi", "\xCE\xA8"},
    {"\\Omega", "\xCE\xA9"},
};

static const std::map<std::string, std::string> g_mathSymbols = {
    {"\\times", "\xC3\x97"}, {"\\div", "\xC3\xB7"}, {"\\cdot", "\xC2\xB7"},
    {"\\pm", "\xC2\xB1"}, {"\\mp", "\xE2\x88\x93"},
    {"\\leq", "\xE2\x89\xA4"}, {"\\le", "\xE2\x89\xA4"},
    {"\\geq", "\xE2\x89\xA5"}, {"\\ge", "\xE2\x89\xA5"},
    {"\\neq", "\xE2\x89\xA0"}, {"\\ne", "\xE2\x89\xA0"},
    {"\\approx", "\xE2\x89\x88"}, {"\\equiv", "\xE2\x89\xA1"},
    {"\\sim", "\xE2\x88\xBC"}, {"\\propto", "\xE2\x88\x9D"},
    {"\\infty", "\xE2\x88\x9E"}, {"\\partial", "\xE2\x88\x82"},
    {"\\nabla", "\xE2\x88\x87"}, {"\\forall", "\xE2\x88\x80"},
    {"\\exists", "\xE2\x88\x83"}, {"\\in", "\xE2\x88\x88"},
    {"\\notin", "\xE2\x88\x89"}, {"\\subset", "\xE2\x8A\x82"},
    {"\\supset", "\xE2\x8A\x83"}, {"\\subseteq", "\xE2\x8A\x86"},
    {"\\cup", "\xE2\x88\xAA"}, {"\\cap", "\xE2\x88\xA9"},
    {"\\emptyset", "\xE2\x88\x85"}, {"\\land", "\xE2\x88\xA7"},
    {"\\lor", "\xE2\x88\xA8"}, {"\\neg", "\xC2\xAC"},
    {"\\Rightarrow", "\xE2\x87\x92"}, {"\\Leftarrow", "\xE2\x87\x90"},
    {"\\rightarrow", "\xE2\x86\x92"}, {"\\to", "\xE2\x86\x92"},
    {"\\leftarrow", "\xE2\x86\x90"}, {"\\gets", "\xE2\x86\x90"},
    {"\\therefore", "\xE2\x88\xB4"}, {"\\angle", "\xE2\x88\xA0"},
    {"\\degree", "\xC2\xB0"}, {"\\circ", "\xC2\xB0"},
    {"\\prime", "\xE2\x80\xB2"}, {"\\ldots", "\xE2\x80\xA6"},
    {"\\cdots", "\xE2\x8B\xAF"}, {"\\quad", "  "}, {"\\qquad", "    "},
    {"\\,", " "}, {"\\;", " "}, {"\\:", " "}, {"\\!", ""},
    {"\\left", ""}, {"\\right", ""},
    {"\\big", ""}, {"\\Big", ""}, {"\\bigg", ""}, {"\\Bigg", ""},
    {"\\lfloor", "\xE2\x8C\x8A"}, {"\\rfloor", "\xE2\x8C\x8B"},
    {"\\lceil", "\xE2\x8C\x88"}, {"\\rceil", "\xE2\x8C\x89"},
    {"\\langle", "\xE2\x9F\xA8"}, {"\\rangle", "\xE2\x9F\xA9"},
    {"\\vert", "|"}, {"\\lVert", "\xE2\x80\x96"}, {"\\rVert", "\xE2\x80\x96"},
};

static const std::map<char, std::string> g_superscripts = {
    {'0', "\xE2\x81\xB0"}, {'1', "\xC2\xB9"}, {'2', "\xC2\xB2"},
    {'3', "\xC2\xB3"}, {'4', "\xE2\x81\xB4"}, {'5', "\xE2\x81\xB5"},
    {'6', "\xE2\x81\xB6"}, {'7', "\xE2\x81\xB7"}, {'8', "\xE2\x81\xB8"},
    {'9', "\xE2\x81\xB9"}, {'+', "\xE2\x81\xBA"}, {'-', "\xE2\x81\xBB"},
    {'=', "\xE2\x81\xBC"}, {'(', "\xE2\x81\xBD"}, {')', "\xE2\x81\xBE"},
    {'n', "\xE2\x81\xBF"}, {'i', "\xE2\x81\xB1"},
    {'a', "\xE1\xB5\x83"}, {'b', "\xE1\xB5\x87"}, {'c', "\xE1\xB6\x9C"},
    {'d', "\xE1\xB5\x88"}, {'e', "\xE1\xB5\x89"}, {'f', "\xE1\xB6\xA0"},
    {'g', "\xE1\xB5\x8D"}, {'h', "\xCA\xB0"}, {'j', "\xCA\xB2"},
    {'k', "\xE1\xB5\x8F"}, {'l', "\xCB\xA1"}, {'m', "\xE1\xB5\x90"},
    {'o', "\xE1\xB5\x92"}, {'p', "\xE1\xB5\x96"}, {'r', "\xCA\xB3"},
    {'s', "\xCB\xA2"}, {'t', "\xE1\xB5\x97"}, {'u', "\xE1\xB5\x98"},
    {'v', "\xE1\xB5\x9B"}, {'w', "\xCA\xB7"}, {'x', "\xCB\xA3"},
    {'y', "\xCA\xB8"}, {'z', "\xE1\xB6\xBB"}, {'T', "\xE1\xB5\x80"},
};

static const std::map<char, std::string> g_subscripts = {
    {'0', "\xE2\x82\x80"}, {'1', "\xE2\x82\x81"}, {'2', "\xE2\x82\x82"},
    {'3', "\xE2\x82\x83"}, {'4', "\xE2\x82\x84"}, {'5', "\xE2\x82\x85"},
    {'6', "\xE2\x82\x86"}, {'7', "\xE2\x82\x87"}, {'8', "\xE2\x82\x88"},
    {'9', "\xE2\x82\x89"}, {'+', "\xE2\x82\x8A"}, {'-', "\xE2\x82\x8B"},
    {'a', "\xE2\x82\x90"}, {'e', "\xE2\x82\x91"}, {'h', "\xE2\x82\x95"},
    {'i', "\xE1\xB5\xA2"}, {'k', "\xE2\x82\x96"}, {'l', "\xE2\x82\x97"},
    {'m', "\xE2\x82\x98"}, {'n', "\xE2\x82\x99"}, {'o', "\xE2\x82\x92"},
    {'p', "\xE2\x82\x93"}, {'r', "\xE1\xB5\xA3"}, {'s', "\xE2\x82\x9B"},
    {'t', "\xE2\x82\x9C"}, {'u', "\xE1\xB5\xA4"}, {'v', "\xE1\xB5\xA5"},
    {'x', "\xE2\x82\x93"},
};

static std::string ToSuperscript(const std::string& text)
{
    std::string r;
    for (char c : text) {
        auto it = g_superscripts.find(c);
        r += (it != g_superscripts.end()) ? it->second : (std::string("^") + c);
    }
    return r;
}

static std::string ToSubscript(const std::string& text)
{
    std::string r;
    for (char c : text) {
        auto it = g_subscripts.find(c);
        r += (it != g_subscripts.end()) ? it->second : (std::string("_") + c);
    }
    return r;
}

static size_t FindMatchingBrace(const std::string& s, size_t pos)
{
    if (pos >= s.size() || s[pos] != '{') return std::string::npos;
    int depth = 1;
    for (size_t i = pos + 1; i < s.size(); i++) {
        if (s[i] == '{') depth++;
        else if (s[i] == '}') { if (--depth == 0) return i; }
    }
    return std::string::npos;
}

static std::string ExtractBraceContent(const std::string& s, size_t& pos)
{
    if (pos >= s.size()) return "";
    if (s[pos] == '{') {
        size_t end = FindMatchingBrace(s, pos);
        if (end != std::string::npos) {
            std::string c = s.substr(pos + 1, end - pos - 1);
            pos = end + 1;
            return c;
        }
    }
    std::string r(1, s[pos]);
    pos++;
    return r;
}

static bool StartsWithCmd(const std::string& s, size_t pos, const std::string& cmd)
{
    if (pos + cmd.size() > s.size()) return false;
    if (s.compare(pos, cmd.size(), cmd) != 0) return false;
    if (pos + cmd.size() < s.size()) {
        char next = s[pos + cmd.size()];
        if (cmd[0] == '\\' && ((next >= 'a' && next <= 'z') || (next >= 'A' && next <= 'Z')))
            return false;
    }
    return true;
}

static std::string ProcessMathToUnicode(const std::string& math);

static std::string ProcessFrac(const std::string& s, size_t& pos)
{
    std::string num = ExtractBraceContent(s, pos);
    std::string den = ExtractBraceContent(s, pos);
    std::string pN = ProcessMathToUnicode(num);
    std::string pD = ProcessMathToUnicode(den);
    if (pN.size() <= 3 && pD.size() <= 3)
        return pN + "/" + pD;
    return "(" + pN + ")/(" + pD + ")";
}

static std::string ProcessSqrt(const std::string& s, size_t& pos)
{
    std::string index;
    if (pos < s.size() && s[pos] == '[') {
        size_t end = s.find(']', pos);
        if (end != std::string::npos) { index = s.substr(pos + 1, end - pos - 1); pos = end + 1; }
    }
    std::string content = ExtractBraceContent(s, pos);
    std::string proc = ProcessMathToUnicode(content);
    if (index.empty())
        return "\xE2\x88\x9A(" + proc + ")";
    return ToSuperscript(index) + "\xE2\x88\x9A(" + proc + ")";
}

static std::string ProcessMathToUnicode(const std::string& math)
{
    std::string result;
    size_t i = 0;
    while (i < math.size())
    {
        if (math[i] == ' ' || math[i] == '\t') { result += ' '; i++; continue; }

        if (math[i] == '\\')
        {
            if (StartsWithCmd(math, i, "\\frac")) { i += 5; result += ProcessFrac(math, i); continue; }
            if (StartsWithCmd(math, i, "\\dfrac") || StartsWithCmd(math, i, "\\tfrac")) { i += 6; result += ProcessFrac(math, i); continue; }
            if (StartsWithCmd(math, i, "\\sqrt")) { i += 5; result += ProcessSqrt(math, i); continue; }

            // \text, \mathrm, \mathbf etc - extract brace content
            const char* textCmds[] = { "\\text", "\\mathrm", "\\textit", "\\textbf", "\\mathbf", "\\mathit", "\\operatorname", "\\boldsymbol", nullptr };
            bool foundText = false;
            for (int t = 0; textCmds[t]; t++) {
                if (StartsWithCmd(math, i, textCmds[t])) {
                    while (i < math.size() && math[i] != '{') i++;
                    result += ExtractBraceContent(math, i);
                    foundText = true; break;
                }
            }
            if (foundText) continue;

            if (StartsWithCmd(math, i, "\\boxed")) { i += 6; result += "[" + ProcessMathToUnicode(ExtractBraceContent(math, i)) + "]"; continue; }

            // Named functions
            struct { const char* cmd; const char* disp; } funcs[] = {
                {"\\sum", "\xE2\x88\x91"}, {"\\prod", "\xE2\x88\x8F"}, {"\\int", "\xE2\x88\xAB"},
                {"\\iint", "\xE2\x88\xAC"}, {"\\iiint", "\xE2\x88\xAD"}, {"\\oint", "\xE2\x88\xAE"},
                {"\\lim", "lim"}, {"\\limsup", "lim sup"}, {"\\liminf", "lim inf"},
                {"\\log", "log"}, {"\\ln", "ln"}, {"\\lg", "lg"}, {"\\exp", "exp"},
                {"\\sin", "sin"}, {"\\cos", "cos"}, {"\\tan", "tan"},
                {"\\sec", "sec"}, {"\\csc", "csc"}, {"\\cot", "cot"},
                {"\\arcsin", "arcsin"}, {"\\arccos", "arccos"}, {"\\arctan", "arctan"},
                {"\\sinh", "sinh"}, {"\\cosh", "cosh"}, {"\\tanh", "tanh"},
                {"\\max", "max"}, {"\\min", "min"}, {"\\sup", "sup"}, {"\\inf", "inf"},
                {"\\det", "det"}, {"\\dim", "dim"}, {"\\ker", "ker"},
                {"\\gcd", "gcd"}, {"\\lcm", "lcm"}, {"\\mod", "mod"},
                {"\\arg", "arg"}, {"\\deg", "deg"},
                {nullptr, nullptr}
            };
            bool foundFunc = false;
            for (int f = 0; funcs[f].cmd; f++) {
                if (StartsWithCmd(math, i, funcs[f].cmd)) {
                    result += funcs[f].disp; i += strlen(funcs[f].cmd); foundFunc = true; break;
                }
            }
            if (foundFunc) continue;

            // Greek letters
            bool foundGreek = false;
            for (const auto& p : g_greekLetters) {
                if (StartsWithCmd(math, i, p.first)) { result += p.second; i += p.first.size(); foundGreek = true; break; }
            }
            if (foundGreek) continue;

            // Math symbols
            bool foundSym = false;
            for (const auto& p : g_mathSymbols) {
                if (StartsWithCmd(math, i, p.first)) { result += p.second; i += p.first.size(); foundSym = true; break; }
            }
            if (foundSym) continue;

            // \\ newline
            if (i + 1 < math.size() && math[i + 1] == '\\') { result += "\n"; i += 2; continue; }

            // Unknown command - skip
            i++;
            while (i < math.size() && ((math[i] >= 'a' && math[i] <= 'z') || (math[i] >= 'A' && math[i] <= 'Z'))) i++;
            continue;
        }

        if (math[i] == '^') { i++; result += ToSuperscript(ProcessMathToUnicode(ExtractBraceContent(math, i))); continue; }
        if (math[i] == '_') { i++; result += ToSubscript(ProcessMathToUnicode(ExtractBraceContent(math, i))); continue; }
        if (math[i] == '{') { result += ProcessMathToUnicode(ExtractBraceContent(math, i)); continue; }
        if (math[i] == '}') { i++; continue; }
        if (math[i] == '&') { result += "  "; i++; continue; }

        result += math[i];
        i++;
    }
    return result;
}

// ============================================
// URL encode
// ============================================

static std::string UrlEncode(const std::string& str)
{
    std::string r;
    r.reserve(str.size() * 3);
    for (unsigned char c : str)
    {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~')
            r += (char)c;
        else if (c == ' ')
            r += "%20";
        else { char b[4]; snprintf(b, sizeof(b), "%%%02X", c); r += b; }
    }
    return r;
}

// ============================================
// Parse - inline math -> Unicode text, block math -> image segments
// ============================================

ParsedMessage MathRender::Parse(const std::string& response)
{
    ParsedMessage msg;
    std::string currentText;

    auto flushText = [&]()
    {
        if (!currentText.empty())
        {
            msg.segments.push_back({ SegmentType::Text, currentText, nullptr, 0, 0, false, false });
            currentText.clear();
        }
    };

    auto addBlockMath = [&](const std::string& latex)
    {
        flushText();
        msg.segments.push_back({ SegmentType::Math, latex, nullptr, 0, 0, false, false });
    };

    auto addInlineMath = [&](const std::string& latex)
    {
        // Convert inline math to Unicode and merge directly into text stream
        currentText += ProcessMathToUnicode(latex);
    };

    size_t i = 0;
    while (i < response.size())
    {
        // \begin{...}...\end{...}  -> BLOCK image
        if (i + 6 < response.size() && response.compare(i, 6, "\\begin") == 0)
        {
            size_t bs = response.find('{', i);
            size_t be = (bs != std::string::npos) ? response.find('}', bs) : std::string::npos;
            if (bs != std::string::npos && be != std::string::npos)
            {
                std::string env = response.substr(bs + 1, be - bs - 1);
                std::string endTag = "\\end{" + env + "}";
                size_t ee = response.find(endTag, be);
                if (ee != std::string::npos)
                {
                    addBlockMath(response.substr(i, ee + endTag.size() - i));
                    i = ee + endTag.size();
                    continue;
                }
            }
        }

        // $$...$$ -> BLOCK image
        if (i + 1 < response.size() && response[i] == '$' && response[i + 1] == '$')
        {
            size_t end = response.find("$$", i + 2);
            if (end != std::string::npos)
            {
                addBlockMath(response.substr(i + 2, end - i - 2));
                i = end + 2;
                continue;
            }
        }

        // \[...\] -> BLOCK image
        if (i + 1 < response.size() && response[i] == '\\' && response[i + 1] == '[')
        {
            size_t end = response.find("\\]", i + 2);
            if (end != std::string::npos)
            {
                addBlockMath(response.substr(i + 2, end - i - 2));
                i = end + 2;
                continue;
            }
        }

        // \(...\) -> INLINE Unicode text
        if (i + 1 < response.size() && response[i] == '\\' && response[i + 1] == '(')
        {
            size_t end = response.find("\\)", i + 2);
            if (end != std::string::npos)
            {
                addInlineMath(response.substr(i + 2, end - i - 2));
                i = end + 2;
                continue;
            }
        }

        // $...$ -> INLINE Unicode text
        if (response[i] == '$')
        {
            size_t end = response.find('$', i + 1);
            if (end != std::string::npos && end > i + 1)
            {
                addInlineMath(response.substr(i + 1, end - i - 1));
                i = end + 1;
                continue;
            }
        }

        currentText += response[i];
        i++;
    }

    flushText();
    return msg;
}

// ============================================
// Download PNG (worker thread - no D3D, no COM)
// ============================================

std::vector<uint8_t> MathRender::DownloadMathPNG(const std::string& latex, int dpi, const std::string& fgColor)
{
    std::vector<uint8_t> result;

    std::string expr = "\\dpi{" + std::to_string(dpi) + "}";
    if (!fgColor.empty())
        expr += "\\color{" + fgColor + "}";
    expr += " " + latex;

    std::string path = "/png.image?" + UrlEncode(expr);

    HINTERNET hSession = WinHttpOpen(L"Bypassify/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
    if (!hSession) return result;
    HINTERNET hConnect = WinHttpConnect(hSession, L"latex.codecogs.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return result; }

    int wl = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, NULL, 0);
    std::vector<wchar_t> wp(wl);
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wp.data(), wl);

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", wp.data(), NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return result; }

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, NULL, 0, 0, 0) ||
        !WinHttpReceiveResponse(hRequest, NULL))
    { WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return result; }

    DWORD sc = 0, ss = sizeof(sc);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, NULL, &sc, &ss, NULL);
    if (sc != 200)
    { WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return result; }

    DWORD avail = 0, rd = 0;
    while (WinHttpQueryDataAvailable(hRequest, &avail) && avail > 0)
    {
        std::vector<uint8_t> buf(avail);
        if (WinHttpReadData(hRequest, buf.data(), avail, &rd))
            result.insert(result.end(), buf.begin(), buf.begin() + rd);
    }

    WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
    return result;
}

// ============================================
// Create texture from PNG (MAIN THREAD ONLY)
// Simple: decode PNG, create texture at native resolution. No scaling.
// ============================================

ID3D11ShaderResourceView* MathRender::CreateTextureFromPNG(const void* pData, size_t dataSize, int& outWidth, int& outHeight)
{
    if (!g_pDevice || !pData || dataSize == 0) return nullptr;
    ID3D11ShaderResourceView* pSRV = nullptr;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);

    IWICImagingFactory* pF = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pF)))) return nullptr;

    IWICStream* pS = nullptr;
    if (FAILED(pF->CreateStream(&pS))) { pF->Release(); return nullptr; }
    if (FAILED(pS->InitializeFromMemory((BYTE*)pData, (DWORD)dataSize))) { pS->Release(); pF->Release(); return nullptr; }

    IWICBitmapDecoder* pD = nullptr;
    if (FAILED(pF->CreateDecoderFromStream(pS, NULL, WICDecodeMetadataCacheOnLoad, &pD)))
    { pS->Release(); pF->Release(); return nullptr; }

    IWICBitmapFrameDecode* pFr = nullptr;
    if (FAILED(pD->GetFrame(0, &pFr)))
    { pD->Release(); pS->Release(); pF->Release(); return nullptr; }

    IWICFormatConverter* pC = nullptr;
    if (FAILED(pF->CreateFormatConverter(&pC)))
    { pFr->Release(); pD->Release(); pS->Release(); pF->Release(); return nullptr; }

    if (FAILED(pC->Initialize(pFr, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeCustom)))
    { pC->Release(); pFr->Release(); pD->Release(); pS->Release(); pF->Release(); return nullptr; }

    UINT w = 0, h = 0;
    pC->GetSize(&w, &h);
    outWidth = (int)w;
    outHeight = (int)h;

    UINT stride = w * 4;
    std::vector<BYTE> px(stride * h);
    if (SUCCEEDED(pC->CopyPixels(NULL, stride, (UINT)px.size(), px.data())))
    {
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA sd = {};
        sd.pSysMem = px.data(); sd.SysMemPitch = stride;

        ID3D11Texture2D* pTex = nullptr;
        if (SUCCEEDED(g_pDevice->CreateTexture2D(&td, &sd, &pTex)) && pTex)
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
            sv.Format = td.Format; sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            sv.Texture2D.MipLevels = 1;
            g_pDevice->CreateShaderResourceView(pTex, &sv, &pSRV);
            pTex->Release();
        }
    }

    pC->Release(); pFr->Release(); pD->Release(); pS->Release(); pF->Release();
    return pSRV;
}

// ============================================
// Worker: download at DPI matched to display size (no scaling needed)
// ============================================

static void DownloadWorker(size_t msgIdx, size_t segIdx, std::string latex, float fontHeight, bool isDark)
{
    int dpi = 120;

    // Black text for light theme (empty = default black on white), white for dark
    auto png = MathRender::DownloadMathPNG(latex, dpi, isDark ? "white" : "");

    RenderResult r;
    r.msgIndex = msgIdx;
    r.segIndex = segIdx;
    r.pngData = std::move(png);

    std::lock_guard<std::mutex> lock(g_queueMutex);
    g_resultQueue.push(std::move(r));
}

// ============================================
// RenderPending - MAIN THREAD each frame
// ============================================

bool MathRender::RenderPending(ParsedMessage& msg, size_t msgIndex, float fontHeight, bool isDark)
{
    // 1) Process completed downloads - create texture at native PNG resolution
    {
        std::lock_guard<std::mutex> lock(g_queueMutex);
        size_t n = g_resultQueue.size();
        for (size_t q = 0; q < n; q++)
        {
            RenderResult res = std::move(g_resultQueue.front());
            g_resultQueue.pop();

            if (res.msgIndex == msgIndex && res.segIndex < msg.segments.size())
            {
                Segment& seg = msg.segments[res.segIndex];
                if (res.pngData.empty())
                {
                    seg.renderFailed = true;
                }
                else
                {
                    int w = 0, h = 0;
                    auto* tex = CreateTextureFromPNG(res.pngData.data(), res.pngData.size(), w, h);
                    if (tex)
                    {
                        seg.texture = tex;
                        seg.texWidth = w;
                        seg.texHeight = h;
                    }
                    else
                        seg.renderFailed = true;
                }
            }
            else
            {
                g_resultQueue.push(std::move(res));
            }
        }
    }

    // 2) Dispatch downloads for unstarted BLOCK math segments
    bool allDone = true;
    for (size_t s = 0; s < msg.segments.size(); s++)
    {
        Segment& seg = msg.segments[s];
        if (seg.type == SegmentType::Math && !seg.texture && !seg.renderFailed)
        {
            if (!IsDispatched(msgIndex, s))
            {
                MarkDispatched(msgIndex, s);
                std::thread(DownloadWorker, msgIndex, s, seg.text, fontHeight, isDark).detach();
            }
            allDone = false;
        }
    }

    return allDone;
}

// ============================================
// Cleanup
// ============================================

void MathRender::Release(ParsedMessage& msg)
{
    for (auto& seg : msg.segments)
    {
        if (seg.texture) { seg.texture->Release(); seg.texture = nullptr; }
    }
    msg.segments.clear();
}