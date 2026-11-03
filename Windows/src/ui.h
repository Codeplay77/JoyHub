// Tela do programa: desenhada inteira com Direct2D/DirectWrite (tema escuro, nítida em qualquer DPI).
#pragma once

#include <dwrite.h>

#include "icon.h"
#include "server.h"
#include "system.h"

namespace swc {

constexpr float kWindowWidthDip = 1000.0f;
constexpr float kWindowHeightDip = 760.0f;
constexpr float kMinWidthDip = 760.0f;
constexpr float kMinHeightDip = 640.0f;

// Tudo o que a tela mostra. A janela monta a partir do servidor; --snapshot monta um exemplo.
struct UiModel {
    uint16_t port = kDefaultPort;
    DriverState driver = DriverState::Missing;
    std::vector<LocalAddress> addresses;
    std::vector<ClientView> clients;
    std::vector<float> packetRates;  // pacotes/s de cada cliente, na mesma ordem
    std::vector<std::string> events;
    FirewallState firewall = FirewallState::Unknown;
    bool firewallBusy = false;
    bool autostart = false;
    bool guideReserved = false;  // a Xbox Game Bar esconde o botão Guide da Steam
    bool guideHintDismissed = false;
};

enum class UiAction { None, Firewall, Autostart, DownloadDriver, ReleaseGuide, DismissGuide, TestRumble };

// O que um clique faz; botões de um cartão levam o slot do celular.
struct UiTarget {
    UiAction action = UiAction::None;
    int slot = -1;
    bool operator==(const UiTarget&) const = default;
};

struct HitRegion {
    D2D1_RECT_F rect;
    UiTarget target;
};

// Textos do estado do celular (bateria, Wi-Fi), usados nos cartões e na bandeja.
std::wstring wifiBandText(uint16_t mhz);  // "5 GHz"; vazio se desconhecida
bool phoneBatteryLow(const PhoneInfo& p);
std::wstring percentText(int basisPoints);  // 23 -> "0,23%"

// Estado geral: cor do ícone da bandeja e do selo no topo da janela.
enum class TrayState { Waiting, Connected, Warning, Error };
TrayState overallState(const UiModel& m);
D2D1_COLOR_F stateColor(TrayState s);
std::wstring statusText(const UiModel& m);
std::wstring formatDecimal(double v);  // "3,1"

class Renderer {
public:
    bool init(ID2D1Factory* d2d, IDWriteFactory* dwrite);
    // Desenha em DIPs no tamanho do render target e devolve as áreas clicáveis.
    void draw(ID2D1RenderTarget* rt, const UiModel& m, UiTarget hover, std::vector<HitRegion>& hits);
    void discardDeviceResources();

private:
    ComPtr<IDWriteTextFormat> makeFormat(float size, DWRITE_FONT_WEIGHT weight, DWRITE_TEXT_ALIGNMENT align,
                                         bool wrap = false);
    void color(D2D1_COLOR_F c);
    void text(const std::wstring& s, D2D1_RECT_F r, IDWriteTextFormat* f, D2D1_COLOR_F c);
    float textWidth(const std::wstring& s, IDWriteTextFormat* f);
    void fillRound(D2D1_RECT_F r, float radius, D2D1_COLOR_F c);
    void strokeRound(D2D1_RECT_F r, float radius, D2D1_COLOR_F c, float width = 1.0f);
    void fillCircle(float x, float y, float r, D2D1_COLOR_F c);
    void strokeCircle(float x, float y, float r, D2D1_COLOR_F c, float width);
    float buttonWidth(const std::wstring& label);
    D2D1_RECT_F button(const std::wstring& label, float x, float yMid, UiTarget target, D2D1_COLOR_F fill);
    // Botão discreto (contorno), para ações secundárias.
    D2D1_RECT_F outlineButton(const std::wstring& label, float x, float yMid, UiTarget target, bool enabled = true);
    void drawArtwork(D2D1_RECT_F area, float opacity);

    float drawHeader(const UiModel& m, float y);
    float drawBanner(const UiModel& m, float y);
    float drawGuideBanner(float y);
    void drawClients(const UiModel& m, D2D1_RECT_F area);
    void drawEmpty(const UiModel& m, D2D1_RECT_F area);
    void drawCard(const UiModel& m, const ClientView& c, float rate, D2D1_RECT_F r);
    void drawGamepad(const PadState& s, D2D1_RECT_F r, bool live);
    void drawEvents(const UiModel& m, D2D1_RECT_F area);
    void drawFooter(const UiModel& m, float y);

    ID2D1Factory* d2d_ = nullptr;
    IDWriteFactory* dw_ = nullptr;
    ID2D1RenderTarget* rt_ = nullptr;  // válido só durante draw()
    ID2D1RenderTarget* brushOwner_ = nullptr;
    ComPtr<ID2D1SolidColorBrush> brush_;
    ComPtr<IWICBitmap> artworkSource_;
    ComPtr<ID2D1Bitmap> artwork_;
    ComPtr<ID2D1RoundedRectangleGeometry> artworkClip_;
    ComPtr<ID2D1PathGeometry> gamepadShell_;
    ComPtr<IDWriteTextFormat> title_, bold_, boldRight_, body_, small_, smallBold_, smallRight_;
    ComPtr<IDWriteTextFormat> centerBold_, centerSmall_, letter_, wrap_;
    ComPtr<IDWriteTextFormat> hero_, label_;
    UiTarget hover_;
    std::vector<HitRegion>* hits_ = nullptr;
    float width_ = 0.0f;
    float height_ = 0.0f;
    float alpha_ = 1.0f;
};

// Renderiza a tela com dados de exemplo em PNG (e uma folha com os ícones), sem abrir janela.
bool renderSnapshot(const std::wstring& path, int demoClients,
                    float width = kWindowWidthDip, float height = kWindowHeightDip, float scale = 1.25f,
                    int demoDriver = -1, bool demoGuide = false);

}  // namespace swc
