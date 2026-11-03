#include "window.h"

#include <dwmapi.h>
#include <windowsx.h>

#include <algorithm>
#include <thread>

namespace swc {
namespace {

constexpr UINT_PTR kTimerId = 1;
constexpr UINT kTimerVisibleMs = 33;   // ~30 Hz: o desenho do controle acompanha os botões
constexpr UINT kTimerHiddenMs = 1000;  // escondido: só ícone, dica e avisos
constexpr UINT kTrayId = 1;
constexpr wchar_t kDriverUrl[] = L"https://github.com/nefarius/ViGEmBus/releases/latest";
constexpr wchar_t kHintSetting[] = L"TrayHintShown";
constexpr wchar_t kGuideHintSetting[] = L"GuideHintDismissed";
constexpr uint64_t kGuideCheckMs = 2000;  // a opção da Game Bar pode mudar nas Configurações a qualquer hora
constexpr int kBatteryRearmPercent = 20;

enum MenuId : UINT { kMenuOpen = 100, kMenuAutostart, kMenuFirewall, kMenuReleaseGuide, kMenuExit };

}  // namespace

MainWindow::MainWindow(Server& server, HINSTANCE instance) : server_(server), instance_(instance) {}

MainWindow::~MainWindow() {
    for (HICON icon : trayIcons_) {
        if (icon) DestroyIcon(icon);
    }
    if (appIconBig_) DestroyIcon(appIconBig_);
    if (appIconSmall_) DestroyIcon(appIconSmall_);
}

bool MainWindow::create(bool visible) {
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d_.GetAddressOf())) ||
        FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                   reinterpret_cast<IUnknown**>(dwrite_.GetAddressOf()))) ||
        FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic_))) ||
        !renderer_.init(d2d_.Get(), dwrite_.Get())) {
        return false;
    }

    appIconBig_ = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                                GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0));
    appIconSmall_ = static_cast<HICON>(LoadImageW(instance_, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                                  GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0));

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = windowProc;
    wc.hInstance = instance_;
    wc.hIcon = appIconBig_;
    wc.hIconSm = appIconSmall_;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kWindowClass;
    if (!RegisterClassExW(&wc)) return false;
    taskbarCreatedMsg_ = RegisterWindowMessageW(L"TaskbarCreated");

    hwnd_ = CreateWindowExW(0, kWindowClass, L"JoyHub", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                            CW_USEDEFAULT, 800, 600, nullptr, nullptr, instance_, this);
    if (!hwnd_) return false;

    // Tamanho inicial em DIPs, centralizado no monitor.
    dpi_ = GetDpiForWindow(hwnd_);
    RECT r{0, 0, MulDiv(int(kWindowWidthDip), int(dpi_), 96), MulDiv(int(kWindowHeightDip), int(dpi_), 96)};
    AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi_);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY), &mi);
    const int workW = mi.rcWork.right - mi.rcWork.left;
    const int workH = mi.rcWork.bottom - mi.rcWork.top;
    const int w = std::min<int>(r.right - r.left, workW);
    const int h = std::min<int>(r.bottom - r.top, workH);
    SetWindowPos(hwnd_, nullptr, mi.rcWork.left + (workW - w) / 2, mi.rcWork.top + (workH - h) / 2, w, h,
                 SWP_NOZORDER | SWP_NOACTIVATE);

    // Barra de título escura, combinando com a janela (atributo 20; 19 em builds antigos do Windows 10).
    const BOOL dark = TRUE;
    if (FAILED(DwmSetWindowAttribute(hwnd_, 20, &dark, sizeof(dark)))) {
        DwmSetWindowAttribute(hwnd_, 19, &dark, sizeof(dark));
    }
    // Cores da barra de título no Windows 11; o Windows 10 ignora e fica com a barra escura acima.
    const COLORREF caption = RGB(8, 14, 23), border = RGB(42, 59, 79), captionText = RGB(240, 246, 252);
    DwmSetWindowAttribute(hwnd_, DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
    DwmSetWindowAttribute(hwnd_, DWMWA_BORDER_COLOR, &border, sizeof(border));
    DwmSetWindowAttribute(hwnd_, DWMWA_TEXT_COLOR, &captionText, sizeof(captionText));

    model_.autostart = isAutostartEnabled();
    refreshModel();
    announceChanges();  // primeira leitura: só registra, sem avisos
    createTrayIcons();
    trayUpdate();
    checkFirewallAsync();
    if (visible) {
        ShowWindow(hwnd_, SW_SHOWNORMAL);
        UpdateWindow(hwnd_);
    }
    updateTimer();
    return true;
}

