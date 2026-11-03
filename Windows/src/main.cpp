// JoyHub - servidor do PC.
// Recebe o estado do controle do celular via UDP e o injeta num controle Xbox 360
// virtual (ViGEmBus), que a Steam e os jogos enxergam como um controle real.
// Roda com janela própria e ícone na área de notificação, sem console.
#include "server.h"
#include "system.h"
#include "ui.h"
#include "window.h"

#include <objbase.h>
#include <shellapi.h>

#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <string>

using namespace swc;

namespace {

struct AppOptions {
    ServerOptions server;
    bool headless = false;
    bool startInTray = false;
    std::wstring snapshotPath;
    int snapshotClients = 2;
    float snapshotWidth = kWindowWidthDip;
    float snapshotHeight = kWindowHeightDip;
    float snapshotScale = 1.25f;
    int snapshotDriver = -1;
    bool snapshotGuide = false;
};

constexpr wchar_t kUsage[] =
    L"Uso: JoyHub.exe [opções]\n\n"
    L"  --tray        abre direto perto do relógio, sem mostrar a janela\n"
    L"  --port N      porta UDP (padrão 38500)\n"
    L"  --dry-run     testa a rede sem o driver ViGEmBus (não cria controle virtual)\n"
    L"  --headless    sem janela: mostra os eventos num console (diagnóstico)\n"
    L"  --verbose     com --headless, imprime cada estado recebido (adiciona latência)\n"
    L"  --snapshot PNG [0..4]  captura a interface com dados de exemplo, sem iniciar o servidor\n"
    L"  --snapshot-size W H   tamanho da captura em DIPs (mínimo 760 x 640)\n"
    L"  --snapshot-scale N    escala de DPI da captura (1 a 3)\n"
    L"  --snapshot-state ready|missing|test|guide   estado do driver na captura\n"
    L"                (guide: driver pronto e o aviso do botão Guide preso na Xbox Game Bar)\n";

std::atomic<bool> g_quit{false};
HANDLE g_cleanupDone = nullptr;

BOOL WINAPI onConsoleCtrl(DWORD type) {
    g_quit = true;
    if (type == CTRL_CLOSE_EVENT || type == CTRL_LOGOFF_EVENT || type == CTRL_SHUTDOWN_EVENT) {
        // O Windows encerra o processo assim que este handler retorna:
        // espera a limpeza (remover os controles virtuais) terminar.
        WaitForSingleObject(g_cleanupDone, 3000);
    }
    return TRUE;
}

void enableDpiAwareness() {
    using SetContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
    const auto fn = reinterpret_cast<SetContextFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext"));
    if (fn && fn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return;
    SetProcessDPIAware();
}

void tuneProcess() {
    // A thread de recepção já roda em THREAD_PRIORITY_TIME_CRITICAL (nível 15 em qualquer
    // classe), então o processo fica em prioridade normal e a janela não disputa CPU com o jogo.
    // Só impede o Windows de pôr o processo em "modo de eficiência" (EcoQoS) em segundo plano.
    PROCESS_POWER_THROTTLING_STATE throttling{};
    throttling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    throttling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    throttling.StateMask = 0;
    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &throttling, sizeof(throttling));
}

bool parseArgs(AppOptions& opt) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return false;
    bool ok = true;
    for (int i = 1; i < argc && ok; ++i) {
        const std::wstring a = argv[i];
        if ((a == L"--port" || a == L"-p") && i + 1 < argc) {
            const long port = std::wcstol(argv[++i], nullptr, 10);
            ok = port > 0 && port <= 65535;
            opt.server.port = uint16_t(port);
        } else if (a == L"--dry-run" || a == L"--teste") {
            opt.server.dryRun = true;
        } else if (a == L"--tray" || a == L"--bandeja") {
            opt.startInTray = true;
        } else if (a == L"--headless") {
            opt.headless = true;
        } else if (a == L"--verbose" || a == L"-v") {
            opt.server.printStates = true;
        } else if (a == L"--snapshot" && i + 1 < argc) {
            opt.snapshotPath = argv[++i];
            if (i + 1 < argc && std::iswdigit(argv[i + 1][0])) opt.snapshotClients = _wtoi(argv[++i]);
        } else if (a == L"--snapshot-size" && i + 2 < argc) {
            opt.snapshotWidth = float(_wtoi(argv[++i]));
            opt.snapshotHeight = float(_wtoi(argv[++i]));
            ok = opt.snapshotWidth >= kMinWidthDip && opt.snapshotWidth <= 3840 &&
                 opt.snapshotHeight >= kMinHeightDip && opt.snapshotHeight <= 2160;
        } else if (a == L"--snapshot-scale" && i + 1 < argc) {
            opt.snapshotScale = float(_wtof(argv[++i]));
            ok = opt.snapshotScale >= 1 && opt.snapshotScale <= 3;
        } else if (a == L"--snapshot-state" && i + 1 < argc) {
            const std::wstring state = argv[++i];
            if (state == L"ready") opt.snapshotDriver = int(DriverState::Ready);
            else if (state == L"missing") opt.snapshotDriver = int(DriverState::Missing);
            else if (state == L"test") opt.snapshotDriver = int(DriverState::DryRun);
            else if (state == L"guide") {
                opt.snapshotDriver = int(DriverState::Ready);
                opt.snapshotGuide = true;
            } else {
                ok = false;
            }
        } else {
            ok = false;
        }
    }
    LocalFree(argv);
    return ok;
}

