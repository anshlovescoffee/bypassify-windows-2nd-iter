/*
imagehelper.hpp
Header for the DirectX image screenshot library
*/

#include <d3d11.h>
#include <string>

namespace ImageHelper
{
    enum class IMAGE_CODEC
    {
        BMP,
        GIF,
        HEIF,
        ICO,
        JPEG,
        PNG,
        TIFF,
        WMP
    };

    ID3D11ShaderResourceView* CaptureImage(ID3D11Device* pDevice, ID3D11Resource* pTexture);
    ID3D11ShaderResourceView* CaptureImage(ID3D11Device* pDevice, ID3D11Texture2D* pTexture);
    bool SaveImageToFile(ID3D11ShaderResourceView* pImage, IMAGE_CODEC codec, wchar_t* outputPath);
    bool GetImageDesc(ID3D11ShaderResourceView* pImage, D3D11_TEXTURE2D_DESC& rDesc);
    std::string ImageToBase64(ID3D11ShaderResourceView* pImage, IMAGE_CODEC codec);
    std::string BufferToBase64(const uint8_t* pBuffer, size_t size);
    char ByteToBase64Char(uint8_t b);
    GUID GetCodecGuid(IMAGE_CODEC codec);
}