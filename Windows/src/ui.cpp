#include "ui.h"
#include "theme.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace swc {
namespace {

constexpr float kPad = 20.0f;
constexpr D2D1_COLOR_F kBg = theme::Background;
constexpr D2D1_COLOR_F kPanel = theme::Surface;
constexpr D2D1_COLOR_F kPanel2 = theme::Raised;
constexpr D2D1_COLOR_F kFaint = theme::Border;
constexpr D2D1_COLOR_F kText = theme::Text;
constexpr D2D1_COLOR_F kDim = theme::Muted;
constexpr D2D1_COLOR_F kAccent = theme::Cyan;
constexpr D2D1_COLOR_F kWarn = theme::Warning;
constexpr D2D1_COLOR_F kError = theme::Error;
constexpr D2D1_COLOR_F kWhite = rgb(0xFFFFFF);
constexpr D2D1_COLOR_F kBrand = theme::Cyan;
constexpr D2D1_COLOR_F kButtonA = theme::Success;
constexpr D2D1_COLOR_F kButtonB = theme::Error;
constexpr D2D1_COLOR_F kButtonX = rgb(0x85BBFF);
constexpr D2D1_COLOR_F kButtonY = theme::Warning;

constexpr uint16_t kDpadUp = 0x0001, kDpadDown = 0x0002, kDpadLeft = 0x0004, kDpadRight = 0x0008;
constexpr uint16_t kStart = 0x0010, kBack = 0x0020, kLeftThumb = 0x0040, kRightThumb = 0x0080;
constexpr uint16_t kLeftShoulder = 0x0100, kRightShoulder = 0x0200, kGuide = 0x0400;
constexpr uint16_t kA = 0x1000, kB = 0x2000, kX = 0x4000, kY = 0x8000;

D2D1_COLOR_F withAlpha(D2D1_COLOR_F c, float a) {
    c.a = a;
    return c;
}

D2D1_RECT_F rect(float l, float t, float r, float b) { return D2D1::RectF(l, t, r, b); }

bool alive(const ClientView& c) { return !c.timedOut && !c.paused; }

constexpr int kWeakSignalDbm = -75;
constexpr int kHighLossBasisPoints = 200;  // 2%

std::wstring joinParts(const std::vector<std::wstring>& parts, const wchar_t* sep) {
    std::wstring s;
    for (const std::wstring& p : parts) {
        if (p.empty()) continue;
        if (!s.empty()) s += sep;
        s += p;
    }
    return s;
}

// Bateria e Wi-Fi do celular, e se algo merece atenção (a linha fica âmbar). Cada parte tem
// uma prioridade: quando a linha não cabe no cartão, as de número maior saem primeiro.
struct PhonePart {
    std::wstring text;
    int priority = 0;
};

struct PhoneSummary {
    std::vector<PhonePart> parts;
    bool warn = false;
};

PhoneSummary phoneSummary(const PhoneInfo& p, bool compact) {
    PhoneSummary s;
    if (!p.known) return s;
    if (p.battery <= 100) {
        std::wstring b = (compact ? L"" : L"Bateria ") + std::to_wstring(p.battery) + L"%";
        if (p.charging && !compact) b += L", carregando";
        s.parts.push_back({b, 0});
    }
    if (p.powerSave && !compact) s.parts.push_back({L"economia de energia ligada", 1});
    const std::wstring band = wifiBandText(p.wifiMhz);
    const bool slowBand = p.wifiMhz >= 2400 && p.wifiMhz < 2500;
    const bool weak = p.wifiDbm < 0 && p.wifiDbm <= kWeakSignalDbm;
    if (compact) {
        if (!band.empty()) s.parts.push_back({band, 0});
    } else if (p.wifiMhz != 0) {
        if (p.wifiGeneration >= 4) s.parts.push_back({L"Wi-Fi " + std::to_wstring(p.wifiGeneration), 3});
        s.parts.push_back({band.empty() ? std::to_wstring(p.wifiMhz) + L" MHz" : band, 0});
        if (p.wifiDbm < 0) s.parts.push_back({L"−" + std::to_wstring(-int(p.wifiDbm)) + L" dBm", 2});
        if (p.linkMbps > 0) s.parts.push_back({std::to_wstring(p.linkMbps) + L" Mbps", 3});
        if (slowBand) s.parts.push_back({L"prefira 5 GHz", 1});
        else if (weak) s.parts.push_back({L"sinal fraco", 1});
    }
    if (p.padBattery <= 100 && !compact) s.parts.push_back({L"controle " + std::to_wstring(p.padBattery) + L"%", 2});
    s.warn = phoneBatteryLow(p) || p.powerSave || slowBand || weak;
    return s;
}

std::wstring joinPhoneParts(const std::vector<PhonePart>& parts, int maxPriority, const wchar_t* sep) {
    std::vector<std::wstring> texts;
    for (const PhonePart& p : parts) {
        if (p.priority <= maxPriority) texts.push_back(p.text);
    }
    return joinParts(texts, sep);
}

}  // namespace

std::wstring wifiBandText(uint16_t mhz) {
    if (mhz >= 2400 && mhz < 2500) return L"2,4 GHz";
    if (mhz >= 4900 && mhz < 5925) return L"5 GHz";
    if (mhz >= 5925 && mhz < 7200) return L"6 GHz";
    if (mhz >= 57000) return L"60 GHz";
    return {};
}

bool phoneBatteryLow(const PhoneInfo& p) { return p.known && p.battery <= 15 && !p.charging; }

std::wstring percentText(int basisPoints) { return formatDecimal(basisPoints / 100.0) + L"%"; }

TrayState overallState(const UiModel& m) {
    if (m.driver == DriverState::Missing) return TrayState::Error;
    bool anyAlive = false;
    bool anyQuiet = false;
    for (const ClientView& c : m.clients) (alive(c) ? anyAlive : anyQuiet) = true;
    if (m.driver == DriverState::DryRun) return TrayState::Warning;
    if (anyAlive) return TrayState::Connected;
    return anyQuiet ? TrayState::Warning : TrayState::Waiting;
}

D2D1_COLOR_F stateColor(TrayState s) {
    switch (s) {
        case TrayState::Connected: return theme::Success;
        case TrayState::Warning: return kWarn;
        case TrayState::Error: return kError;
        default: return kAccent;
    }
}

