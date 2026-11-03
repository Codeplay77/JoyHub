#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "protocol.h"
#include "vigem.h"

namespace swc {

struct ServerOptions {
    uint16_t port = kDefaultPort;
    bool dryRun = false;       // sem driver: só recebe e mostra (para testar a rede)
    bool printEvents = false;  // imprime os eventos no stdout (modo --headless)
    bool printStates = false;  // imprime cada estado aplicado (diagnóstico; adiciona latência)
};

enum class DriverState { DryRun, Ready, Missing };

constexpr int kMaxClients = 4;              // limite do XInput
constexpr uint64_t kNeutralTimeoutMs = 500;  // sem pacotes por esse tempo = solta tudo
constexpr uint64_t kEvictAfterMs = 10000;    // slot parado pode ser reaproveitado
constexpr uint64_t kReleaseAfterMs = 120000; // celular sumiu sem BYE: remove o controle virtual
constexpr uint64_t kClosedSessionMs = 3000;  // ignora pacotes atrasados de uma sessão que mandou BYE
constexpr uint64_t kPlugRetryMs = 3000;
constexpr uint64_t kDriverRetryMs = 3000;    // sem ViGEmBus: procura o driver de novo (instalação ao vivo)
constexpr int kLossWindowPings = 10;         // perda medida nos últimos ~5 s (um PING a cada 500 ms)

// Estado do celular, mandado na extensão do PING (apps antigos não mandam).
struct PhoneInfo {
    bool known = false;
    uint8_t battery = kUnknownPercent;  // 0-100
    bool charging = false;
    bool powerSave = false;
    uint8_t wifiGeneration = 0;  // 4, 5, 6, 7; 0 = desconhecida
    uint16_t wifiMhz = 0;        // 0 = sem Wi-Fi
    int8_t wifiDbm = 0;          // 0 = desconhecido
    uint16_t linkMbps = 0;
    uint8_t padBattery = kUnknownPercent;
    bool operator==(const PhoneInfo&) const = default;
};

struct ClientView {
    int slot = 0;
    uint32_t clientId = 0;
    bool padActive = false;
    std::string name;
    std::string address;
    int player = -1;
    uint32_t rttUs = 0;
    uint64_t packets = 0, applied = 0, lost = 0, recovered = 0;
    uint64_t silentMs = 0;
    bool timedOut = false;
    bool paused = false;
    uint8_t largeMotor = 0, smallMotor = 0;
    PadState state;
    PhoneInfo phone;
    int lossBasisPoints = -1;  // perda celular -> PC nos últimos ~5 s, em centésimos de %; -1 = sem dados
    bool rumbleTest = false;
};

class Server {
public:
    explicit Server(ServerOptions options);
    ~Server();
    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    bool start(std::string* error);
    void stop();

    std::vector<ClientView> clients();
    std::vector<std::string> recentEvents();
    const ServerOptions& options() const { return options_; }
    DriverState driverState() const { return DriverState(driverState_.load(std::memory_order_relaxed)); }

    // Dois pulsos curtos de vibração no celular do slot (pode ser chamado de qualquer thread).
    void testRumble(int slot);
    void logEvent(std::string text);

private:
    struct Client {
        // Escritos apenas pela thread de recepção. Os marcados com (m) também são
        // lidos por outras threads e só mudam com mutex_ travado.
        bool active = false;          // (m)
        uint32_t clientId = 0;        // (m)
        uint32_t session = 0;         // (m)
        bool hasSession = false;
        sockaddr_in addr{};           // (m)
        std::string name;             // (m)
        std::string addressText;      // (m)
        uint32_t lastSeq = 0;
        bool hasSeq = false;
        bool neutral = true;
        bool reapplyNewest = false;   // após timeout, aceita de novo o último seq
        uint64_t lastRecvMs = 0;
        X360Pad pad;

        std::atomic<uint64_t> packets{0}, applied{0}, lost{0}, recovered{0};
        std::atomic<uint64_t> lastRecvShared{0};
        std::atomic<uint32_t> rttUs{0};
        std::atomic<uint8_t> largeMotor{0}, smallMotor{0};
        std::atomic<int> player{-1};
        std::atomic<bool> timedOut{false};
        std::atomic<bool> paused{false};
        std::atomic<bool> hasPad{false};
        std::atomic<uint64_t> stateLo{0}, stateHi{0};  // cópia para exibição
        PhoneInfo phone;                               // (m)

        // Perda de pacotes: (enviados pelo celular, recebidos aqui) a cada PING.
        uint64_t sessionRecv = 0;
        std::array<std::pair<uint32_t, uint32_t>, kLossWindowPings + 1> lossSamples{};
        int lossCount = 0;
        int lossPos = 0;
        std::atomic<int> lossBasisPoints{-1};

        // Teste de vibração: a janela marca o início; a thread de recepção toca os pulsos.
        std::atomic<uint64_t> rumbleTestStartMs{0};
        uint64_t rumbleTestSeen = 0;  // início do teste que já está tocando
        bool rumbleTestOn = false;

        // Só a thread de notificação do controle usa.
        int sentLarge = -1, sentSmall = -1;
    };

    struct ClosedSession {
        uint32_t clientId = 0;
        uint32_t session = 0;
        uint64_t atMs = 0;
    };

    void recvLoop();
    void handlePacket(const uint8_t* p, size_t len, const sockaddr_in& from, uint64_t now);
    void handleInput(Client& c, const uint8_t* p, size_t len);
    void handlePing(Client& c, const uint8_t* p, size_t len, const sockaddr_in& from);
    void readPhoneExtension(Client& c, const uint8_t* ext, size_t len);
    void sendDiscoverReply(const sockaddr_in& to);
    void applyState(Client& c, const PadState& s);
    void maintenance(uint64_t now);
    void runRumbleTest(Client& c, uint64_t now);
    void sendRumble(Client& c, uint8_t large, uint8_t weak, uint8_t flags);
    void resetSessionStats(Client& c);
    Client* findClient(uint32_t clientId);
    Client* acquireClient(uint32_t clientId, const sockaddr_in& from, uint64_t now);
    void releaseClient(Client& c, const char* reason);
    bool isClosedSession(uint32_t clientId, uint32_t session, uint64_t now) const;
    bool tryOpenDriver(uint64_t now);
    void ensurePad(Client& c, uint64_t now);
    void onFeedback(Client& c, uint8_t large, uint8_t weak, uint8_t led);

    // Declarado antes de clients_: os controles virtuais são destruídos antes do barramento.
    VigemBus bus_;
    std::atomic<int> driverState_{int(DriverState::Missing)};
    uint64_t nextDriverRetryMs_ = 0;
    ServerOptions options_;
    SOCKET sock_ = INVALID_SOCKET;
    std::thread recvThread_;
    std::atomic<bool> running_{false};
    std::array<Client, kMaxClients> clients_;
    std::array<ClosedSession, 8> closed_;  // só a thread de recepção usa
    size_t closedNext_ = 0;
    std::mutex mutex_;
    std::mutex eventsMutex_;
    std::deque<std::string> events_;
    std::string hostName_;
    uint64_t nextPlugAttemptMs_ = 0;
};

std::string formatAddress(const sockaddr_in& a);

}  // namespace swc
