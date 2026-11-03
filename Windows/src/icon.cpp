#include "icon.h"
#include "theme.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace swc {
namespace {

// Geometrias em coordenadas de um quadrado de 108 (as mesmas do vetor do app Android).
struct IconGeometry {
    ID2D1Factory* factory = nullptr;
    ComPtr<ID2D1PathGeometry> body;   // controle com o J de JoyHub e botões recortados
    ComPtr<ID2D1PathGeometry> hub;    // três conexões convergem no controle
    ComPtr<ID2D1PathGeometry> nodes;
    ComPtr<ID2D1StrokeStyle> round;
};

D2D1_POINT_2F pt(float x, float y) { return D2D1::Point2F(x, y); }

void addCircle(ID2D1GeometrySink* s, float cx, float cy, float r) {
    s->BeginFigure(pt(cx - r, cy), D2D1_FIGURE_BEGIN_FILLED);
    s->AddArc(D2D1::ArcSegment(pt(cx + r, cy), D2D1::SizeF(r, r), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE,
                               D2D1_ARC_SIZE_SMALL));
    s->AddArc(D2D1::ArcSegment(pt(cx - r, cy), D2D1::SizeF(r, r), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE,
                               D2D1_ARC_SIZE_SMALL));
    s->EndFigure(D2D1_FIGURE_END_CLOSED);
}

IconGeometry& geometry(ID2D1Factory* f) {
    static IconGeometry g;
    if (g.factory == f && g.body) return g;
    g = {};
    g.factory = f;

    ComPtr<ID2D1GeometrySink> s;
    if (SUCCEEDED(f->CreatePathGeometry(&g.body)) && SUCCEEDED(g.body->Open(&s))) {
        s->SetFillMode(D2D1_FILL_MODE_ALTERNATE);
        s->BeginFigure(pt(38, 46), D2D1_FIGURE_BEGIN_FILLED);
        s->AddLine(pt(70, 46));
        s->AddQuadraticBezier(D2D1::QuadraticBezierSegment(pt(77, 46), pt(79, 55)));
        s->AddLine(pt(83, 70));
        s->AddQuadraticBezier(D2D1::QuadraticBezierSegment(pt(85, 80), pt(77, 80)));
        s->AddQuadraticBezier(D2D1::QuadraticBezierSegment(pt(73, 80), pt(66, 71)));
        s->AddLine(pt(42, 71));
        s->AddQuadraticBezier(D2D1::QuadraticBezierSegment(pt(35, 80), pt(31, 80)));
        s->AddQuadraticBezier(D2D1::QuadraticBezierSegment(pt(23, 80), pt(25, 70)));
        s->AddLine(pt(29, 55));
        s->AddQuadraticBezier(D2D1::QuadraticBezierSegment(pt(31, 46), pt(38, 46)));
        s->EndFigure(D2D1_FIGURE_END_CLOSED);

        s->BeginFigure(pt(48, 52), D2D1_FIGURE_BEGIN_FILLED);
        s->AddLine(pt(48, 62));
        s->AddQuadraticBezier(D2D1::QuadraticBezierSegment(pt(48, 69), pt(41, 69)));
        s->AddQuadraticBezier(D2D1::QuadraticBezierSegment(pt(34, 69), pt(34, 62)));
        s->AddLine(pt(38, 62));
        s->AddQuadraticBezier(D2D1::QuadraticBezierSegment(pt(38, 65), pt(41, 65)));
        s->AddQuadraticBezier(D2D1::QuadraticBezierSegment(pt(44, 65), pt(44, 62)));
        s->AddLine(pt(44, 52));
        s->EndFigure(D2D1_FIGURE_END_CLOSED);

        addCircle(s.Get(), 68.5f, 55, 2.5f);
        addCircle(s.Get(), 74.5f, 61, 2.5f);
        s->Close();
    }

    s.Reset();
    if (SUCCEEDED(f->CreatePathGeometry(&g.hub)) && SUCCEEDED(g.hub->Open(&s))) {
        s->BeginFigure(pt(40, 31), D2D1_FIGURE_BEGIN_HOLLOW);
        s->AddLine(pt(54, 39));
        s->AddLine(pt(68, 31));
        s->EndFigure(D2D1_FIGURE_END_OPEN);
        s->BeginFigure(pt(54, 26), D2D1_FIGURE_BEGIN_HOLLOW);
        s->AddLine(pt(54, 46));
        s->EndFigure(D2D1_FIGURE_END_OPEN);
        s->Close();
    }
    s.Reset();
    if (SUCCEEDED(f->CreatePathGeometry(&g.nodes)) && SUCCEEDED(g.nodes->Open(&s))) {
        addCircle(s.Get(), 40, 31, 3);
        addCircle(s.Get(), 54, 26, 3);
        addCircle(s.Get(), 68, 31, 3);
        s->Close();
    }

    f->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND), nullptr, 0,
                         &g.round);
    return g;
}

}  // namespace