std::wstring statusText(const UiModel& m) {
    if (m.driver == DriverState::Missing) return L"Sem driver ViGEmBus";
    int connected = 0, paused = 0, quiet = 0;
    for (const ClientView& c : m.clients) {
        if (c.paused) {
            ++paused;
        } else if (c.timedOut) {
            ++quiet;
        } else {
            ++connected;
        }
    }
    std::wstring s;
    if (connected == 1) {
        s = L"1 controle conectado";
    } else if (connected > 1) {
        s = std::to_wstring(connected) + L" controles conectados";
    } else if (paused) {
        s = L"Celular em segundo plano";
    } else if (quiet) {
        s = L"Sem sinal do celular";
    } else {
        s = L"Aguardando celular";
    }
    if (m.driver == DriverState::DryRun) s += L" (modo teste)";
    return s;
}

std::wstring formatDecimal(double v) {
    wchar_t buf[32];
    swprintf_s(buf, L"%.1f", v);
    for (wchar_t* p = buf; *p; ++p) {
        if (*p == L'.') *p = L',';
    }
    return buf;
}

// ------------------------------------------------------------------ recursos

ComPtr<IDWriteTextFormat> Renderer::makeFormat(float size, DWRITE_FONT_WEIGHT weight, DWRITE_TEXT_ALIGNMENT align,
                                               bool wrap) {
    ComPtr<IDWriteTextFormat> f;
    if (FAILED(dw_->CreateTextFormat(L"Segoe UI", nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                                     DWRITE_FONT_STRETCH_NORMAL, size, L"pt-br", &f))) {
        return nullptr;
    }
    f->SetTextAlignment(align);
    if (wrap) {
        f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        f->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    } else {
        // Uma linha, centralizada na altura do retângulo, com "…" quando não cabe.
        f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
        ComPtr<IDWriteInlineObject> ellipsis;
        if (SUCCEEDED(dw_->CreateEllipsisTrimmingSign(f.Get(), &ellipsis))) f->SetTrimming(&trimming, ellipsis.Get());
    }
    return f;
}

bool Renderer::init(ID2D1Factory* d2d, IDWriteFactory* dwrite) {
    d2d_ = d2d;
    dw_ = dwrite;
    const auto lead = DWRITE_TEXT_ALIGNMENT_LEADING;
    const auto center = DWRITE_TEXT_ALIGNMENT_CENTER;
    const auto right = DWRITE_TEXT_ALIGNMENT_TRAILING;
    title_ = makeFormat(24, DWRITE_FONT_WEIGHT_SEMI_BOLD, lead);
    hero_ = makeFormat(32, DWRITE_FONT_WEIGHT_SEMI_BOLD, lead);
    label_ = makeFormat(11, DWRITE_FONT_WEIGHT_SEMI_BOLD, lead);
    bold_ = makeFormat(14, DWRITE_FONT_WEIGHT_SEMI_BOLD, lead);
    boldRight_ = makeFormat(14, DWRITE_FONT_WEIGHT_SEMI_BOLD, right);
    body_ = makeFormat(13, DWRITE_FONT_WEIGHT_NORMAL, lead);
    small_ = makeFormat(12, DWRITE_FONT_WEIGHT_NORMAL, lead);
    smallBold_ = makeFormat(12, DWRITE_FONT_WEIGHT_SEMI_BOLD, lead);
    smallRight_ = makeFormat(12, DWRITE_FONT_WEIGHT_NORMAL, right);
    centerBold_ = makeFormat(14, DWRITE_FONT_WEIGHT_SEMI_BOLD, center);
    centerSmall_ = makeFormat(12, DWRITE_FONT_WEIGHT_NORMAL, center);
    letter_ = makeFormat(9, DWRITE_FONT_WEIGHT_BOLD, center);
    wrap_ = makeFormat(12, DWRITE_FONT_WEIGHT_NORMAL, lead, true);
    // A arte de fundo é decodificada uma vez; o bitmap de GPU só é recriado com o render target.
    const HMODULE module = GetModuleHandleW(nullptr);
    const HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(101), RT_RCDATA);
    if (resource) {
        const DWORD size = SizeofResource(module, resource);
        const HGLOBAL data = LoadResource(module, resource);
        auto* bytes = static_cast<BYTE*>(LockResource(data));
        ComPtr<IWICImagingFactory> wic;
        ComPtr<IWICStream> stream;
        ComPtr<IWICBitmapDecoder> decoder;
        ComPtr<IWICBitmapFrameDecode> frame;
        ComPtr<IWICFormatConverter> converter;
        if (bytes && SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                                 IID_PPV_ARGS(&wic))) &&
            SUCCEEDED(wic->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromMemory(bytes, size)) &&
            SUCCEEDED(wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder)) &&
            SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(wic->CreateFormatConverter(&converter)) &&
            SUCCEEDED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                                             nullptr, 0, WICBitmapPaletteTypeCustom))) {
            wic->CreateBitmapFromSource(converter.Get(), WICBitmapCacheOnLoad, &artworkSource_);
        }
    }
    // Contorno do controle, criado uma vez: a mesma silhueta do monitor do Android.
    ComPtr<ID2D1GeometrySink> shell;
    if (SUCCEEDED(d2d_->CreatePathGeometry(&gamepadShell_)) && SUCCEEDED(gamepadShell_->Open(&shell))) {
        const auto p = [](float x, float y) { return D2D1::Point2F(x, y); };
        shell->BeginFigure(p(70, 40), D2D1_FIGURE_BEGIN_FILLED);
        shell->AddBezier(D2D1::BezierSegment(p(42, 35), p(27, 48), p(20, 76)));
        shell->AddLine(p(8, 121));
        shell->AddBezier(D2D1::BezierSegment(p(-3, 165), p(25, 167), p(46, 143)));
        shell->AddLine(p(68, 132));
        shell->AddBezier(D2D1::BezierSegment(p(115, 150), p(185, 150), p(232, 132)));
        shell->AddLine(p(254, 143));
        shell->AddBezier(D2D1::BezierSegment(p(275, 167), p(303, 165), p(292, 121)));
        shell->AddLine(p(280, 76));
        shell->AddBezier(D2D1::BezierSegment(p(273, 48), p(258, 35), p(230, 40)));
        shell->AddBezier(D2D1::BezierSegment(p(190, 49), p(110, 49), p(70, 40)));
        shell->EndFigure(D2D1_FIGURE_END_CLOSED);
        shell->Close();
    }
    return title_ && bold_ && boldRight_ && body_ && small_ && smallBold_ && smallRight_ && centerBold_ &&
           centerSmall_ && letter_ && wrap_ && hero_ && label_ && gamepadShell_;
}