int MainWindow::run() {
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return int(msg.wParam);
}

LRESULT CALLBACK MainWindow::windowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        self = static_cast<MainWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->handle(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT MainWindow::handle(UINT msg, WPARAM wp, LPARAM lp) {
    if (taskbarCreatedMsg_ != 0 && msg == taskbarCreatedMsg_) {
        trayAdded_ = false;  // o Explorer reiniciou: o ícone sumiu e precisa ser recriado
        trayState_ = -1;
        trayUpdate();
        return 0;
    }
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd_, &ps);
            paint();
            EndPaint(hwnd_, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_SIZE:
            if (target_) target_->Resize(D2D1::SizeU(LOWORD(lp), HIWORD(lp)));
            updateTimer();
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case WM_DPICHANGED: {
            dpi_ = HIWORD(wp);
            const RECT* r = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            if (target_) target_->SetDpi(float(dpi_), float(dpi_));
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            RECT r{0, 0, MulDiv(int(kMinWidthDip), int(dpi_), 96), MulDiv(int(kMinHeightDip), int(dpi_), 96)};
            AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi_);
            mmi->ptMinTrackSize = {r.right - r.left, r.bottom - r.top};
            return 0;
        }
        case WM_TIMER:
            if (wp == kTimerId) onTimer();
            return 0;
        case WM_MOUSEMOVE:
            onMouseMove(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            return 0;
        case WM_MOUSELEAVE:
            trackingMouse_ = false;
            if (hover_.action != UiAction::None) {
                hover_ = {};
                InvalidateRect(hwnd_, nullptr, FALSE);
            }
            return 0;
        case WM_SETCURSOR:
            if (LOWORD(lp) == HTCLIENT && hover_.action != UiAction::None) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            break;
        case WM_LBUTTONUP:
            onMouseMove(GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            if (hover_.action != UiAction::None) runAction(hover_);
            return 0;
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) {
                hideToTray();
                return 0;
            }
            break;
        case WM_CLOSE:
            hideToTray();  // o "X" só esconde: o servidor continua rodando na bandeja
            return 0;
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case kMenuOpen: show(); break;
                case kMenuAutostart: runAction({UiAction::Autostart}); break;
                case kMenuFirewall: runAction({UiAction::Firewall}); break;
                case kMenuReleaseGuide: runAction({UiAction::ReleaseGuide}); break;
                case kMenuExit: DestroyWindow(hwnd_); break;
                default: break;
            }
            return 0;
        case WM_APP_TRAY:
            switch (LOWORD(lp)) {
                case NIN_SELECT:
                case NIN_KEYSELECT:
                    show();
                    return 0;
                case WM_CONTEXTMENU:
                    trayMenu(GET_X_LPARAM(wp), GET_Y_LPARAM(wp));
                    return 0;
                default:
                    return 0;
            }
        case WM_APP_SHOW:
            show();
            return 0;
        case WM_APP_FIREWALL:
            model_.firewall = FirewallState(wp);
            if (lp) {
                firewallBusy_ = false;
                model_.firewallBusy = false;
            }
            InvalidateRect(hwnd_, nullptr, FALSE);
            return 0;
        case WM_QUERYENDSESSION:
            return TRUE;
        case WM_ENDSESSION:
            // Logoff/desligamento: o Windows encerra o processo logo depois; remove os
            // controles virtuais antes.
            if (wp) server_.stop();
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd_, kTimerId);
            trayRemove();
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