void drawAppIcon(ID2D1RenderTarget* rt, ID2D1Factory* factory, ID2D1SolidColorBrush* brush, D2D1_RECT_F box,
                 D2D1_COLOR_F accent, bool detailed) {
    IconGeometry& g = geometry(factory);
    if (!g.body) return;
    const float size = box.right - box.left;
    brush->SetColor(theme::IconBackground);
    rt->FillRoundedRectangle(D2D1::RoundedRect(box, size * 0.22f, size * 0.22f), brush);

    D2D1_MATRIX_3X2_F old;
    rt->GetTransform(&old);
    if (detailed) {
        const float scale = size / 108.0f;
        rt->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale) * D2D1::Matrix3x2F::Translation(box.left, box.top) *
                         old);
    } else {
        // Tamanho de bandeja: só o controle, maior, na cor do estado do servidor.
        const float scale = size * 0.84f / 60.0f;
        rt->SetTransform(D2D1::Matrix3x2F::Translation(-54.0f, -63.0f) * D2D1::Matrix3x2F::Scale(scale, scale) *
                         D2D1::Matrix3x2F::Translation(box.left + size / 2, box.top + size / 2) * old);
    }
    brush->SetColor(accent);
    rt->FillGeometry(g.body.Get(), brush);
    if (detailed) {
        brush->SetColor(theme::Text);
        rt->DrawGeometry(g.hub.Get(), brush, 2.5f, g.round.Get());
        rt->FillGeometry(g.nodes.Get(), brush);
    }
    rt->SetTransform(old);
}

bool renderIconBitmap(ID2D1Factory* factory, IWICImagingFactory* wic, int sizePx, D2D1_COLOR_F accent,
                      ComPtr<IWICBitmap>* out) {
    ComPtr<IWICBitmap> bmp;
    if (FAILED(wic->CreateBitmap(UINT(sizePx), UINT(sizePx), GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad,
                                 &bmp))) {
        return false;
    }
    const auto props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f, 96.0f);
    ComPtr<ID2D1RenderTarget> rt;
    if (FAILED(factory->CreateWicBitmapRenderTarget(bmp.Get(), props, &rt))) return false;
    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(rt->CreateSolidColorBrush(accent, &brush))) return false;

    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(0, 0, 0, 0));
    drawAppIcon(rt.Get(), factory, brush.Get(), D2D1::RectF(0, 0, float(sizePx), float(sizePx)), accent,
                sizePx >= 32);
    if (FAILED(rt->EndDraw())) return false;
    *out = bmp;
    return true;
}

HICON createIcon(ID2D1Factory* factory, IWICImagingFactory* wic, int sizePx, D2D1_COLOR_F accent) {
    ComPtr<IWICBitmap> bmp;
    if (!renderIconBitmap(factory, wic, sizePx, accent, &bmp)) return nullptr;

    std::vector<uint8_t> px(size_t(sizePx) * sizePx * 4);
    const WICRect rc{0, 0, sizePx, sizePx};
    if (FAILED(bmp->CopyPixels(&rc, UINT(sizePx * 4), UINT(px.size()), px.data()))) return nullptr;
    // Direct2D gera alfa pré-multiplicado; ícones do Windows usam alfa "reto".
    for (size_t i = 0; i < px.size(); i += 4) {
        const unsigned a = px[i + 3];
        if (a == 0 || a == 255) continue;
        for (int c = 0; c < 3; ++c) px[i + c] = uint8_t(std::min(255u, px[i + c] * 255u / a));
    }

    BITMAPV5HEADER bi{};
    bi.bV5Size = sizeof(bi);
    bi.bV5Width = sizePx;
    bi.bV5Height = -sizePx;  // de cima para baixo
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;
    void* bits = nullptr;
    HDC dc = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    if (!color) return nullptr;
    std::memcpy(bits, px.data(), px.size());

    std::vector<uint8_t> maskBits(size_t((sizePx + 15) / 16) * 2 * sizePx, 0);
    HBITMAP mask = CreateBitmap(sizePx, sizePx, 1, 1, maskBits.data());
    ICONINFO ii{TRUE, 0, 0, mask, color};
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}

std::vector<uint8_t> encodePng(IWICImagingFactory* wic, IWICBitmap* bitmap) {
    std::vector<uint8_t> out;
    UINT w = 0, h = 0;
    bitmap->GetSize(&w, &h);
    ComPtr<IStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)) ||
        FAILED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
        FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
        FAILED(encoder->CreateNewFrame(&frame, nullptr)) || FAILED(frame->Initialize(nullptr)) ||
        FAILED(frame->SetSize(w, h))) {
        return out;
    }
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;  // PNG guarda alfa reto; o WIC converte
    if (FAILED(frame->SetPixelFormat(&format)) || FAILED(frame->WriteSource(bitmap, nullptr)) ||
        FAILED(frame->Commit()) || FAILED(encoder->Commit())) {
        return out;
    }
    STATSTG st{};
    HGLOBAL mem = nullptr;
    if (FAILED(stream->Stat(&st, STATFLAG_NONAME)) || FAILED(GetHGlobalFromStream(stream.Get(), &mem))) return out;
    const void* data = GlobalLock(mem);
    if (data) {
        out.assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + st.cbSize.QuadPart);
        GlobalUnlock(mem);
    }
    return out;
}

bool savePng(IWICImagingFactory* wic, IWICBitmap* bitmap, const std::wstring& path) {
    const std::vector<uint8_t> png = encodePng(wic, bitmap);
    if (png.empty()) return false;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return false;
    const bool ok = std::fwrite(png.data(), 1, png.size(), f) == png.size();
    std::fclose(f);
    return ok;
}

}  // namespace swc