void Renderer::discardDeviceResources() {
    brush_.Reset();
    artwork_.Reset();
    brushOwner_ = nullptr;
}

// ------------------------------------------------------------------ primitivas

void Renderer::color(D2D1_COLOR_F c) {
    c.a *= alpha_;
    brush_->SetColor(c);
}

void Renderer::text(const std::wstring& s, D2D1_RECT_F r, IDWriteTextFormat* f, D2D1_COLOR_F c) {
    if (s.empty() || r.right <= r.left) return;
    color(c);
    rt_->DrawText(s.c_str(), UINT32(s.size()), f, r, brush_.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

float Renderer::textWidth(const std::wstring& s, IDWriteTextFormat* f) {
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(dw_->CreateTextLayout(s.c_str(), UINT32(s.size()), f, 10000.0f, 100.0f, &layout))) return 0.0f;
    DWRITE_TEXT_METRICS tm{};
    layout->GetMetrics(&tm);
    return tm.widthIncludingTrailingWhitespace;
}

void Renderer::fillRound(D2D1_RECT_F r, float radius, D2D1_COLOR_F c) {
    color(c);
    rt_->FillRoundedRectangle(D2D1::RoundedRect(r, radius, radius), brush_.Get());
}

void Renderer::strokeRound(D2D1_RECT_F r, float radius, D2D1_COLOR_F c, float width) {
    color(c);
    const float h = width / 2;
    rt_->DrawRoundedRectangle(D2D1::RoundedRect(rect(r.left + h, r.top + h, r.right - h, r.bottom - h), radius, radius),
                              brush_.Get(), width);
}

void Renderer::fillCircle(float x, float y, float r, D2D1_COLOR_F c) {
    color(c);
    rt_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), r, r), brush_.Get());
}

void Renderer::strokeCircle(float x, float y, float r, D2D1_COLOR_F c, float width) {
    color(c);
    rt_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), r, r), brush_.Get(), width);
}

float Renderer::buttonWidth(const std::wstring& label) { return textWidth(label, smallBold_.Get()) + 32.0f; }

D2D1_RECT_F Renderer::button(const std::wstring& label, float x, float yMid, UiTarget target, D2D1_COLOR_F fill) {
    const D2D1_RECT_F r = rect(x, yMid - 18, x + buttonWidth(label), yMid + 18);
    if (hover_ == target) {
        fill.r = std::min(1.0f, fill.r * 1.12f + 0.05f);
        fill.g = std::min(1.0f, fill.g * 1.12f + 0.05f);
        fill.b = std::min(1.0f, fill.b * 1.12f + 0.05f);
    }
    fillRound(r, 10, fill);
    if (hover_ == target) strokeRound(r, 10, kText);
    text(label, r, centerSmall_.Get(), theme::Ink);
    hits_->push_back({r, target});
    return r;
}

D2D1_RECT_F Renderer::outlineButton(const std::wstring& label, float x, float yMid, UiTarget target, bool enabled) {
    const D2D1_RECT_F r = rect(x, yMid - 15, x + buttonWidth(label), yMid + 15);
    const bool hot = enabled && hover_ == target;
    fillRound(r, 9, hot ? withAlpha(kAccent, 0.16f) : kPanel2);
    strokeRound(r, 9, hot ? kAccent : (enabled ? withAlpha(kAccent, 0.45f) : kFaint));
    text(label, r, centerSmall_.Get(), !enabled ? kDim : (hot ? kWhite : kText));
    if (enabled) hits_->push_back({r, target});
    return r;
}

// ------------------------------------------------------------------ tela

void Renderer::drawArtwork(D2D1_RECT_F area, float opacity) {
    if (!artwork_ || area.bottom <= area.top || area.right <= area.left) return;
    D2D1_ROUNDED_RECT clip{};
    if (artworkClip_) artworkClip_->GetRoundedRect(&clip);
    if (!artworkClip_ || clip.rect.left != area.left || clip.rect.right != area.right ||
        clip.rect.top != area.top || clip.rect.bottom != area.bottom) {
        artworkClip_.Reset();
        d2d_->CreateRoundedRectangleGeometry(D2D1::RoundedRect(area, 16, 16), &artworkClip_);
    }
    if (!artworkClip_) return;
    const auto size = artwork_->GetSize();
    const float aspect = (area.right - area.left) / (area.bottom - area.top);
    float w = size.width, h = size.height;
    if (w / h > aspect) w = h * aspect; else h = w / aspect;
    rt_->PushLayer(D2D1::LayerParameters(area, artworkClip_.Get()), nullptr);
    rt_->DrawBitmap(artwork_.Get(), area, opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                    rect((size.width - w) / 2, (size.height - h) / 2,
                         (size.width + w) / 2, (size.height + h) / 2));
    rt_->PopLayer();
}

void Renderer::draw(ID2D1RenderTarget* rt, const UiModel& m, UiTarget hover, std::vector<HitRegion>& hits) {
    if (rt != brushOwner_ || !brush_) {
        brush_.Reset();
        artwork_.Reset();
        if (FAILED(rt->CreateSolidColorBrush(kWhite, &brush_))) return;
        if (artworkSource_) rt->CreateBitmapFromWicBitmap(artworkSource_.Get(), nullptr, &artwork_);
        brushOwner_ = rt;
    }
    rt_ = rt;
    hover_ = hover;
    hits_ = &hits;
    hits.clear();
    alpha_ = 1.0f;
    const D2D1_SIZE_F size = rt->GetSize();
    width_ = size.width;
    height_ = size.height;

    rt->Clear(kBg);
    float y = drawHeader(m, kPad);
    y = drawBanner(m, y);
    const float footerY = height_ - kPad - 38.0f;
    const int eventLines = height_ < 700 ? 2 : 4;
    const float eventsH = 38.0f + float(eventLines) * 18.0f;
    const float eventsY = footerY - 14.0f - eventsH;
    text(L"SUA SESSÃO", rect(kPad, y, width_ - kPad, y + 18), label_.Get(), kDim);
    text(std::to_wstring(m.clients.size()) + L" / 4 controles", rect(kPad, y, width_ - kPad, y + 18),
         smallRight_.Get(), kDim);
    y += 28;
    drawClients(m, rect(kPad, y, width_ - kPad, std::max(y + 120.0f, eventsY - 14.0f)));
    drawEvents(m, rect(kPad, eventsY, width_ - kPad, eventsY + eventsH));
    drawFooter(m, footerY);
    rt_ = nullptr;
}

