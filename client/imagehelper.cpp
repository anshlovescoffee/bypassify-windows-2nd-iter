/*
imagehelper.hpp
DirectX image screenshot library/utility implementation
*/

#include "imagehelper.hpp"
#include "DirectXTex.h"
#include <stdio.h>

using namespace ImageHelper;
using namespace DirectX;

ID3D11ShaderResourceView* ImageHelper::CaptureImage(ID3D11Device* pDevice, ID3D11Resource* pTexture)
{
    ID3D11Texture2D* pCastedTexture = nullptr;
    if (pTexture && SUCCEEDED(pTexture->QueryInterface(__uuidof(ID3D11Texture2D*), (void**)&pCastedTexture)))
    {
        return CaptureImage(pDevice, pCastedTexture);
    }
    return nullptr;
}

ID3D11ShaderResourceView* ImageHelper::CaptureImage(ID3D11Device* pDevice, ID3D11Texture2D* pTexture)
{
    ID3D11DeviceContext* pContext = nullptr;
    ID3D11ShaderResourceView* result = nullptr;
    D3D11_TEXTURE2D_DESC textureDesc = {};
    ID3D11Texture2D* pCopyTexture = nullptr;
    
    if (!pTexture || !pDevice) // null checks
    {
        return result;
    }

    // create copy texture
    pTexture->GetDesc(&textureDesc);
    if (!SUCCEEDED(pDevice->CreateTexture2D(&textureDesc, nullptr, &pCopyTexture)))
    {
        return result;
    }

    // get device context
    pDevice->GetImmediateContext(&pContext);
    if (!pContext)
    {
        pCopyTexture->Release();
        return result;
    }

    // copy data and create render target view
    pContext->CopyResource(pCopyTexture, pTexture);
    pDevice->CreateShaderResourceView(pCopyTexture, nullptr, &result);
    pCopyTexture->Release();
    pContext->Release();

    D3D11_RENDER_TARGET_VIEW_DESC d;
    return result;
}

bool ImageHelper::SaveImageToFile(ID3D11ShaderResourceView* pImage, IMAGE_CODEC codec, wchar_t* outputPath)
{
    bool succeeded = false;
    ID3D11Device* pDevice = nullptr;
    ID3D11DeviceContext* pContext = nullptr;
    ID3D11Resource* pResource = nullptr;
    ScratchImage scratch = {};

    pImage->GetResource(&pResource);
    if (!pResource)
    {
        return succeeded;
    }

    pImage->GetDevice(&pDevice);
    if (!pDevice)
    {
        pResource->Release();
        return succeeded;
    }
    
    pDevice->GetImmediateContext(&pContext);
    if (!pContext)
    {
        pResource->Release();
        pDevice->Release();
        return succeeded;
    }

    if (SUCCEEDED(CaptureTexture(pDevice, pContext, pResource, scratch)))
    {
        HRESULT result = SaveToWICFile(
            *scratch.GetImage(0, 0, 0),
            WIC_FLAGS_FORCE_SRGB,
            GetCodecGuid(codec),
            outputPath
        );

        if (SUCCEEDED(result))
        {
            succeeded = true;
        }
        else
        {
            fprintf(stderr, "failed to save file award: 0x%x\n", result);
        }

        scratch.Release();
    }

    pDevice->Release();
    pResource->Release();
    pContext->Release();
    return succeeded;
}

bool ImageHelper::GetImageDesc(ID3D11ShaderResourceView* pImage, D3D11_TEXTURE2D_DESC& rDesc)
{
    ID3D11Resource* pResource = nullptr;
    ID3D11Texture2D* pTexture = nullptr;

    pImage->GetResource(&pResource);
    if (!pResource)
    {
        return false;
    }
    
    if (SUCCEEDED(pResource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&pTexture)))
    {
        pTexture->GetDesc(&rDesc);
        return true;
    }
    return false;
}

