#include "system.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <netfw.h>
#include <shellapi.h>
#include <wrl/client.h>

#include <cstring>
#include <cwchar>

namespace swc {
namespace {

using Microsoft::WRL::ComPtr;

constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValue[] = L"JoyHub";
constexpr wchar_t kLegacyRunValue[] = L"SteamWifiController";
// Chave do nome antigo: preserva as configurações salvas antes da mudança para JoyHub.
constexpr wchar_t kSettingsKey[] = L"Software\\SteamWifiController";
constexpr wchar_t kFirewallRuleName[] = L"JoyHub";
constexpr wchar_t kLegacyFirewallRuleName[] = L"SteamWifiController";
constexpr wchar_t kGameBarKey[] = L"Software\\Microsoft\\GameBar";
constexpr wchar_t kGameBarGuideValue[] = L"UseNexusForGameBarEnabled";

std::string toUtf8(const wchar_t* w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string s(size_t(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

bool containsNoCase(const std::wstring& haystack, const std::wstring& needle) {
    if (needle.empty() || haystack.size() < needle.size()) return false;
    for (size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        if (_wcsnicmp(haystack.c_str() + i, needle.c_str(), needle.size()) == 0) return true;
    }
    return false;
}

}  // namespace

std::vector<LocalAddress> localAddresses() {
    std::vector<LocalAddress> out;
    ULONG size = 32 * 1024;
    std::vector<uint8_t> buf(size);
    const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    ULONG r = GetAdaptersAddresses(AF_INET, flags, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &size);
    if (r == ERROR_BUFFER_OVERFLOW) {
        buf.resize(size);
        r = GetAdaptersAddresses(AF_INET, flags, nullptr, reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()), &size);
    }
    if (r != NO_ERROR) return out;
    for (auto* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data()); a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        for (auto* u = a->FirstUnicastAddress; u; u = u->Next) {
            const auto* sin = reinterpret_cast<const sockaddr_in*>(u->Address.lpSockaddr);
            if (sin->sin_family != AF_INET) continue;
            char ip[INET_ADDRSTRLEN] = {};
            inet_ntop(AF_INET, &sin->sin_addr, ip, sizeof(ip));
            if (std::strncmp(ip, "169.254.", 8) == 0) continue;  // sem DHCP, inútil
            out.push_back({toUtf8(a->FriendlyName), ip});
        }
    }
    return out;
}

std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

std::wstring exePath() {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, path.data(), DWORD(path.size()));
        if (n < path.size()) {
            path.resize(n);
            return path;
        }
        path.resize(path.size() * 2);
    }
}

bool isAutostartEnabled() {
    wchar_t buf[2048];
    DWORD size = sizeof(buf);
    if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, RRF_RT_REG_SZ, nullptr, buf, &size) != ERROR_SUCCESS) {
        return false;
    }
    return containsNoCase(buf, exePath());  // entrada antiga de outra pasta não conta
}

bool setAutostart(bool enabled) {
    if (enabled) {
        const std::wstring cmd = L"\"" + exePath() + L"\" --tray";
        if (RegSetKeyValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, REG_SZ, cmd.c_str(),
                           DWORD((cmd.size() + 1) * sizeof(wchar_t))) != ERROR_SUCCESS) return false;
        const LSTATUS legacy = RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, kLegacyRunValue);
        return legacy == ERROR_SUCCESS || legacy == ERROR_FILE_NOT_FOUND;
    }
    const LSTATUS r = RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, kRunValue);
    const LSTATUS legacy = RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, kLegacyRunValue);
    return (r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND) &&
           (legacy == ERROR_SUCCESS || legacy == ERROR_FILE_NOT_FOUND);
}

void migrateAutostart() {
    // Só migra quem já tinha ativado; nunca liga a inicialização numa instalação nova.
    wchar_t oldCommand[2048]{};
    DWORD size = sizeof(oldCommand);
    if (RegGetValueW(HKEY_CURRENT_USER, kRunKey, kLegacyRunValue, RRF_RT_REG_SZ, nullptr,
                     oldCommand, &size) != ERROR_SUCCESS) return;
    if (!containsNoCase(oldCommand, L"SteamWifiController.exe")) return;
    wchar_t currentCommand[2048]{};
    size = sizeof(currentCommand);
    const LSTATUS current = RegGetValueW(HKEY_CURRENT_USER, kRunKey, kRunValue, RRF_RT_REG_SZ,
                                         nullptr, currentCommand, &size);
    if (current == ERROR_FILE_NOT_FOUND) setAutostart(true);
    else if (current == ERROR_SUCCESS && containsNoCase(currentCommand, exePath()))
        RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, kLegacyRunValue);
}

