#include "LayeredRender.h"

#include "GlowTextRenderer.h"
#include "logging.h"
#include "strings.h"

#include <algorithm>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "WindowsCodecs.lib")

#define IDT_REFRESH 0x1001

using Microsoft::WRL::ComPtr;

inline std::wstring NowString()
{
    std::time_t t = std::time(nullptr);
    wchar_t buf[100];
    wcsftime(buf, 100, L"%m-%d %X", std::localtime(&t));
    return buf;
}

int LayeredRender::AddToast(const std::wstring &title, const std::wstring &text, const Image &im, const std::wstring &link)
{
    TIMEIT_START(AddToast);

    float boxHeight = marginTop;
    float titleHeight = 0.f;
    float textHeight = 0.f, lineHeight = 0.f;
    float imageHeight = 0.f;

    // TODO: better typesetting for timestamp
    ComPtr<IDWriteTextLayout> timeLayout;
    {
        auto time = NowString();
        ThrowIfFailed(
            dwFactory->CreateTextLayout(
                time.c_str(), (UINT32)time.size(),
                dwTimeStampFormat.Get(),
                boxMaxWidth - 8.f,
                boxMaxHeight - boxHeight - marginBottom,
                &timeLayout
            )
        );
        TIMEIT(AddToast, "Init Time string");
    }

    ComPtr<IDWriteTextLayout> titleLayout;
    if (!title.empty()) {
        ThrowIfFailed(
            dwFactory->CreateTextLayout(
                title.c_str(), (UINT32)title.size(),
                dwTitleFormat.Get(),
                boxMaxWidth - marginLeft * 2,
                boxMaxHeight - boxHeight - marginBottom,
                &titleLayout
            )
        );

        DWRITE_TEXT_METRICS metrics;
        ThrowIfFailed(
            titleLayout->GetMetrics(&metrics)
        );

        titleHeight = boxHeight;
        boxHeight += metrics.height * 1.25f;
        TIMEIT(AddToast, "Init Title");
    }

    ComPtr<IDWriteTextLayout> textLayout;
    if (!text.empty()) {
        ThrowIfFailed(
            dwFactory->CreateTextLayout(
                text.c_str(), (UINT32)text.size(),
                dwTextFormat.Get(),
                boxMaxWidth - marginLeft * 2,
                boxMaxHeight - boxHeight - marginBottom,
                &textLayout
            )
        );

        DWRITE_TEXT_METRICS metrics;
        ThrowIfFailed(
            textLayout->GetMetrics(&metrics)
        );

        textHeight = boxHeight;
        boxHeight += metrics.height;

        UINT32 lineCount = metrics.lineCount;
        std::vector<DWRITE_LINE_METRICS> lineMetrics;
        lineMetrics.resize(lineCount);
        ThrowIfFailed(textLayout->GetLineMetrics(lineMetrics.data(), lineCount, &lineCount));
        lineHeight = lineMetrics.back().height;
        TIMEIT(AddToast, "Init Text");
    }

    ComPtr<ID2D1Bitmap> imageBitmap;
    if (im) {
        assert(im.ch == 4); // only except BGRA uint8 pixel
        assert(im.width <= boxMaxWidth); // image ignore box margin
        assert(im.height <= boxMaxHeight - boxHeight - marginBottom);

        ThrowIfFailed(
            d2dRTContext->CreateBitmap(
                { (UINT32)im.width, (UINT32)im.height },
                im.data, im.ch * im.width,
                {
                    {
                        im.order == Image::BGR
                            ? DXGI_FORMAT_B8G8R8A8_UNORM
                            : DXGI_FORMAT_R8G8B8A8_UNORM,
                        D2D1_ALPHA_MODE_PREMULTIPLIED
                }, 0.f, 0.f },
                &imageBitmap
            )
        );
        // assume normal image dose not have pre alpha multiply
        d2dEffectPremultiply->SetInput(0, imageBitmap.Get());

        imageHeight = lineHeight + boxHeight;
        boxHeight += lineHeight + im.height;

        TIMEIT(AddToast, "Init Image");
    }

    boxHeight += marginBottom;

    DBG << "BOX: " << boxMaxWidth << "x" << boxHeight
        << " Title: " << titleHeight
        << " Text: " << textHeight << "(" << lineHeight << ")"
        << " Image: " << imageHeight;

    // ---------+-----------------------------------------
    // |        |
    // |       posY
    // |        |
    // +--posX--+--------------------------------------+
    // |        |               marginTop              |
    // |        |                                      |
    // |        | marginL |--texts--|--text--| marginR |
    // |        |                                      |
    // |        | <-------------box width------------> |
    //

    auto d2dDraw = [&](IDWriteTextRenderer *textRenderer, ID2D1Bitmap *bmp) {
        d2dRTContext->BeginDraw();

        TIMEITF(AddToast, "D2D Background", ([&] {
            d2dRTContext->SetTransform(D2D1::IdentityMatrix());
            d2dRTContext->Clear(D2D1::ColorF(D2D1::ColorF::Black, 0.f));
            d2dRTContext->FillRoundedRectangle(
                {
                    D2D1_RECT_F {
                        0, 0,
                        boxMaxWidth, boxHeight
                    },
                    7.f, 7.f
                },
                d2dBoxBrush.Get()
            );
                                             }));

        if (timeLayout) {
            d2dRTContext->DrawTextLayout(
                D2D1_POINT_2F{
                    0.f, 3.5f,
                },
                timeLayout.Get(), d2dTimeBrush.Get(),
                D2D1_DRAW_TEXT_OPTIONS_NONE
                );
            TIMEIT(AddToast, "D2D Draw Timestamp");
        }

        if (titleLayout) {
            titleLayout->Draw(
                d2dRTContext.Get(),
                textRenderer,
                marginLeft, titleHeight
            );
            TIMEIT(AddToast, "D2D Draw Title");
        }

        if (textLayout) {
            textLayout->Draw(
                d2dRTContext.Get(),
                textRenderer,
                marginLeft, textHeight
            );
            TIMEIT(AddToast, "D2D Draw Text");
        }

        if (imageBitmap) {
            d2dRTContext->DrawImage(
                d2dEffectPremultiply.Get(),
                D2D1_POINT_2F{
                    (boxMaxWidth - im.width) / 2, // center of the box
                    imageHeight
                },
            {
                0.f, 0.f,
                (FLOAT)im.width, (FLOAT)im.height
            }
            );
            TIMEIT(AddToast, "D2D Draw Image");
        }

        ThrowIfFailed(d2dRTContext->EndDraw());
        TIMEIT(AddToast, "D2D EndDraw");

        auto dest = D2D1_POINT_2U{ 0, 0 };
        auto src = D2D1_RECT_U{ 0, 0, (UINT32)boxMaxWidth, (UINT32)boxHeight };

        ThrowIfFailed(
            bmp->CopyFromRenderTarget(
                &dest, d2dRTContext.Get(), &src
            )
        );
    };

    ComPtr<ID2D1Bitmap> normalBmp, highlightBmp;
    {
        ThrowIfFailed(
            d2dContext->CreateBitmap(
                D2D1_SIZE_U{ (UINT32)boxMaxWidth, (UINT32)boxHeight },
                D2D1_BITMAP_PROPERTIES{ pixelFormat, 0.f, 0.f },
                &normalBmp)
        );

        ThrowIfFailed(
            d2dContext->CreateBitmap(
                D2D1_SIZE_U{ (UINT32)boxMaxWidth, (UINT32)boxHeight },
                D2D1_BITMAP_PROPERTIES{ pixelFormat, 0.f, 0.f },
                &highlightBmp)
        );
    }

    d2dDraw(dwTextRenderer.Get(), normalBmp.Get());
    d2dDraw(dwHighlightedRenderer.Get(), highlightBmp.Get());

    int toastId = -1;

    TIMEITF(AddToast, "Add to list", ([&] {
        std::lock_guard _(renderLock);
        toastId = ++_counter;
        toastList.emplace_front(
            Toast{
                (UINT32)boxMaxWidth, (UINT32)boxHeight,
                timer.ms(),
                normalBmp, highlightBmp, toastId, link
            });
        invalidated = true;
    }));

    DBG << "Toast added";

    TIMEIT_END(AddToast);
    return toastId;
}

