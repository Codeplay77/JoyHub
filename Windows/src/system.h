// Integração com o Windows: rede, inicialização automática, firewall, configurações.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

namespace swc {

// Leva o "Iniciar com o Windows" da versão SteamWifiController para o JoyHub.
void migrateAutostart();

struct LocalAddress {
    std::string adapter;
    std::string ip;
};

// IPv4 das placas de rede ativas (para digitar no celular).
std::vector<LocalAddress> localAddresses();

std::wstring utf8ToWide(const std::string& s);
std::wstring exePath();

// "Iniciar com o Windows": entrada em HKCU\...\Run que abre o programa direto na bandeja.
bool isAutostartEnabled();
bool setAutostart(bool enabled);

// Valores simples em HKCU\Software\SteamWifiController.
DWORD readSetting(const wchar_t* name, DWORD fallback);
void writeSetting(const wchar_t* name, DWORD value);

enum class FirewallState { Unknown, Allowed, NotAllowed, Blocked };

// Lê as regras do Firewall do Windows (COM; leva até alguns décimos de segundo).
// A thread precisa ter COM inicializado.
FirewallState checkFirewall();

// Pede permissão (UAC) e libera a porta UDP para a rede local, removendo bloqueios
// antigos deste programa. Bloqueia até terminar; false se o usuário cancelar.
bool openFirewall(HWND owner, uint16_t port);

void openUrl(const wchar_t* url);

// Xbox Game Bar: com "Abrir a Xbox Game Bar usando este botão no controle" ligado (o padrão),
// o Windows esconde o botão Xbox/Guide dos outros programas, inclusive do XInputGetStateEx.
// A Steam então não recebe o Guide do controle virtual. Sem a chave da Game Bar: false.
bool isGuideReservedByGameBar();
// Desliga essa opção (HKCU\Software\Microsoft\GameBar\UseNexusForGameBarEnabled = 0). Vale na hora.
bool releaseGuideFromGameBar();

}  // namespace swc