// Sem janela: usa o console de quem chamou (ou abre um) para mostrar os eventos.
void attachConsoleOutput() {
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out == nullptr || out == INVALID_HANDLE_VALUE) {
        if (AttachConsole(ATTACH_PARENT_PROCESS) || AllocConsole()) {
            FILE* f = nullptr;
            freopen_s(&f, "CONOUT$", "w", stdout);
            freopen_s(&f, "CONOUT$", "w", stderr);
        }
    }
    setvbuf(stdout, nullptr, _IONBF, 0);
    SetConsoleOutputCP(CP_UTF8);
}

int runHeadless(const AppOptions& opt) {
    attachConsoleOutput();
    g_cleanupDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    SetConsoleCtrlHandler(onConsoleCtrl, TRUE);

    ServerOptions so = opt.server;
    so.printEvents = true;
    Server server(so);
    std::string error;
    if (!server.start(&error)) {
        std::printf("ERRO: %s\n", error.c_str());
        return 1;
    }
    std::printf("JoyHub ouvindo na porta UDP %u%s\n", unsigned(so.port),
                so.dryRun ? " (modo teste, sem driver)" : "");
    for (const LocalAddress& a : localAddresses()) std::printf("  IP: %s (%s)\n", a.ip.c_str(), a.adapter.c_str());

    while (!g_quit) Sleep(100);
    server.stop();
    std::printf("Encerrado.\n");
    SetEvent(g_cleanupDone);
    return 0;
}

int runGui(HINSTANCE instance, const AppOptions& opt) {
    // Uma instância só: abrir de novo traz para a frente a janela que já está rodando.
    // O nome antigo do mutex também impede rodar ao lado da versão anterior do app.
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\SteamWifiController.Instance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        const HWND other = FindWindowW(kWindowClass, nullptr);
        if (other && !opt.startInTray) {
            DWORD pid = 0;
            GetWindowThreadProcessId(other, &pid);
            AllowSetForegroundWindow(pid);
            PostMessageW(other, WM_APP_SHOW, 0, 0);
        }
        if (mutex) CloseHandle(mutex);
        return 0;
    }

    migrateAutostart();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    ServerOptions so = opt.server;
    so.printEvents = false;  // sem console: os eventos aparecem na janela
    so.printStates = false;
    Server server(so);
    std::string error;
    int code = 1;
    if (!server.start(&error)) {
        MessageBoxW(nullptr, utf8ToWide("Não foi possível iniciar o servidor:\n\n" + error).c_str(),
                    L"JoyHub", MB_ICONERROR);
    } else {
        MainWindow window(server, instance);
        if (window.create(!opt.startInTray)) code = window.run();
        server.stop();
    }
    CoUninitialize();
    if (mutex) CloseHandle(mutex);
    return code;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    enableDpiAwareness();
    AppOptions opt;
    if (!parseArgs(opt)) {
        MessageBoxW(nullptr, kUsage, L"JoyHub", MB_ICONINFORMATION);
        return 2;
    }

    if (!opt.snapshotPath.empty()) {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        const bool ok = renderSnapshot(opt.snapshotPath, opt.snapshotClients, opt.snapshotWidth, opt.snapshotHeight,
                                       opt.snapshotScale, opt.snapshotDriver, opt.snapshotGuide);
        CoUninitialize();
        return ok ? 0 : 1;
    }

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;
    tuneProcess();
    const int code = opt.headless ? runHeadless(opt) : runGui(instance, opt);
    WSACleanup();
    return code;
}
