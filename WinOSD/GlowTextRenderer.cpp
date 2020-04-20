#include "GlowTextRenderer.h"

#include "logging.h"

using namespace Microsoft::WRL;

// The constructor stores the Direct2D factory and device context
// and creates resources the renderer will use.
GlowTextRenderer::GlowTextRenderer(
    ComPtr<ID2D1Factory> d2dFactory,
    ComPtr<ID2D1DeviceContext4> d2dDeviceContext,
    D2D1::ColorF color, float glowWidth, float glowStep
) :
    m_refCount(0),
    m_d2dFactory(d2dFactory),
    m_d2dDeviceContext(d2dDeviceContext),
    m_glowWidth(glowWidth),
    m_glowStep(glowStep)
{
    ThrowIfFailed(
        DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory4),
            &m_dwriteFactory
        )
    );

    ThrowIfFailed(
        m_d2dFactory->CreatePathGeometry(&m_pathGeometry)
    );

    ThrowIfFailed(
        m_d2dFactory->CreateStrokeStyle(
            D2D1::StrokeStyleProperties(
                D2D1_CAP_STYLE_ROUND,
                D2D1_CAP_STYLE_ROUND,
                D2D1_CAP_STYLE_ROUND,
                D2D1_LINE_JOIN_ROUND,
                1.f, D2D1_DASH_STYLE_SOLID, 0.f
            ), nullptr, 0,
            &m_strokeStyle
        )
    );

    ID2D1SimplifiedGeometrySink **sinkPrt = &m_geometrySink;
    ThrowIfFailed(
        m_pathGeometry->Open((ID2D1GeometrySink **)sinkPrt)
    );

    ThrowIfFailed(
        m_d2dDeviceContext->CreateSolidColorBrush(
            D2D1::ColorF(D2D1::ColorF::White),
            &m_outlineBrush
        )
    );

    ThrowIfFailed(
        m_d2dDeviceContext->CreateSolidColorBrush(
            color,
            &m_glowBrush
        )
    );

    ThrowIfFailed(
        m_d2dDeviceContext->CreateSolidColorBrush(
            D2D1::ColorF(D2D1::ColorF::Black),
            &m_tempBrush
        )
    );
}