float Renderer::drawHeader(const UiModel& m, float y) {
    drawAppIcon(rt_, d2d_, brush_.Get(), rect(kPad, y, kPad + 46, y + 46), kBrand, true);
    const float tx = kPad + 60;

    const D2D1_COLOR_F sc = stateColor(overallState(m));
    const std::wstring status = statusText(m);
    const float pillW = textWidth(status, smallBold_.Get()) + 36;
    const D2D1_RECT_F pill = rect(width_ - kPad - pillW, y + 8, width_ - kPad, y + 38);
    fillRound(pill, 15, withAlpha(sc, 0.12f));
    strokeRound(pill, 15, withAlpha(sc, 0.3f));
    fillCircle(pill.left + 16, (pill.top + pill.bottom) / 2, 4.5f, sc);
    text(status, rect(pill.left + 26, pill.top, pill.right - 8, pill.bottom), smallBold_.Get(), sc);

    text(L"JoyHub", rect(tx, y - 2, pill.left - 12, y + 28), title_.Get(), kText);
    text(L"SEU CONTROLE. SEM FIOS.", rect(tx, y + 29, pill.left - 12, y + 47), label_.Get(), kDim);

    const auto network = rect(kPad, y + 64, width_ - kPad, y + 130);
    fillRound(network, 16, kPanel);
    drawArtwork(network, 0.48f);
    strokeRound(network, 16, kFaint);
    text(L"ENDEREÇOS DESTE PC", rect(network.left + 18, network.top + 10, network.right - 170, network.top + 26),
         label_.Get(), kDim);
    std::wstring addresses;
    if (m.addresses.empty()) addresses = L"Nenhuma rede ativa";
    for (size_t i = 0; i < m.addresses.size(); ++i) {
        addresses += (i ? L"    ·    " : L"") + utf8ToWide(m.addresses[i].ip);
    }
    text(addresses, rect(network.left + 18, network.top + 29, network.right - 170, network.bottom - 9), bold_.Get(), kText);
    text(L"CONEXÃO LOCAL", rect(network.right - 156, network.top + 10, network.right - 18, network.top + 26),
         smallRight_.Get(), kAccent);
    text(L"UDP " + std::to_wstring(m.port), rect(network.right - 156, network.top + 29, network.right - 18, network.bottom - 9),
         boldRight_.Get(), kText);
    return network.bottom + 18;
}

float Renderer::drawBanner(const UiModel& m, float y) {
    if (m.driver == DriverState::Ready) return m.guideReserved && !m.guideHintDismissed ? drawGuideBanner(y) : y + 4;
    const bool missing = m.driver == DriverState::Missing;
    const D2D1_COLOR_F c = missing ? kError : kWarn;
    const float h = 60;
    const D2D1_RECT_F r = rect(kPad, y, width_ - kPad, y + h);
    fillRound(r, 10, withAlpha(c, 0.12f));
    strokeRound(r, 10, withAlpha(c, 0.45f));
    float right = r.right - 14;
    if (missing) {
        const std::wstring label = L"Baixar ViGEmBus";
        const float bw = buttonWidth(label);
        button(label, right - bw, y + h / 2, {UiAction::DownloadDriver}, kAccent);
        right -= bw + 14;
    }
    text(missing ? L"Falta o driver no computador" : L"Modo teste ativo", rect(r.left + 16, y + 9, right, y + 31),
         bold_.Get(), c);
    text(missing ? L"Instale o ViGEmBus para usar o controle nos jogos. A detecção é automática."
                 : L"Conexão disponível para diagnóstico, sem criar um controle virtual.",
         rect(r.left + 16, y + 31, right, y + 51), small_.Get(), kText);
    return y + h + 14;
}

float Renderer::drawGuideBanner(float y) {
    const float h = 60;
    const D2D1_RECT_F r = rect(kPad, y, width_ - kPad, y + h);
    fillRound(r, 10, withAlpha(kWarn, 0.10f));
    strokeRound(r, 10, withAlpha(kWarn, 0.45f));
    float right = r.right - 14;
    const std::wstring later = L"Agora não";
    const float laterW = buttonWidth(later);
    outlineButton(later, right - laterW, y + h / 2, {UiAction::DismissGuide});
    right -= laterW + 10;
    const std::wstring release = L"Liberar para a Steam";
    const float releaseW = buttonWidth(release);
    button(release, right - releaseW, y + h / 2, {UiAction::ReleaseGuide}, kAccent);
    right -= releaseW + 14;
    text(L"O Windows está segurando o botão Guide", rect(r.left + 16, y + 9, right, y + 31), bold_.Get(), kWarn);
    text(L"A Xbox Game Bar usa o botão Xbox do controle, então a Steam não recebe o Guide.",
         rect(r.left + 16, y + 31, right, y + 51), small_.Get(), kText);
    return y + h + 14;
}

void Renderer::drawClients(const UiModel& m, D2D1_RECT_F area) {
    if (m.clients.empty()) {
        drawEmpty(m, area);
        return;
    }
    const int n = int(m.clients.size());
    const int cols = n == 1 ? 1 : 2;
    const int rows = (n + cols - 1) / cols;
    const float gap = 12;
    const float cw = (area.right - area.left - gap * float(cols - 1)) / float(cols);
    // Um celular só ganha um cartão maior (desenho do controle maior); com mais, grade 2x2.
    const float maxH = 520.0f;
    const float ch = std::min((area.bottom - area.top - gap * float(rows - 1)) / float(rows), maxH);
    for (int i = 0; i < n; ++i) {
        const float x = area.left + float(i % cols) * (cw + gap);
        const float y = area.top + float(i / cols) * (ch + gap);
        const float rate = size_t(i) < m.packetRates.size() ? m.packetRates[size_t(i)] : 0.0f;
        drawCard(m, m.clients[size_t(i)], rate, rect(x, y, x + cw, y + ch));
    }
}