LayeredRender::LayeredRender(int width, int height, const std::wstring &name) :
    WindowRender(width, height, name), _ready(false)
{
    timer.start();
}

HRESULT LayeredRender::CreateTextFormat(
    const std::wstring &fontFamily, float fontSize,
    IDWriteTextFormat **ppTextFormat) const
{
    HRESULT hr = dwFactory->CreateTextFormat(
        fontFamily.c_str(),
        nullptr,
        DWRITE_FONT_WEIGHT_REGULAR,
        DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        fontSize * 96 / 72, L"zh-cn", ppTextFormat
    );

    if (SUCCEEDED(hr)) {
        hr = (*ppTextFormat)->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    }

    if (SUCCEEDED(hr)) {
        hr = (*ppTextFormat)->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    }

    if (SUCCEEDED(hr)) {
        hr = (*ppTextFormat)->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_PROPORTIONAL, 1.0f, 1.0f);
    }

    return hr;
}

void LayeredRender::CreateDeviceIndependentResources()
{
    // Create DXGI factory
    ThrowIfFailed(
        CreateDXGIFactory2(0, __uuidof(IDXGIFactory2), &dxFactory)
    );

    // Create Direct2D factory.
    ThrowIfFailed(
        D2D1CreateFactory<ID2D1Factory1>(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2dFactory)
    );

    // Create a shared DirectWrite factory.
    ThrowIfFailed(
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory2), &dwFactory)
    );

    // Create text format for DirectWrite to render text
    ThrowIfFailed(
        CreateTextFormat(titleFontName.c_str(), titleFontSize, &dwTitleFormat)
    );

    ThrowIfFailed(
        CreateTextFormat(emojiFontName.c_str(), titleFontSize, &dwTitleEmojiFormat)
    );

    ThrowIfFailed(
        CreateTextFormat(textFontName.c_str(), textFontSize, &dwTextFormat)
    );

    ThrowIfFailed(
        CreateTextFormat(emojiFontName.c_str(), textFontSize, &dwEmojiFormat)
    );

    ThrowIfFailed(
        CreateTextFormat(timeFontName.c_str(), timeFontSize, &dwTimeStampFormat)
    );

    dwTimeStampFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    dwTimeStampFormat->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_PROPORTIONAL, 1.2f, 1.0f);
}

