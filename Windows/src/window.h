// Janela principal + ícone na área de notificação (perto do relógio).
#pragma once

#include <d2d1.h>
#include <dwrite.h>
#include <shellapi.h>

#include <atomic>
#include <map>

#include "ui.h"

namespace swc {

// Mesmo nome das versões anteriores, para a ativação da instância única continuar funcionando.
constexpr wchar_t kWindowClass[] = L"SteamWifiControllerWindow";
constexpr UINT WM_APP_TRAY = WM_APP + 1;      // eventos do ícone da bandeja
constexpr UINT WM_APP_SHOW = WM_APP + 2;      // outra instância pediu para mostrar a janela
constexpr UINT WM_APP_FIREWALL = WM_APP + 3;  // resultado da verificação do firewall

class MainWindow {
public:
    MainWindow(Server& server, HINSTANCE instance);
    ~MainWindow();
    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    bool create(bool visible);
    int run();

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);

    void onTimer();
    void paint();
    void show();
    void hideToTray();
    void updateTimer();
    void refreshModel();
    void announceChanges();
    void runAction(UiTarget target);
    void checkFirewallAsync();
    void openFirewallAsync();
    void onMouseMove(int x, int y);

    void createTrayIcons();
    void trayAdd();
    void trayUpdate();
    void trayRemove();
    void trayMenu(int x, int y);
    void notify(const std::wstring& title, const std::wstring& text);

    Server& server_;
    HINSTANCE instance_;
    HWND hwnd_ = nullptr;
    UINT dpi_ = 96;
    UINT timerMs_ = 0;

    ComPtr<ID2D1Factory> d2d_;
    ComPtr<IDWriteFactory> dwrite_;
    ComPtr<IWICImagingFactory> wic_;
    ComPtr<ID2D1HwndRenderTarget> target_;
    Renderer renderer_;

    UiModel model_;
    std::vector<HitRegion> hits_;
    UiTarget hover_;
    bool trackingMouse_ = false;
    uint64_t lastAddressRefresh_ = 0;
    uint64_t lastGuideCheck_ = 0;
    std::map<uint32_t, bool> lowBatteryWarned_;  // clientId -> já avisou da bateria fraca
    std::map<int, std::pair<uint64_t, uint64_t>> rateBase_;  // slot -> (pacotes, ms)
    std::map<int, float> rates_;
    std::map<int, std::pair<uint32_t, std::wstring>> known_;  // slot -> (clientId, nome) já anunciados
    bool firstRefresh_ = true;
    std::atomic<bool> firewallBusy_{false};

    NOTIFYICONDATAW nid_{};
    bool trayAdded_ = false;
    int trayState_ = -1;
    std::wstring trayTip_;
    uint64_t lastTrayUpdate_ = 0;
    HICON trayIcons_[4] = {};
    HICON appIconBig_ = nullptr;
    HICON appIconSmall_ = nullptr;
    UINT taskbarCreatedMsg_ = 0;
};

}  // namespace swc