void Renderer::drawEmpty(const UiModel& m, D2D1_RECT_F a) {
    fillRound(a, 18, kPanel);
    strokeRound(a, 18, kFaint);
    const float split = a.left + (a.right - a.left) * 0.57f;
    const float y = a.top + std::max(16.0f, (a.bottom - a.top - 180) / 2);
    const float x = a.left + 24;
    text(L"PRONTO PARA O PRÓXIMO JOGO", rect(x, y, split, y + 18), label_.Get(), kAccent);
    text(L"Seu controle. Sem fios.", rect(x, y + 28, split, y + 70), hero_.Get(), kText);
    text(L"Abra o app Android e toque em “Buscar na rede”.\nOu conecte usando um dos IPs acima.",
         rect(x, y + 83, split - 20, y + 128), wrap_.Get(), kDim);
    const std::wstring note = m.addresses.empty() ? L"Conecte este PC à sua rede para começar."
                                                 : L"Celular e computador na mesma rede.";
    text(note, rect(x, y + 142, split, y + 166), body_.Get(), kDim);
    const PadState idle{};
    drawGamepad(idle, rect(split + 10, a.top + 24, a.right - 24, a.bottom - 52), true);
    text(L"USB / BLUETOOTH  →  WI-FI  →  PC", rect(split, a.bottom - 45, a.right - 24, a.bottom - 23),
         centerSmall_.Get(), kDim);
}

void Renderer::drawCard(const UiModel& m, const ClientView& c, float rate, D2D1_RECT_F r) {
    const bool live = alive(c);
    const bool noPad = !c.padActive && m.driver != DriverState::DryRun;
    const D2D1_COLOR_F edge = !live ? kWarn : (noPad ? kError : theme::Success);
    fillRound(r, 16, kPanel);
    strokeRound(r, 16, withAlpha(edge, 0.3f));
    const std::wstring testLabel = c.rumbleTest ? L"Vibrando…" : L"Testar vibração";
    const UiTarget testTarget{UiAction::TestRumble, c.slot};
    const bool canTest = live && !c.rumbleTest;
    const bool highLoss = c.lossBasisPoints >= kHighLossBasisPoints;

    // Cartão compacto (grade 2x2): o desenho do controle fica ao lado das leituras, não espremido embaixo.
    if (r.bottom - r.top < 190) {
        const bool shortCard = r.bottom - r.top < 120;
        const float split = r.left + (r.right - r.left) * 0.58f;
        fillCircle(r.left + 28, r.top + 28, 12, live && !noPad ? kAccent : kFaint);
        text(c.player >= 0 ? std::to_wstring(c.player + 1) : L"?",
             rect(r.left + 16, r.top + 16, r.left + 40, r.top + 40), centerSmall_.Get(), live && !noPad ? kBg : kText);
        text(c.name.empty() ? L"Celular" : utf8ToWide(c.name),
             rect(r.left + 48, r.top + 16, split - 4, r.top + 40), bold_.Get(), kText);
        const bool subIsStatus = shortCard && (c.paused || c.timedOut || noPad);
        std::wstring sub = shortCard && c.paused ? L"Em segundo plano" :
            shortCard && c.timedOut ? L"Sem sinal · Neutro" :
            shortCard && noPad ? L"Instale o ViGEmBus" : utf8ToWide(c.address);
        const PhoneSummary phone = phoneSummary(c.phone, true);
        const std::wstring phoneText = joinPhoneParts(phone.parts, 0, L" · ");
        if (!subIsStatus && !phoneText.empty()) sub += L" · " + phoneText;
        text(sub, rect(r.left + 16, r.top + 43, split, r.top + 61), small_.Get(),
             subIsStatus ? edge : (phone.warn ? kWarn : kDim));
        const std::wstring ping = c.rttUs ? formatDecimal(c.rttUs / 1000.0) + L" ms" : L"— ms";
        text(ping + L"  ·  " + std::to_wstring(int(std::lround(rate))) + L" pacotes/s",
             rect(r.left + 16, r.top + 65, split, r.top + 85), smallBold_.Get(), live ? kAccent : kDim);
        if (r.bottom - r.top >= 160) outlineButton(testLabel, r.left + 16, r.top + 110, testTarget, canTest);
        const std::wstring detail = c.paused ? L"Em segundo plano · Controle em neutro" :
            c.timedOut ? L"Sem sinal há " + formatDecimal(c.silentMs / 1000.0) + L" s · Controle em neutro" :
            noPad ? L"Sem driver · Instale o ViGEmBus no PC" :
            joinParts({c.lossBasisPoints >= 0 ? L"Perda " + percentText(c.lossBasisPoints)
                                              : L"Perdidos " + std::to_wstring(c.lost),
                       L"Recuperados " + std::to_wstring(c.recovered),
                       L"Vibração " + std::to_wstring(c.largeMotor) + L"/" + std::to_wstring(c.smallMotor)},
                      L" · ");
        if (!shortCard) text(detail, rect(r.left + 16, r.bottom - 30, r.right - 16, r.bottom - 10), small_.Get(),
                             !live || noPad ? edge : (highLoss ? kWarn : kDim));
        drawGamepad(c.state, rect(split + 4, r.top + 12, r.right - 14, r.bottom - (shortCard ? 12 : 38)), live);
        return;
    }

    // Selo com o número do jogador (slot do XInput).
    const float bx = r.left + 32;
    const float by = r.top + 32;
    fillCircle(bx, by, 17, live && !noPad ? kAccent : kFaint);
    text(c.player >= 0 ? std::to_wstring(c.player + 1) : L"?", rect(bx - 17, by - 17, bx + 17, by + 17),
         centerBold_.Get(), live && !noPad ? kBg : kText);

    const float statsW = 112;
    const float tx = r.left + 60;
    const float statsL = r.right - 16 - statsW;
    const std::wstring name = c.name.empty() ? L"Celular" : utf8ToWide(c.name);
    text(name, rect(tx, r.top + 13, statsL - 8, r.top + 33), bold_.Get(), kText);
    std::wstring who = c.player >= 0 ? L"Jogador " + std::to_wstring(c.player + 1) : L"Jogador ?";
    who += L"   ·   " + utf8ToWide(c.address);
    text(who, rect(tx, r.top + 33, statsL - 8, r.top + 51), small_.Get(), kDim);

    const std::wstring ping = c.rttUs ? L"ping " + formatDecimal(c.rttUs / 1000.0) + L" ms" : L"ping —";
    text(ping, rect(statsL, r.top + 13, r.right - 16, r.top + 33), boldRight_.Get(), live ? kAccent : kDim);
    text(std::to_wstring(int(std::lround(rate))) + L" pacotes/s", rect(statsL, r.top + 33, r.right - 16, r.top + 51),
         smallRight_.Get(), kDim);

    std::wstring line;
    D2D1_COLOR_F lineColor = kDim;
    if (c.paused) {
        line = L"App em segundo plano no celular: controle em posição neutra";
        lineColor = kWarn;
    } else if (c.timedOut) {
        line = L"Sem sinal há " + formatDecimal(c.silentMs / 1000.0) + L" s: controle em posição neutra";
        lineColor = kWarn;
    } else if (noPad) {
        line = L"Sem o driver ViGEmBus a Steam não vê este controle";
        lineColor = kError;
    } else {
        line = joinParts({c.lossBasisPoints >= 0 ? L"perda de pacotes " + percentText(c.lossBasisPoints) : L"",
                          L"recuperados " + std::to_wstring(c.recovered), L"perdidos " + std::to_wstring(c.lost)},
                         L"   ·   ");
        if (highLoss) lineColor = kWarn;
    }
    text(line, rect(r.left + 16, r.top + 58, r.right - 16, r.top + 76), small_.Get(), lineColor);
    float gamepadTop = r.top + 82;
    const PhoneSummary phone = phoneSummary(c.phone, false);
    if (!phone.parts.empty()) {
        const float room = r.right - r.left - 32;
        std::wstring phoneText;
        for (int maxPriority = 3; maxPriority >= 0; --maxPriority) {
            phoneText = joinPhoneParts(phone.parts, maxPriority, L"  ·  ");
            if (textWidth(phoneText, small_.Get()) <= room) break;
        }
        text(phoneText, rect(r.left + 16, r.top + 78, r.right - 16, r.top + 96), small_.Get(), phone.warn ? kWarn : kDim);
        gamepadTop = r.top + 104;
    }

    // Rodapé: vibração pedida pelo jogo e o teste (dois pulsos no celular).
    const float footY = r.bottom - 27;
    const float testW = buttonWidth(testLabel);
    outlineButton(testLabel, r.right - 16 - testW, footY, testTarget, canTest);
    text(L"vibração do jogo " + std::to_wstring(c.largeMotor) + L"/" + std::to_wstring(c.smallMotor),
         rect(r.left + 16, footY - 9, r.right - 28 - testW, footY + 9), small_.Get(), kDim);
    drawGamepad(c.state, rect(r.left + 12, gamepadTop, r.right - 12, r.bottom - 50), live);
}

