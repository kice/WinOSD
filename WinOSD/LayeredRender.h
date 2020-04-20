#pragma once

#include "WindowRender.h"

#include <dxgi1_3.h>
#include <d3d11_2.h>
#include <d2d1_3.h>

#include <dcomp.h>
#include <dwrite_2.h>

#include <wrl/client.h>

#include <chrono>
#include <deque>
#include <mutex>

#include "Image.h"
#include "timer.h"

class LayeredRender : public WindowRender
{
public:
    LayeredRender(int width, int height, const std::wstring &name);

    int AddToast(const std::wstring &title, const std::wstring &text, const Image &im = Image());

    void OnInit()    override;
    void OnDestroy() override;

    bool OnUpdate() override;
    void OnRender() override;
    void OnMouse(MouseEvent event, int xPos, int yPos, bool shifted, bool ctrled) override;

    void OnTimer(int id) override;

    float GetDrawableWidth() const
    {
        return boxMaxWidth - marginLeft - marginRight;
    }

private:
    struct RenderCommand
    {
        enum RenderType
        {
            Text,
            TextNewline,
            Bitmap,
        };

        RenderCommand() = default;
        RenderCommand(RenderType rt) : renderType(rt) {};

        RenderType renderType;

        float width = 0.f, height = 0.f;

        Microsoft::WRL::ComPtr<IDWriteTextLayout> textLayout = nullptr;
        Image *im = nullptr;
    };

    struct Toast
    {
        UINT32 width, height;
        std::chrono::steady_clock::time_point addedTime;
        Microsoft::WRL::ComPtr<ID2D1Bitmap>  normal;
        Microsoft::WRL::ComPtr<ID2D1Bitmap>  highlight;

        int id;
    };

    void CreateDeviceIndependentResources();
    void CreateDeviceResources();

    // DXGI Resources
    Microsoft::WRL::ComPtr<IDXGIFactory2>           dxFactory;
    Microsoft::WRL::ComPtr<IDXGISwapChain1>         dxSwapChain;

    // DirectX 3D Resources
    D3D_FEATURE_LEVEL d3dFeatureLevel;
    Microsoft::WRL::ComPtr<ID3D11Device2>           d3dDevice;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext2>    d3dContext;
    Microsoft::WRL::ComPtr<IDXGISwapChain1>         d3dSwapChain;

    // Direct2D Resources
    Microsoft::WRL::ComPtr<ID2D1Factory1>           d2dFactory;
    Microsoft::WRL::ComPtr<ID2D1Device>             d2dDevice;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext>      d2dContext;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1>            d2dTargetBitmap;

    Microsoft::WRL::ComPtr<ID2D1DeviceContext4>     d2dRTContext;
    Microsoft::WRL::ComPtr<ID2D1BitmapRenderTarget> d2dCacheTarget;
    Microsoft::WRL::ComPtr<ID2D1Bitmap>             d2dEmptyBitmap;

    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>    d2dWhiteBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>    d2dTimeBrush;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush>    d2dBoxBrush;
    Microsoft::WRL::ComPtr<ID2D1Effect>             d2dEffectPremultiply;

    // DirectComposition Resources
    Microsoft::WRL::ComPtr<IDCompositionDevice>     dcompDevice;
    Microsoft::WRL::ComPtr<IDCompositionTarget>     dcompTarget;
    Microsoft::WRL::ComPtr<IDCompositionVisual>     dcompVisual;

    const D2D1_PIXEL_FORMAT pixelFormat = {
        DXGI_FORMAT_B8G8R8A8_UNORM,
        D2D1_ALPHA_MODE_PREMULTIPLIED
    };

    float boxMaxWidth = 350.f;
    float boxMaxHeight = 0.f;

    // undefined position
    float posX = NAN;
    float posY = NAN;

    const float glowWidth = 8.f;
    const float glowStep = 2.f;

    const D2D1::ColorF glowColor = D2D1::ColorF(0.f, 0.5f, 0.75f, 0.5f);
    const D2D1::ColorF highlightColor = D2D1::ColorF(1.f, 0.25f, 0.25f, 0.5f);

    const float marginTop = 22.f;
    const float marginRight = 15.f;
    const float marginBottom = 15.f;
    const float marginLeft = 15.f;

    const float titleFontSize = 26.f;
    const float textFontSize = 22.f;
    const float timeFontSize = 10.f;

    const std::wstring titleFontName = L"Î¢ÈíÑÅºÚ";
    const std::wstring textFontName = L"Î¢ÈíÑÅºÚ";
    const std::wstring emojiFontName = L"Segoe UI Emoji";
    const std::wstring timeFontName = L"DSEG7 Classic";

    HRESULT CreateTextFormat(const std::wstring &fontFamily, float fontSize,
                             IDWriteTextFormat **ppTextFormat) const;

    Microsoft::WRL::ComPtr<IDWriteFactory2>      dwFactory;
    Microsoft::WRL::ComPtr<IDWriteTextRenderer>  dwTextRenderer;
    Microsoft::WRL::ComPtr<IDWriteTextRenderer>  dwHighlightedRenderer;

    Microsoft::WRL::ComPtr<IDWriteTextFormat>    dwTitleFormat;
    Microsoft::WRL::ComPtr<IDWriteTextFormat>    dwTitleEmojiFormat;

    Microsoft::WRL::ComPtr<IDWriteTextFormat>    dwTextFormat;
    Microsoft::WRL::ComPtr<IDWriteTextFormat>    dwEmojiFormat;
    Microsoft::WRL::ComPtr<IDWriteTextFormat>    dwTimeStampFormat;

    std::atomic<bool> _ready;
    std::atomic<bool> _stop;
    std::atomic<int> _counter = 0;

    std::mutex renderLock;
    std::deque<Toast> toastList;
    std::atomic<bool> invalidated;

    int mouseHover = -1;

    HWND hWorkerW;

    // unused method
    auto MeasureText(const std::wstring &text) const;
};