DWORD readSetting(const wchar_t* name, DWORD fallback) {
    DWORD value = 0;
    DWORD size = sizeof(value);
    if (RegGetValueW(HKEY_CURRENT_USER, kSettingsKey, name, RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS) {
        return value;
    }
    return fallback;
}

void writeSetting(const wchar_t* name, DWORD value) {
    RegSetKeyValueW(HKEY_CURRENT_USER, kSettingsKey, name, REG_DWORD, &value, sizeof(value));
}

FirewallState checkFirewall() {
    ComPtr<INetFwPolicy2> policy;
    if (FAILED(CoCreateInstance(__uuidof(NetFwPolicy2), nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&policy)))) {
        return FirewallState::Unknown;
    }
    long profiles = 0;
    if (FAILED(policy->get_CurrentProfileTypes(&profiles))) return FirewallState::Unknown;

    bool enabled = false;
    for (NET_FW_PROFILE_TYPE2 p : {NET_FW_PROFILE2_DOMAIN, NET_FW_PROFILE2_PRIVATE, NET_FW_PROFILE2_PUBLIC}) {
        VARIANT_BOOL on = VARIANT_FALSE;
        if ((profiles & p) && SUCCEEDED(policy->get_FirewallEnabled(p, &on)) && on) enabled = true;
    }
    if (!enabled) return FirewallState::Allowed;  // firewall desligado na rede atual

    ComPtr<INetFwRules> rules;
    ComPtr<IUnknown> enumUnknown;
    ComPtr<IEnumVARIANT> it;
    if (FAILED(policy->get_Rules(&rules)) || FAILED(rules->get__NewEnum(&enumUnknown)) || FAILED(enumUnknown.As(&it))) {
        return FirewallState::Unknown;
    }

    const std::wstring exe = exePath();
    bool allowed = false;
    bool blocked = false;
    VARIANT v;
    VariantInit(&v);
    ULONG fetched = 0;
    while (it->Next(1, &v, &fetched) == S_OK) {
        ComPtr<INetFwRule> rule;
        if (v.vt == VT_DISPATCH && v.pdispVal) v.pdispVal->QueryInterface(IID_PPV_ARGS(&rule));
        VariantClear(&v);
        if (!rule) continue;

        VARIANT_BOOL ruleEnabled = VARIANT_FALSE;
        NET_FW_RULE_DIRECTION dir = NET_FW_RULE_DIR_OUT;
        long ruleProfiles = 0;
        long protocol = 0;
        NET_FW_ACTION action = NET_FW_ACTION_BLOCK;
        rule->get_Enabled(&ruleEnabled);
        rule->get_Direction(&dir);
        rule->get_Profiles(&ruleProfiles);
        rule->get_Protocol(&protocol);
        rule->get_Action(&action);
        if (!ruleEnabled || dir != NET_FW_RULE_DIR_IN || !(ruleProfiles & profiles)) continue;
        if (protocol != NET_FW_IP_PROTOCOL_UDP && protocol != NET_FW_IP_PROTOCOL_ANY) continue;

        BSTR name = nullptr;
        BSTR app = nullptr;
        rule->get_Name(&name);
        rule->get_ApplicationName(&app);
        const bool ours = name && (_wcsicmp(name, kFirewallRuleName) == 0 ||
                                   _wcsicmp(name, kLegacyFirewallRuleName) == 0);
        const bool sameApp = app && _wcsicmp(app, exe.c_str()) == 0;
        SysFreeString(name);
        SysFreeString(app);

        // "Cancelar" no aviso do firewall cria uma regra de bloqueio, que vence as de permissão.
        if (action == NET_FW_ACTION_BLOCK && sameApp) blocked = true;
        if (action == NET_FW_ACTION_ALLOW && (ours || sameApp)) allowed = true;
    }
    if (blocked) return FirewallState::Blocked;
    return allowed ? FirewallState::Allowed : FirewallState::NotAllowed;
}

bool openFirewall(HWND owner, uint16_t port) {
    const std::wstring exe = exePath();
    // Uma única elevação: remove regras de entrada antigas deste .exe (inclusive bloqueios)
    // e cria a regra da porta UDP, só para aparelhos da rede local.
    wchar_t params[4096];
    swprintf_s(params,
               L"/s /c \"netsh advfirewall firewall delete rule name=all dir=in program=\"%s\" >nul 2>&1 & "
               L"netsh advfirewall firewall delete rule name=\"%s\" >nul 2>&1 & "
               L"netsh advfirewall firewall add rule name=\"%s\" dir=in action=allow protocol=UDP localport=%u "
               L"remoteip=localsubnet profile=any\"",
               exe.c_str(), kFirewallRuleName, kFirewallRuleName, unsigned(port));

    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    sei.hwnd = owner;
    sei.lpVerb = L"runas";
    sei.lpFile = L"cmd.exe";
    sei.lpParameters = params;
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei)) return false;  // UAC cancelado
    DWORD code = 1;
    if (sei.hProcess) {
        WaitForSingleObject(sei.hProcess, 30000);
        GetExitCodeProcess(sei.hProcess, &code);
        CloseHandle(sei.hProcess);
    }
    return code == 0;
}

void openUrl(const wchar_t* url) {
    ShellExecuteW(nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
}

bool isGuideReservedByGameBar() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kGameBarKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
    DWORD value = 1;
    DWORD size = sizeof(value);
    const LSTATUS r = RegGetValueW(key, nullptr, kGameBarGuideValue, RRF_RT_REG_DWORD, nullptr, &value, &size);
    RegCloseKey(key);
    return r != ERROR_SUCCESS || value != 0;  // sem o valor vale o padrão do Windows: ligado
}

bool releaseGuideFromGameBar() {
    const DWORD off = 0;
    return RegSetKeyValueW(HKEY_CURRENT_USER, kGameBarKey, kGameBarGuideValue, REG_DWORD, &off, sizeof(off)) ==
           ERROR_SUCCESS;
}

}  // namespace swc