void Renderer::drawGamepad(const PadState& s, D2D1_RECT_F r, bool live) {
    // Esquema de um controle Xbox em 300x160, escalado para caber em r.
    const float bw = 300.0f, bh = 160.0f;
    const float scale = std::min((r.right - r.left) / bw, (r.bottom - r.top) / bh);
    if (scale <= 0.05f) return;
    const float ox = r.left + ((r.right - r.left) - bw * scale) / 2;
    const float oy = r.top + ((r.bottom - r.top) - bh * scale) / 2;
    D2D1_MATRIX_3X2_F old;
    rt_->GetTransform(&old);
    rt_->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale) * D2D1::Matrix3x2F::Translation(ox, oy) * old);
    const float savedAlpha = alpha_;
    if (!live) alpha_ = 0.45f;
    const auto on = [&s](uint16_t mask) { return (s.buttons & mask) != 0; };

    color(kPanel2);
    rt_->FillGeometry(gamepadShell_.Get(), brush_.Get());
    color(rgb(0x415974));
    rt_->DrawGeometry(gamepadShell_.Get(), brush_.Get(), 1.2f);

    const auto trigger = [&](float x, uint8_t value, const wchar_t* label) {
        const D2D1_RECT_F track = rect(x, 4, x + 76, 18);
        fillRound(track, 7, kFaint);
        if (value > 0) {
            rt_->PushAxisAlignedClip(rect(x, 4, x + 76.0f * float(value) / 255.0f, 18), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            fillRound(track, 7, kAccent);
            rt_->PopAxisAlignedClip();
        }
        text(label, track, letter_.Get(), value > 127 ? kBg : kText);
    };
    trigger(20, s.leftTrigger, L"LT");
    trigger(204, s.rightTrigger, L"RT");

    const auto bumper = [&](float x, bool pressed, const wchar_t* label) {
        const D2D1_RECT_F b = rect(x, 22, x + 76, 36);
        fillRound(b, 7, pressed ? kAccent : kFaint);
        text(label, b, letter_.Get(), pressed ? kBg : kText);
    };
    bumper(20, on(kLeftShoulder), L"LB");
    bumper(204, on(kRightShoulder), L"RB");

    const auto stick = [&](float cx, float cy, int16_t x, int16_t y, bool pressed) {
        fillCircle(cx, cy, 21, pressed ? withAlpha(kAccent, 0.35f) : kBg);
        strokeCircle(cx, cy, 21, pressed ? kAccent : kFaint, 2);
        const float dx = float(x) / 32767.0f * 13.0f;
        const float dy = -float(y) / 32767.0f * 13.0f;  // Y do XInput é para cima
        const bool moved = std::abs(int(x)) > 4000 || std::abs(int(y)) > 4000;
        fillCircle(cx + dx, cy + dy, 13, pressed ? kAccent : rgb(0x2A3E53));
        strokeCircle(cx + dx, cy + dy, 13, moved || pressed ? kAccent : rgb(0x415974), 1.5f);
        fillCircle(cx + dx, cy + dy, 1.5f, pressed ? kBg : kDim);
    };
    stick(72, 76, s.thumbLX, s.thumbLY, on(kLeftThumb));
    stick(192, 120, s.thumbRX, s.thumbRY, on(kRightThumb));

    const float px = 112, py = 120;
    const auto arm = [&](D2D1_RECT_F a, bool pressed) { fillRound(a, 2, pressed ? kAccent : kFaint); };
    arm(rect(px - 6, py - 19, px + 6, py - 5), on(kDpadUp));
    arm(rect(px - 6, py + 5, px + 6, py + 19), on(kDpadDown));
    arm(rect(px - 19, py - 6, px - 5, py + 6), on(kDpadLeft));
    arm(rect(px + 5, py - 6, px + 19, py + 6), on(kDpadRight));
    fillRound(rect(px - 6, py - 6, px + 6, py + 6), 1, kFaint);

    const auto face = [&](float cx, float cy, bool pressed, const wchar_t* label, D2D1_COLOR_F c) {
        if (pressed) {
            fillCircle(cx, cy, 9, c);
        } else {
            fillCircle(cx, cy, 9, kBg);
            strokeCircle(cx, cy, 9, withAlpha(c, 0.85f), 1.5f);
        }
        text(label, rect(cx - 9, cy - 9, cx + 9, cy + 9), letter_.Get(), pressed ? kBg : c);
    };
    face(232, 58, on(kY), L"Y", kButtonY);
    face(214, 76, on(kX), L"X", kButtonX);
    face(250, 76, on(kB), L"B", kButtonB);
    face(232, 94, on(kA), L"A", kButtonA);

    fillRound(rect(124, 72, 142, 82), 5, on(kBack) ? kAccent : kFaint);
    fillRound(rect(158, 72, 176, 82), 5, on(kStart) ? kAccent : kFaint);
    fillCircle(150, 56, 9, on(kGuide) ? kAccent : kFaint);

    alpha_ = savedAlpha;
    rt_->SetTransform(old);
}