// Decomposes the received glyph run into smaller color glyph runs
// using IDWriteFactory4::TranslateColorGlyphRun. Depending on the
// type of each color run, the renderer uses Direct2D to draw the
// outlines, SVG content, or bitmap content.
HRESULT GlowTextRenderer::DrawGlyphRun(
    _In_opt_ void *clientDrawingContext,
    FLOAT baselineOriginX,
    FLOAT baselineOriginY,
    DWRITE_MEASURING_MODE measuringMode,
    _In_ DWRITE_GLYPH_RUN const *glyphRun,
    _In_ DWRITE_GLYPH_RUN_DESCRIPTION const *glyphRunDescription,
    IUnknown *clientDrawingEffect
)
{
    // The list of glyph image formats this renderer is prepared to support.
    const DWRITE_GLYPH_IMAGE_FORMATS supportedFormats =
        DWRITE_GLYPH_IMAGE_FORMATS_TRUETYPE |
        DWRITE_GLYPH_IMAGE_FORMATS_CFF |
        DWRITE_GLYPH_IMAGE_FORMATS_COLR |
        DWRITE_GLYPH_IMAGE_FORMATS_SVG |
        DWRITE_GLYPH_IMAGE_FORMATS_PNG |
        DWRITE_GLYPH_IMAGE_FORMATS_JPEG |
        DWRITE_GLYPH_IMAGE_FORMATS_TIFF |
        DWRITE_GLYPH_IMAGE_FORMATS_PREMULTIPLIED_B8G8R8A8;

    // Determine whether there are any color glyph runs within glyphRun. If
    // there are, glyphRunEnumerator can be used to iterate through them.
    ComPtr<IDWriteColorGlyphRunEnumerator1> glyphRunEnumerator;
    HRESULT hr = m_dwriteFactory->TranslateColorGlyphRun(
        D2D1::Point2F(baselineOriginX, baselineOriginY),
        glyphRun,
        glyphRunDescription,
        supportedFormats,
        measuringMode,
        nullptr,
        0,
        &glyphRunEnumerator
    );

    if (hr == DWRITE_E_NOCOLOR) {
        // Create the path geometry.
        ComPtr<ID2D1PathGeometry> pathGeometry;
        ThrowIfFailed(
            m_d2dFactory->CreatePathGeometry(&pathGeometry)
        );

        // Write to the path geometry using the geometry sink.
        ComPtr<ID2D1GeometrySink> sink;
        ThrowIfFailed(
            pathGeometry->Open(&sink)
        );

        ThrowIfFailed(
            glyphRun->fontFace->GetGlyphRunOutline(
                glyphRun->fontEmSize,
                glyphRun->glyphIndices,
                glyphRun->glyphAdvances,
                glyphRun->glyphOffsets,
                glyphRun->glyphCount,
                glyphRun->isSideways,
                glyphRun->bidiLevel % 2,
                sink.Get()
            )
        );

        ThrowIfFailed(sink->Close());

        // Initialize a matrix to translate the origin of the glyph run.
        D2D1::Matrix3x2F const matrix = D2D1::Matrix3x2F(
            1.0f, 0.0f,
            0.0f, 1.0f,
            baselineOriginX, baselineOriginY
        );

        // Create the transformed geometry
        ComPtr<ID2D1TransformedGeometry> transformedGeometry;
        ThrowIfFailed(
            m_d2dFactory->CreateTransformedGeometry(
                pathGeometry.Get(),
                &matrix,
                &transformedGeometry
            )
        );

        // Draw the outline of the glyph run
        for (float i = 1.f; i <= m_glowWidth; i += 4.f) {
            m_d2dDeviceContext->DrawGeometry(
                transformedGeometry.Get(),
                m_glowBrush.Get(),
                i, m_strokeStyle.Get()
            );
        }

        // Fill in the glyph run
        m_d2dDeviceContext->FillGeometry(
            transformedGeometry.Get(),
            m_outlineBrush.Get()
        );

    } else {
        ThrowIfFailed(hr);

        // Complex case: the run has one or more color runs within it. Iterate
        // over the sub-runs and draw them, depending on their format.
        for (;;) {
            BOOL haveRun = FALSE;
            ThrowIfFailed(glyphRunEnumerator->MoveNext(&haveRun));
            if (!haveRun)
                break;

            DWRITE_COLOR_GLYPH_RUN1 const *colorRun;
            ThrowIfFailed(glyphRunEnumerator->GetCurrentRun(&colorRun));

            D2D1_POINT_2F currentBaselineOrigin = D2D1::Point2F(
                colorRun->baselineOriginX,
                colorRun->baselineOriginY
            );

            switch (colorRun->glyphImageFormat) {
            case DWRITE_GLYPH_IMAGE_FORMATS_PNG:
            case DWRITE_GLYPH_IMAGE_FORMATS_JPEG:
            case DWRITE_GLYPH_IMAGE_FORMATS_TIFF:
            case DWRITE_GLYPH_IMAGE_FORMATS_PREMULTIPLIED_B8G8R8A8:
            {
                // This run is bitmap glyphs. Use Direct2D to draw them.
                m_d2dDeviceContext->DrawColorBitmapGlyphRun(
                    colorRun->glyphImageFormat,
                    currentBaselineOrigin,
                    &colorRun->glyphRun,
                    measuringMode
                );
            }
            break;

            case DWRITE_GLYPH_IMAGE_FORMATS_SVG:
            {
                // This run is SVG glyphs. Use Direct2D to draw them.
                m_d2dDeviceContext->DrawSvgGlyphRun(
                    currentBaselineOrigin,
                    &colorRun->glyphRun,
                    m_outlineBrush.Get(),
                    nullptr,                // svgGlyphStyle
                    0,                      // colorPaletteIndex
                    measuringMode
                );
            }
            break;

            case DWRITE_GLYPH_IMAGE_FORMATS_TRUETYPE:
            case DWRITE_GLYPH_IMAGE_FORMATS_CFF:
            case DWRITE_GLYPH_IMAGE_FORMATS_COLR:
            default:
            {
                // This run is solid-color outlines, either from non-color
                // glyphs or from COLR glyph layers. Use Direct2D to draw them.

                ComPtr<ID2D1Brush> layerBrush;
                if (colorRun->paletteIndex == 0xFFFF) {
                    // This run uses the current text color.
                    layerBrush = m_outlineBrush;
                } else {
                    // This run specifies its own color.
                    m_tempBrush->SetColor(colorRun->runColor);
                    layerBrush = m_tempBrush;
                }

                // Draw the run with the selected color.
                m_d2dDeviceContext->DrawGlyphRun(
                    currentBaselineOrigin,
                    &colorRun->glyphRun,
                    colorRun->glyphRunDescription,
                    layerBrush.Get(),
                    measuringMode
                );
            }
            break;
            }
        }
    }

    return hr;
}