std::string ImageHelper::ImageToBase64(ID3D11ShaderResourceView* pImage, IMAGE_CODEC codec)
{
    std::string result = "";
    ID3D11Device* pDevice = nullptr;
    ID3D11DeviceContext* pContext = nullptr;
    ID3D11Resource* pResource = nullptr;
    Blob imageBlob = {};
    ScratchImage scratch = {};

    pImage->GetResource(&pResource);
    if (!pResource)
    {
        return result;
    }

    pImage->GetDevice(&pDevice);
    if (!pDevice)
    {
        pResource->Release();
        return result;
    }
    
    pDevice->GetImmediateContext(&pContext);
    if (!pContext)
    {
        pResource->Release();
        pDevice->Release();
        return result;
    }

    if (SUCCEEDED(CaptureTexture(pDevice, pContext, pResource, scratch)))
    {
        if (SUCCEEDED(SaveToWICMemory(
            *scratch.GetImage(0, 0, 0),
            WIC_FLAGS_FORCE_SRGB,
            GetCodecGuid(codec),
            imageBlob
        )))
        { // with the image data blob we can now do base64 encoding yippee!!!
            result = BufferToBase64(imageBlob.GetBufferPointer(), imageBlob.GetBufferSize());
            imageBlob.Release();
        }
        scratch.Release();
    }

    pDevice->Release();
    pResource->Release();
    pContext->Release();
    return result;
}

std::string ImageHelper::BufferToBase64(const uint8_t* pBuffer, size_t size)
{
    std::string result = "";
    unsigned int staging = 0;
    int padding = 0;

    for (size_t i = 0; i < size; i += 3)
    {
        staging = 0;
        padding = 0;

        for (size_t j = 0; j < 3; ++j) // storing the bytes as follows: b0 b1 b2 00
        {
            if ((i + j) < size)
            {
                staging += pBuffer[i + j];
            }
            else
            {
                ++padding;
            }
            staging <<= 8;
        }

        for (int i = 0; i < (4 - padding); ++i) // apply base64 encoding
        {
            result += ByteToBase64Char(staging >> 26); // grabbing the upper 6 bits to process
            staging <<= 6; // sliding the upper 6 bytes out
        }

        for (int i = 0; i < padding; ++i) // apply padding
        {
            result += "=";
        }
    }

    fprintf(stderr, "Result: %s\n", result.c_str());
    return result;
}

char ImageHelper::ByteToBase64Char(uint8_t b)
{
    if (b <= 25) // 0 to 25
    {
        return 'A' + b;
    }
    else if (b <= 51) // 26 to 51
    {
        return 'a' + b - 26;
    }
    else if (b <= 61) // 52 to 61
    {
        return '0' + b - 52;
    }
    else if (b == 62) // 62
    {
        return '+';
    }
    else // 63
    {
        return '/';
    }
}

GUID ImageHelper::GetCodecGuid(IMAGE_CODEC codec)
{
    DirectX::WICCodecs wicCodec = WIC_CODEC_BMP;

    switch (codec)
    {
        case IMAGE_CODEC::BMP:
            wicCodec = WIC_CODEC_BMP;
            break;
        case IMAGE_CODEC::GIF:
            wicCodec = WIC_CODEC_GIF;
            break;
        case IMAGE_CODEC::HEIF:
            wicCodec = WIC_CODEC_HEIF;
            break;
        case IMAGE_CODEC::ICO:
            wicCodec = WIC_CODEC_ICO;
            break;
        case IMAGE_CODEC::JPEG:
            wicCodec = WIC_CODEC_JPEG;
            break;
        case IMAGE_CODEC::PNG:
            wicCodec = WIC_CODEC_PNG;
            break;
        case IMAGE_CODEC::TIFF:
            wicCodec = WIC_CODEC_TIFF;
            break;
        case IMAGE_CODEC::WMP:
            wicCodec = WIC_CODEC_WMP;
            break;
    }

    return GetWICCodec(wicCodec);
}