void Renderer::drawEvents(const UiModel& m, D2D1_RECT_F a) {
    fillRound(a, 14, kPanel);
    strokeRound(a, 14, kFaint);
    text(L"ATIVIDADE RECENTE", rect(a.left + 16, a.top + 10, a.right - 16, a.top + 28), label_.Get(), kDim);
    float y = a.top + 32;
    if (m.events.empty()) {
        text(L"As conexões e os avisos aparecem aqui.", rect(a.left + 16, y, a.right - 16, y + 18), small_.Get(), kDim);
        return;
    }
    const size_t lines = size_t(std::max(1.0f, std::floor((a.bottom - y - 6) / 18)));
    const size_t first = m.events.size() > lines ? m.events.size() - lines : 0;
    for (size_t i = first; i < m.events.size(); ++i) {
        fillCircle(a.left + 19, y + 9, 2, kAccent);
        text(utf8ToWide(m.events[i]), rect(a.left + 30, y, a.right - 16, y + 18), small_.Get(), kDim);
        y += 18;
    }
}

void Renderer::drawFooter(const UiModel& m, float y) {
    const float mid = y + 17;
    float x = kPad;

    if (m.firewallBusy) {
        const std::wstring t = L"Aguardando permissão do Windows…";
        text(t, rect(x, y, x + 260, y + 34), small_.Get(), kDim);
        x += textWidth(t, small_.Get()) + 24;
    } else if (m.firewall == FirewallState::Allowed) {
        color(kAccent);
        rt_->DrawLine(D2D1::Point2F(x + 2, mid), D2D1::Point2F(x + 6, mid + 4), brush_.Get(), 2.0f);
        rt_->DrawLine(D2D1::Point2F(x + 6, mid + 4), D2D1::Point2F(x + 13, mid - 4), brush_.Get(), 2.0f);
        const std::wstring t = L"Firewall liberado";
        text(t, rect(x + 20, y, x + 220, y + 34), small_.Get(), kAccent);
        x += 20 + textWidth(t, small_.Get()) + 24;
    } else if (m.firewall == FirewallState::Unknown) {
        const std::wstring t = L"Verificando o firewall…";
        text(t, rect(x, y, x + 220, y + 34), small_.Get(), kDim);
        x += textWidth(t, small_.Get()) + 24;
    } else {
        const bool blocked = m.firewall == FirewallState::Blocked;
        const D2D1_RECT_F b = button(blocked ? L"Desbloquear no firewall" : L"Liberar no firewall", x, mid,
                                     {UiAction::Firewall}, blocked ? kError : kAccent);
        x = b.right + 24;
    }

    // Caixa "Iniciar com o Windows".
    const std::wstring label = L"Iniciar com o Windows";
    const float boxL = x;
    const D2D1_RECT_F box = rect(boxL, mid - 9, boxL + 18, mid + 9);
    const bool hot = hover_.action == UiAction::Autostart;
    if (m.autostart) {
        fillRound(box, 4, hot ? rgb(0xB2F5FC) : kAccent);
        color(kBg);
        rt_->DrawLine(D2D1::Point2F(boxL + 4, mid), D2D1::Point2F(boxL + 7.5f, mid + 3.5f), brush_.Get(), 2.0f);
        rt_->DrawLine(D2D1::Point2F(boxL + 7.5f, mid + 3.5f), D2D1::Point2F(boxL + 14, mid - 4), brush_.Get(), 2.0f);
    } else {
        strokeRound(box, 4, hot ? kText : kDim, 1.5f);
    }
    const float labelW = textWidth(label, small_.Get());
    text(label, rect(boxL + 26, y, boxL + 26 + labelW + 4, y + 34), small_.Get(), hot ? kWhite : kText);
    hits_->push_back({rect(boxL - 4, y, boxL + 30 + labelW, y + 34), {UiAction::Autostart}});

    text(L"O × mantém o app na bandeja",
         rect(boxL + 46 + labelW, y, width_ - kPad, y + 34), smallRight_.Get(), kDim);
}

// ------------------------------------------------------------------ captura sem janela