void LayeredRender::CreateDeviceResources()
{
    RECT rect = {};
    GetClientRect(Win32Application::GetHwnd(), &rect);

    const UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

    D3D_FEATURE_LEVEL featureLevels[] =
    {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
        D3D_FEATURE_LEVEL_9_3,
        D3D_FEATURE_LEVEL_9_2,
        D3D_FEATURE_LEVEL_9_1
    };

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;

    HRESULT hr = D3D11CreateDevice(
        nullptr,                    // Specify nullptr to use the default adapter.
        D3D_DRIVER_TYPE_HARDWARE,   // Create a device using the hardware graphics driver.
        0,                          // Should be 0 unless the driver is D3D_DRIVER_TYPE_SOFTWARE.
        creationFlags,              // Set debug and Direct2D compatibility flags.
        featureLevels,              // List of feature levels this app can support.
        ARRAYSIZE(featureLevels),   // Size of the list above.
        D3D11_SDK_VERSION,          // Always set this to D3D11_SDK_VERSION for Windows Runtime apps.
        &device,                    // Returns the Direct3D device created.
        &d3dFeatureLevel,         // Returns feature level of device created.
        &context                    // Returns the device immediate context.
    );

    if (FAILED(hr)) {
        // If the initialization fails, fall back to the WARP device.
        // For more information on WARP, see:
        // http://go.microsoft.com/fwlink/?LinkId=286690
        ThrowIfFailed(
            D3D11CreateDevice(
                nullptr,
                D3D_DRIVER_TYPE_WARP, // Create a WARP device instead of a hardware device.
                0,
                creationFlags,
                featureLevels,
                ARRAYSIZE(featureLevels),
                D3D11_SDK_VERSION,
                &device,
                &d3dFeatureLevel,
                &context
            )
        );
    }

    // Store pointers to the Direct3D 11.1 API device and immediate context.
    {
        ThrowIfFailed(
            device.As(&d3dDevice)
        );

        ThrowIfFailed(
            context.As(&d3dContext)
        );
    }

    // Get DXGI device for DirectComposition and Direct2D creation
    ComPtr<IDXGIDevice3> dxgiDevice;
    ThrowIfFailed(
        d3dDevice.As(&dxgiDevice)
    );

    // Create DirectComposition Resources
    {
        // Create Swap Chain for Composition
        DXGI_SWAP_CHAIN_DESC1 description = {};
        description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        description.BufferCount = 2;
        description.SampleDesc.Count = 1;
        description.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        description.Width = rect.right - rect.left;
        description.Height = rect.bottom - rect.top;

        ThrowIfFailed(
            dxFactory->CreateSwapChainForComposition(
                dxgiDevice.Get(), &description, nullptr, &dxSwapChain
            )
        );

        ThrowIfFailed(
            DCompositionCreateDevice(
                dxgiDevice.Get(),
                __uuidof(IDCompositionDevice),
                &dcompDevice)
        );

        // Top most window
        ThrowIfFailed(
            dcompDevice->CreateTargetForHwnd(
                Win32Application::GetHwnd(), true, &dcompTarget)
        );

        ThrowIfFailed(
            dcompDevice->CreateVisual(&dcompVisual)
        );

        // Commit the visual to composition engine
        ThrowIfFailed(
            dcompVisual->SetContent(dxSwapChain.Get())
        );

        ThrowIfFailed(
            dcompTarget->SetRoot(dcompVisual.Get())
        );

        ThrowIfFailed(
            dcompDevice->Commit()
        );
    }

    // Create Direct2D resources
    {
        ThrowIfFailed(
            d2dFactory->CreateDevice(dxgiDevice.Get(), &d2dDevice)
        );

        ThrowIfFailed(
            d2dDevice->CreateDeviceContext(
                D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                &d2dContext
            )
        );

        // Retrieve the swap chain's back buffer
        ComPtr<IDXGISurface2> surface;
        ThrowIfFailed(
            dxSwapChain->GetBuffer(0, __uuidof(IDXGISurface2), &surface)
        );

        // Create a Direct2D bitmap that points to the swap chain surface
        ThrowIfFailed(
            d2dContext->CreateBitmapFromDxgiSurface(
                surface.Get(),
                D2D1_BITMAP_PROPERTIES1{
                    pixelFormat, 0.f, 0.f,
                    D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW
                },
                &d2dTargetBitmap)
        );

        ThrowIfFailed(
            d2dContext->CreateBitmap(
                D2D1_SIZE_U{
                    (UINT32)rect.right - rect.left,
                    (UINT32)rect.bottom - rect.top
                },
                D2D1_BITMAP_PROPERTIES{ pixelFormat, 0.f, 0.f },
                &d2dEmptyBitmap)
        );

        auto dest = D2D1_POINT_2U{ (UINT32)0, (UINT32)0 };
        auto size = d2dEmptyBitmap->GetPixelSize();
        auto src = D2D1_RECT_U{ 0, 0, size.width, size.height };

        ThrowIfFailed(
            d2dEmptyBitmap->CopyFromBitmap(
                &dest, d2dTargetBitmap.Get(), &src
            )
        );
    }

    // Create internal back buffer
    {
        ThrowIfFailed(
            d2dContext->CreateCompatibleRenderTarget(
                D2D1_SIZE_F{ boxMaxWidth, (float)rect.bottom - rect.top },
                D2D1_SIZE_U{ (UINT32)boxMaxWidth, (UINT32)rect.bottom - rect.top },
                pixelFormat, D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS_NONE,
                &d2dCacheTarget
            )
        );

        ThrowIfFailed(
            d2dCacheTarget.As(&d2dRTContext)
        );

        ThrowIfFailed(
            d2dRTContext->CreateSolidColorBrush(
                D2D1::ColorF(D2D1::ColorF::White), &d2dWhiteBrush
            )
        );

        ThrowIfFailed(
            d2dRTContext->CreateSolidColorBrush(
                D2D1::ColorF(D2D1::ColorF::White, 0.85f), &d2dTimeBrush
            )
        );

        ThrowIfFailed(
            d2dRTContext->CreateSolidColorBrush(
                D2D1::ColorF(D2D1::ColorF::Black, 0.75f), &d2dBoxBrush
            )
        );

        ThrowIfFailed(
            d2dRTContext->CreateEffect(CLSID_D2D1Premultiply, &d2dEffectPremultiply)
        );

        dwTextRenderer = new (std::nothrow) GlowTextRenderer(
            d2dFactory.Get(), d2dRTContext.Get(),
            glowColor, glowWidth, glowStep
        );

        dwHighlightedRenderer = new (std::nothrow) GlowTextRenderer(
            d2dFactory.Get(), d2dRTContext.Get(),
            highlightColor, glowWidth, glowStep
        );
    }
}