IFACEMETHODIMP GlowTextRenderer::DrawUnderline(
    _In_opt_ void *clientDrawingContext,
    FLOAT baselineOriginX,
    FLOAT baselineOriginY,
    _In_ DWRITE_UNDERLINE const *underline,
    IUnknown *clientDrawingEffect
)
{
    // Not implemented
    return E_NOTIMPL;
}

IFACEMETHODIMP GlowTextRenderer::DrawStrikethrough(
    _In_opt_ void *clientDrawingContext,
    FLOAT baselineOriginX,
    FLOAT baselineOriginY,
    _In_ DWRITE_STRIKETHROUGH const *strikethrough,
    IUnknown *clientDrawingEffect
)
{
    // Not implemented
    return E_NOTIMPL;
}

IFACEMETHODIMP GlowTextRenderer::DrawInlineObject(
    _In_opt_ void *clientDrawingContext,
    FLOAT originX,
    FLOAT originY,
    IDWriteInlineObject *inlineObject,
    BOOL isSideways,
    BOOL isRightToLeft,
    IUnknown *clientDrawingEffect
)
{
    // Not implemented
    return E_NOTIMPL;
}

IFACEMETHODIMP_(unsigned long) GlowTextRenderer::AddRef()
{
    return InterlockedIncrement(&m_refCount);
}

IFACEMETHODIMP_(unsigned long) GlowTextRenderer::Release()
{
    unsigned long newCount = InterlockedDecrement(&m_refCount);
    if (newCount == 0) {
        delete this;
        return 0;
    }

    return newCount;
}

IFACEMETHODIMP GlowTextRenderer::IsPixelSnappingDisabled(
    _In_opt_ void *clientDrawingContext,
    _Out_ BOOL *isDisabled
)
{
    *isDisabled = FALSE;
    return S_OK;
}

IFACEMETHODIMP GlowTextRenderer::GetCurrentTransform(
    _In_opt_ void *clientDrawingContext,
    _Out_ DWRITE_MATRIX *transform
)
{
    // forward the render target's transform
    m_d2dDeviceContext->GetTransform(reinterpret_cast<D2D1_MATRIX_3X2_F *>(transform));
    return S_OK;
}

IFACEMETHODIMP GlowTextRenderer::GetPixelsPerDip(
    _In_opt_ void *clientDrawingContext,
    _Out_ FLOAT *pixelsPerDip
)
{
    float x, yUnused;

    m_d2dDeviceContext.Get()->GetDpi(&x, &yUnused);
    *pixelsPerDip = x / 96.0f;

    return S_OK;
}

IFACEMETHODIMP GlowTextRenderer::QueryInterface(
    IID const &riid,
    void **ppvObject
)
{
    if (__uuidof(IDWriteTextRenderer) == riid) {
        *ppvObject = this;
    } else if (__uuidof(IDWritePixelSnapping) == riid) {
        *ppvObject = this;
    } else if (__uuidof(IUnknown) == riid) {
        *ppvObject = this;
    } else {
        *ppvObject = nullptr;
        return E_FAIL;
    }

    this->AddRef();

    return S_OK;
}