namespace {

UiModel demoModel(int clients, int demoDriver, bool demoGuide) {
    UiModel m;
    m.addresses = {{"Ethernet", "192.168.0.10"}, {"Wi-Fi", "192.168.0.11"}};
    m.driver = demoDriver >= 0 ? DriverState(demoDriver) : (clients > 0 ? DriverState::Ready : DriverState::Missing);
    m.firewall = clients > 0 ? FirewallState::Allowed : FirewallState::NotAllowed;
    m.autostart = clients > 0;
    m.guideReserved = demoGuide;
    const char* names[] = {"Galaxy S23 Ultra", "Pixel 8", "Moto G84", "Redmi Note 13"};
    for (int i = 0; i < std::min(clients, 4); ++i) {
        ClientView c;
        c.slot = i;
        c.clientId = uint32_t(1000 + i);
        c.padActive = m.driver == DriverState::Ready;
        c.name = names[i];
        c.address = "192.168.0." + std::to_string(21 + i);
        c.player = i;
        c.rttUs = uint32_t(2600 + 1700 * i);
        c.lost = uint64_t(i);
        c.recovered = uint64_t(3 + i * 2);
        c.lossBasisPoints = i == 1 ? 260 : 10 * i;
        c.phone.known = true;
        const uint8_t batteries[] = {76, 14, 58, 91};
        c.phone.battery = batteries[i];
        c.phone.wifiGeneration = 6;
        c.phone.wifiMhz = 5180;
        c.phone.wifiDbm = int8_t(-52 - 6 * i);
        c.phone.linkMbps = 866;
        if (i == 0) {
            c.state.buttons = kA | kRightShoulder | kDpadUp;
            c.state.rightTrigger = 190;
            c.state.thumbLX = 14000;
            c.state.thumbLY = -21000;
            c.largeMotor = 120;
            c.smallMotor = 40;
            c.phone.charging = true;
        } else if (i == 1) {
            c.state.buttons = kY | kLeftThumb;
            c.state.leftTrigger = 80;
            c.state.thumbRX = -26000;
            c.state.thumbRY = 9000;
            c.phone.wifiGeneration = 4;
            c.phone.wifiMhz = 2437;
            c.phone.linkMbps = 72;
            c.rumbleTest = true;
        } else if (i == 2) {
            c.timedOut = true;
            c.silentMs = 1800;
        } else {
            c.paused = true;
        }
        m.clients.push_back(c);
        m.packetRates.push_back(i < 2 ? 480.0f - 130.0f * float(i) : 0.0f);
    }
    m.events = {"21:14:02  Driver ViGEmBus pronto", "21:14:09  Celular conectado de 192.168.0.21: controle Xbox 360 virtual pronto",
                "21:15:40  Celular conectado de 192.168.0.22: controle Xbox 360 virtual pronto"};
    if (m.driver == DriverState::Missing) m.events = {"21:14:02  Driver ViGEmBus não encontrado. Aguardando instalação."};
    else if (m.driver == DriverState::DryRun) m.events = {"21:14:02  Servidor em modo teste: nenhum controle virtual criado."};
    else if (clients == 0) m.events = {"21:14:02  Driver ViGEmBus pronto. Aguardando o celular."};
    return m;
}

}  // namespace

bool renderSnapshot(const std::wstring& path, int demoClients, float width, float height, float scale, int demoDriver,
                    bool demoGuide) {
    ComPtr<ID2D1Factory> d2d;
    ComPtr<IDWriteFactory> dw;
    ComPtr<IWICImagingFactory> wic;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf())) ||
        FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(dw.GetAddressOf()))) ||
        FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) {
        return false;
    }
    Renderer renderer;
    if (!renderer.init(d2d.Get(), dw.Get())) return false;

    ComPtr<IWICBitmap> bmp;
    if (FAILED(wic->CreateBitmap(UINT(width * scale), UINT(height * scale),
                                 GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp))) {
        return false;
    }
    const auto props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        96.0f * scale, 96.0f * scale);
    ComPtr<ID2D1RenderTarget> rt;
    if (FAILED(d2d->CreateWicBitmapRenderTarget(bmp.Get(), props, &rt))) return false;
    std::vector<HitRegion> hits;
    rt->BeginDraw();
    renderer.draw(rt.Get(), demoModel(demoClients, demoDriver, demoGuide), {}, hits);
    if (FAILED(rt->EndDraw())) return false;
    rt.Reset();
    if (!savePng(wic.Get(), bmp.Get(), path)) return false;

    // Folha de ícones: os 4 estados da bandeja em 16/24/32 px e o ícone do app em 48/64/128.
    ComPtr<IWICBitmap> sheet;
    if (FAILED(wic->CreateBitmap(480, 150, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &sheet)) ||
        FAILED(d2d->CreateWicBitmapRenderTarget(sheet.Get(), D2D1::RenderTargetProperties(
                                                    D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                                    D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                                                                      D2D1_ALPHA_MODE_PREMULTIPLIED)),
                                                &rt))) {
        return false;
    }
    ComPtr<ID2D1SolidColorBrush> brush;
    rt->CreateSolidColorBrush(kWhite, &brush);
    rt->BeginDraw();
    rt->Clear(rgb(0x202020));
    const TrayState states[] = {TrayState::Waiting, TrayState::Connected, TrayState::Warning, TrayState::Error};
    float x = 10;
    for (TrayState st : states) {
        float yy = 10;
        for (float sz : {16.0f, 24.0f, 32.0f}) {
            drawAppIcon(rt.Get(), d2d.Get(), brush.Get(), rect(x, yy, x + sz, yy + sz), stateColor(st), sz >= 32);
            yy += sz + 8;
        }
        x += 44;
    }
    for (float sz : {48.0f, 64.0f, 128.0f}) {
        drawAppIcon(rt.Get(), d2d.Get(), brush.Get(), rect(x, 10, x + sz, 10 + sz), kBrand, true);
        x += sz + 10;
    }
    if (FAILED(rt->EndDraw())) return false;
    rt.Reset();
    std::wstring sheetPath = path;
    const size_t dot = sheetPath.rfind(L'.');
    sheetPath.insert(dot == std::wstring::npos ? sheetPath.size() : dot, L"-icones");
    return savePng(wic.Get(), sheet.Get(), sheetPath);
}

}  // namespace swc