void LayeredRender::OnInit()
{
    DBG << "Setting Window location and size";
    {
        RECT rect = {};
        GetClientRect(Win32Application::GetHwnd(), &rect);

        boxMaxHeight = (float)(rect.bottom - rect.top);

        if (isnan(posX)) {
            posX = rect.right - boxMaxWidth - 50.f;
        }

        if (isnan(posY)) {
            posY = 50.f;
        }
    }

    DBG << "Finding Desktop WorkerW";
    {
        HWND hDestop = GetDesktopWindow();
        hWorkerW = NULL;
        HWND hShellViewWin = NULL;
        do {
            hWorkerW = FindWindowEx(hDestop, hWorkerW, L"WorkerW", nullptr);
            hShellViewWin = FindWindowEx(hWorkerW, NULL, L"SHELLDLL_DefView", nullptr);
        } while (!IsWindow(hShellViewWin) && IsWindow(hWorkerW));

        DBG << "Found Desktop WorkerW: " << hWorkerW;
    }

    DBG << "Setting up timer...";
    SetTimer(Win32Application::GetHwnd(), IDT_REFRESH, 100, nullptr);

    DBG << "Creating DirectX resources...";
    CreateDeviceIndependentResources();
    CreateDeviceResources();

    _stop = false;

    OnUpdate();

    std::wstring titleStr = L"测试标题";

    std::wstring wstr = //L"等";
        L"等距更纱黑体\n\t测试中"
        L"123456789012345678"
        L"abcdefghijklmnopqrst"
        L"\xd83e\xdc00\xD83D\xDE80"
        L"abcde\n";

    AddToast(titleStr, wstr);

    DBG << "Start to draw first frame";
    PostMessage(Win32Application::GetHwnd(), WM_PAINT, 0, 0);

    _ready = true;
}