void MainWindow::paint() {
    if (!target_) {
        RECT rc;
        GetClientRect(hwnd_, &rc);
        const auto props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(),
                                                        float(dpi_), float(dpi_));
        const auto hwndProps = D2D1::HwndRenderTargetProperties(
            hwnd_, D2D1::SizeU(UINT32(rc.right), UINT32(rc.bottom)), D2D1_PRESENT_OPTIONS_IMMEDIATELY);
        if (FAILED(d2d_->CreateHwndRenderTarget(props, hwndProps, &target_))) return;
        renderer_.discardDeviceResources();
    }
    target_->BeginDraw();
    renderer_.draw(target_.Get(), model_, hover_, hits_);
    if (target_->EndDraw() == D2DERR_RECREATE_TARGET) {  // placa de vídeo reiniciou, troca de GPU...
        target_.Reset();
        renderer_.discardDeviceResources();
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void MainWindow::onMouseMove(int x, int y) {
    if (!trackingMouse_) {
        TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, hwnd_, 0};
        TrackMouseEvent(&t);
        trackingMouse_ = true;
    }
    const float fx = float(x) * 96.0f / float(dpi_);
    const float fy = float(y) * 96.0f / float(dpi_);
    UiTarget hit;
    for (const HitRegion& h : hits_) {
        if (fx >= h.rect.left && fx < h.rect.right && fy >= h.rect.top && fy < h.rect.bottom) {
            hit = h.target;
            break;
        }
    }
    if (hit != hover_) {
        hover_ = hit;
        InvalidateRect(hwnd_, nullptr, FALSE);
    }
}