bool LayeredRender::OnUpdate()
{
    if (!_ready) {
        return false;
    }

    if (invalidated) {
        return true;
    }

    std::lock_guard _(renderLock);

    POINT pt;
    GetCursorPos(&pt);

    int hover = -1;
    float x = posX, y = posY;
    for (const auto &toast : toastList) {
        if (x <= pt.x && pt.x <= x + toast.width &&
            y <= pt.y && pt.y <= y + toast.height) {
            hover = toast.id;
            break;
        }
        y += toast.height + marginBottom;
    }

    if (mouseHover != hover) {
        DBG << (hover ? "Mouse Entered" : "Mouse Leave");
        invalidated = true;
        mouseHover = hover;
    }

    return invalidated;
}

void LayeredRender::OnRender()
{
    TIMEIT_START(OnRender);

    POINT pt;
    GetCursorPos(&pt);

    TIMEITF(OnRender, "D2D to SwapChain backbuffer", ([&] {
        std::lock_guard _(renderLock);

        {
            // Clear buffer
            auto dest = D2D1_POINT_2U{ (UINT32)0, (UINT32)0 };
            auto size = d2dEmptyBitmap->GetPixelSize();
            auto src = D2D1_RECT_U{ 0, 0, size.width, size.height };

            d2dTargetBitmap->CopyFromBitmap(
                &dest, d2dEmptyBitmap.Get(), &src
            );
        }

        {
            float x = posX, y = posY;
            for (const auto &toast : toastList) {
                ID2D1Bitmap *bmp = toast.normal.Get();
                if (x <= pt.x && pt.x <= x + toast.width &&
                    y <= pt.y && pt.y <= y + toast.height) {
                    bmp = toast.highlight.Get();
                }

                auto dest = D2D1_POINT_2U{ (UINT32)x, (UINT32)y };
                auto src = D2D1_RECT_U{ 0, 0, toast.width, toast.height };

                d2dTargetBitmap->CopyFromBitmap(
                    &dest, bmp, &src
                );

                y += toast.height + marginBottom;

                if (y > boxMaxHeight) {
                    break;
                }
            }
        }

        invalidated = false;
                                                      }));

    TIMEITF(OnRender, "SwapChain Present", ([&] {
        ThrowIfFailed(dxSwapChain->Present(1, 0));
                                            }));

    TIMEIT_END(OnRender);
}

void LayeredRender::OnDestroy()
{
    KillTimer(Win32Application::GetHwnd(), IDT_REFRESH);

    _ready = false;
    _stop = true;
}

void LayeredRender::OnMouse(MouseEvent event, int xPos, int yPos, bool shifted, bool ctrled)
{
    // only handle left mouse click event when the focus of user is on desktop
    if (GetForegroundWindow() != hWorkerW || event != LButtonDown) {
        return;
    }

    if (!shifted && !ctrled) {
        return;
    }

    TIMEIT_START(OnMouse);

    std::lock_guard _(renderLock);

    float x = posX, y = posY;
    for (auto it = toastList.begin(); it != toastList.end(); ++it) {
        const auto &toast = *it;
        if (!(x <= xPos && xPos <= x + toast.width &&
              y <= yPos && yPos <= y + toast.height)) {
            y += toast.height + marginBottom;
            continue;
        }

        if (shifted) {
            // close the message
            toastList.erase(it);
        } else if (ctrled) {
            std::thread([&] {
                ShellExecute(NULL, nullptr, toast.link.c_str(),
                             nullptr, nullptr, SW_SHOWNORMAL);
            }).detach();
        }

        invalidated = true;
        break;
    }

    TIMEIT_END(OnMouse);
}

void LayeredRender::OnTimer(int id)
{
    // refresh window render
    PostMessage(Win32Application::GetHwnd(), WM_PAINT, 0, 0);

    int style = GetWindowLong(Win32Application::GetHwnd(), GWL_EXSTYLE);
    if (style & WS_EX_TOPMOST && topmostTime < timer.ms()) {
        SetWindowPos(Win32Application::GetHwnd(), HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    }
}

void LayeredRender::SetTopMost(float sec)
{
    topmostTime = timer.ms() + sec * 1000;
    SetWindowPos(Win32Application::GetHwnd(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
}

auto LayeredRender::MeasureText(const std::wstring &text) const
{
    wstring_char chars(text);

    float lineHeight = 0;

    std::vector<RenderCommand> commands;
    for (const auto &[c, emoji] : chars.items()) {
        RenderCommand cmd{ RenderCommand::Text };

        if (c[0] == L'\n') {
            commands.emplace_back(RenderCommand::TextNewline);
            continue;
        }

        // we don't support them, skip
        if (c[0] == L'\r' || c[0] == L'\b') {
            continue;
        }

        ThrowIfFailed(
            dwFactory->CreateTextLayout(
                c, (UINT32)wcslen(c),
                emoji ? dwEmojiFormat.Get() : dwTextFormat.Get(),
                boxMaxWidth, boxMaxHeight,
                &cmd.textLayout
            )
        );

        DWRITE_TEXT_METRICS metrics;
        ThrowIfFailed(cmd.textLayout->GetMetrics(&metrics));

        // make sure line height could accommodate that line
        // could use IDWriteTextFormat to normailze line height
        lineHeight = lineHeight < metrics.height ? metrics.height : lineHeight;

        cmd.width = metrics.widthIncludingTrailingWhitespace;
        cmd.height = metrics.height;

        commands.emplace_back(cmd);

        std::string str;
        if (c[0] == L'\n' || c[0] == L'\r') {
            str = "\\n";
        } else if (emoji) {
            str = "emoji";
        } else {
            str = std::wtoa(c);
        }

        DBG << std::setprecision(2) << std::fixed
            << "Text: " << str
            << " SIZE: " << metrics.widthIncludingTrailingWhitespace << "x" << metrics.height;
    }

    return std::tuple(lineHeight, commands);
}