void MainWindow::show() {
    ShowWindow(hwnd_, IsIconic(hwnd_) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(hwnd_);
    refreshModel();
    updateTimer();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::hideToTray() {
    ShowWindow(hwnd_, SW_HIDE);
    updateTimer();
    if (!readSetting(kHintSetting, 0)) {
        writeSetting(kHintSetting, 1);
        notify(L"Continua rodando perto do relógio",
               L"Clique no ícone para abrir a janela. Para fechar de vez: botão direito > Sair.");
    }
}

void MainWindow::updateTimer() {
    const bool visible = IsWindowVisible(hwnd_) && !IsIconic(hwnd_);
    const UINT ms = visible ? kTimerVisibleMs : kTimerHiddenMs;
    if (ms != timerMs_) {
        SetTimer(hwnd_, kTimerId, ms, nullptr);
        timerMs_ = ms;
    }
}

void MainWindow::onTimer() {
    refreshModel();
    announceChanges();
    trayUpdate();
    updateTimer();
    if (IsWindowVisible(hwnd_) && !IsIconic(hwnd_)) InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::refreshModel() {
    const uint64_t now = GetTickCount64();
    model_.port = server_.options().port;
    model_.driver = server_.driverState();
    model_.clients = server_.clients();
    model_.events = server_.recentEvents();
    model_.firewallBusy = firewallBusy_;
    if (model_.addresses.empty() || now - lastAddressRefresh_ >= 5000) {
        model_.addresses = localAddresses();
        lastAddressRefresh_ = now;
    }
    if (lastGuideCheck_ == 0 || now - lastGuideCheck_ >= kGuideCheckMs) {
        model_.guideReserved = isGuideReservedByGameBar();
        model_.guideHintDismissed = readSetting(kGuideHintSetting, 0) != 0;
        lastGuideCheck_ = now;
    }
    // Pacotes/s de cada celular, medidos em janelas de ~1 s.
    model_.packetRates.clear();
    for (const ClientView& c : model_.clients) {
        auto& base = rateBase_[c.slot];
        float& rate = rates_[c.slot];
        if (base.second == 0 || c.packets < base.first) {
            base = {c.packets, now};
            rate = 0.0f;
        } else if (now - base.second >= 1000) {
            rate = float(c.packets - base.first) * 1000.0f / float(now - base.second);
            base = {c.packets, now};
        }
        model_.packetRates.push_back(rate);
    }
}

void MainWindow::announceChanges() {
    std::map<int, std::pair<uint32_t, std::wstring>> current;
    for (const ClientView& c : model_.clients) {
        current[c.slot] = {c.clientId, utf8ToWide(c.name.empty() ? c.address : c.name)};
    }
    // Com a janela aberta os cartões já mostram tudo; os avisos são para quando ela está escondida.
    const bool announce = !firstRefresh_ && !(IsWindowVisible(hwnd_) && !IsIconic(hwnd_));
    if (announce) {
        for (const auto& [slot, info] : current) {
            const auto it = known_.find(slot);
            if (it != known_.end() && it->second.first == info.first) continue;
            std::wstring text = info.second;
            for (const ClientView& c : model_.clients) {
                if (c.slot == slot && c.player >= 0) text += L"  ·  Jogador " + std::to_wstring(c.player + 1);
            }
            notify(L"Celular conectado", text);
        }
        for (const auto& [slot, info] : known_) {
            const auto it = current.find(slot);
            if (it == current.end() || it->second.first != info.first) notify(L"Celular desconectado", info.second);
        }
    }
    known_ = std::move(current);
    firstRefresh_ = false;

    // Bateria fraca: avisa uma vez ao cruzar 15% (mesmo com a janela aberta) e rearma ao carregar.
    std::map<uint32_t, bool> warned;
    for (const ClientView& c : model_.clients) {
        const auto it = lowBatteryWarned_.find(c.clientId);
        bool done = it != lowBatteryWarned_.end() && it->second;
        if (phoneBatteryLow(c.phone)) {
            if (!done) {
                notify(L"Bateria fraca no celular",
                       utf8ToWide(c.name.empty() ? c.address : c.name) + L": " + std::to_wstring(c.phone.battery) +
                           L"%. Coloque para carregar para o controle não cair no meio do jogo.");
            }
            done = true;
        } else if (c.phone.known && (c.phone.charging || c.phone.battery >= kBatteryRearmPercent)) {
            done = false;
        }
        warned[c.clientId] = done;
    }
    lowBatteryWarned_ = std::move(warned);
}

void MainWindow::runAction(UiTarget target) {
    switch (target.action) {
        case UiAction::Firewall:
            if (!firewallBusy_) openFirewallAsync();
            break;
        case UiAction::Autostart:
            setAutostart(!model_.autostart);
            model_.autostart = isAutostartEnabled();
            break;
        case UiAction::DownloadDriver:
            openUrl(kDriverUrl);
            break;
        case UiAction::ReleaseGuide:
            if (releaseGuideFromGameBar()) {
                model_.guideReserved = isGuideReservedByGameBar();
                server_.logEvent("Botão Guide liberado para a Steam (a Xbox Game Bar não abre mais pelo controle)");
            } else {
                server_.logEvent("ERRO: não foi possível mudar a opção da Xbox Game Bar");
            }
            break;
        case UiAction::DismissGuide:
            writeSetting(kGuideHintSetting, 1);
            model_.guideHintDismissed = true;
            break;
        case UiAction::TestRumble:
            server_.testRumble(target.slot);
            break;
        default:
            break;
    }
    model_.events = server_.recentEvents();
    InvalidateRect(hwnd_, nullptr, FALSE);
}

void MainWindow::checkFirewallAsync() {
    const HWND hwnd = hwnd_;
    std::thread([hwnd] {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const FirewallState state = checkFirewall();
        CoUninitialize();
        PostMessageW(hwnd, WM_APP_FIREWALL, WPARAM(state), 0);
    }).detach();
}

void MainWindow::openFirewallAsync() {
    firewallBusy_ = true;
    model_.firewallBusy = true;
    const HWND hwnd = hwnd_;
    const uint16_t port = server_.options().port;
    std::thread([hwnd, port] {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        openFirewall(hwnd, port);  // mostra o UAC; espera o netsh terminar
        const FirewallState state = checkFirewall();
        CoUninitialize();
        PostMessageW(hwnd, WM_APP_FIREWALL, WPARAM(state), 1);
    }).detach();
}

// ------------------------------------------------------------------ bandeja

void MainWindow::createTrayIcons() {
    const UINT dpi = GetDpiForSystem();
    const int size = GetSystemMetricsForDpi(SM_CXSMICON, dpi);
    const TrayState states[] = {TrayState::Waiting, TrayState::Connected, TrayState::Warning, TrayState::Error};
    for (int i = 0; i < 4; ++i) trayIcons_[i] = createIcon(d2d_.Get(), wic_.Get(), size, stateColor(states[i]));
}

void MainWindow::trayAdd() {
    nid_ = {};
    nid_.cbSize = sizeof(nid_);
    nid_.hWnd = hwnd_;
    nid_.uID = kTrayId;
    nid_.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE | NIF_SHOWTIP;
    nid_.uCallbackMessage = WM_APP_TRAY;
    nid_.hIcon = trayIcons_[std::max(trayState_, 0)];
    wcsncpy_s(nid_.szTip, trayTip_.empty() ? L"JoyHub" : trayTip_.c_str(), _TRUNCATE);
    trayAdded_ = Shell_NotifyIconW(NIM_ADD, &nid_) != FALSE;
    if (trayAdded_) {
        nid_.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &nid_);
    }
}

void MainWindow::trayUpdate() {
    if (!trayAdded_) {
        trayAdd();  // no login a barra de tarefas pode ainda não existir: tenta de novo a cada tique
        if (!trayAdded_) return;
    }
    const int state = int(overallState(model_));
    const uint64_t now = GetTickCount64();
    if (state == trayState_ && now - lastTrayUpdate_ < 1000) return;

    std::wstring tip = L"JoyHub\n" + statusText(model_);
    for (const ClientView& c : model_.clients) {
        if (!c.timedOut && !c.paused && c.rttUs) {
            tip += L"  ·  ping " + formatDecimal(c.rttUs / 1000.0) + L" ms";
            if (c.phone.known && c.phone.battery <= 100) tip += L"  ·  bateria " + std::to_wstring(c.phone.battery) + L"%";
            break;
        }
    }
    if (model_.clients.empty() && !model_.addresses.empty()) tip += L"\nIP: " + utf8ToWide(model_.addresses[0].ip);
    lastTrayUpdate_ = now;
    if (state == trayState_ && tip == trayTip_) return;

    trayState_ = state;
    trayTip_ = tip;
    nid_.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    nid_.hIcon = trayIcons_[state];
    wcsncpy_s(nid_.szTip, tip.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid_);
}

void MainWindow::trayRemove() {
    if (!trayAdded_) return;
    Shell_NotifyIconW(NIM_DELETE, &nid_);
    trayAdded_ = false;
}

void MainWindow::trayMenu(int x, int y) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, kMenuOpen, L"Abrir");
    SetMenuDefaultItem(menu, kMenuOpen, FALSE);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING | (model_.autostart ? MF_CHECKED : MF_UNCHECKED), kMenuAutostart,
                L"Iniciar com o Windows");
    if (model_.firewall != FirewallState::Allowed) AppendMenuW(menu, MF_STRING, kMenuFirewall, L"Liberar no firewall…");
    if (model_.guideReserved) AppendMenuW(menu, MF_STRING, kMenuReleaseGuide, L"Liberar botão Guide para a Steam");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kMenuExit, L"Sair");
    SetForegroundWindow(hwnd_);  // sem isso o menu não fecha ao clicar fora
    const UINT align = GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN;
    TrackPopupMenuEx(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | align, x, y, hwnd_, nullptr);
    PostMessageW(hwnd_, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

void MainWindow::notify(const std::wstring& title, const std::wstring& text) {
    if (!trayAdded_) return;
    NOTIFYICONDATAW n = nid_;
    n.uFlags = NIF_INFO;
    wcsncpy_s(n.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(n.szInfo, text.c_str(), _TRUNCATE);
    n.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND;
    Shell_NotifyIconW(NIM_MODIFY, &n);
}

}  // namespace swc
