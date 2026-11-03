#include "server.h"

#include <mswsock.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace swc {
namespace {

// Teste de vibração: dois pulsos (0-250 ms e 450-700 ms) e depois volta ao que o jogo pediu.
constexpr uint64_t kRumbleTestMs = 700;
constexpr uint8_t kRumbleTestStrength = 220;

bool rumbleTestPulseOn(uint64_t t) { return t < 250 || t >= 450; }

uint64_t nowMs() { return GetTickCount64(); }

std::string wideToUtf8(const wchar_t* w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string s(size_t(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
    return s;
}

std::string computerName() {
    wchar_t buf[256];
    DWORD size = 256;
    if (GetComputerNameExW(ComputerNamePhysicalDnsHostname, buf, &size)) return wideToUtf8(buf);
    return "PC";
}

void packState(const PadState& s, uint64_t& lo, uint64_t& hi) {
    lo = uint64_t(s.buttons) | (uint64_t(s.leftTrigger) << 16) | (uint64_t(s.rightTrigger) << 24) |
         (uint64_t(uint16_t(s.thumbLX)) << 32) | (uint64_t(uint16_t(s.thumbLY)) << 48);
    hi = uint64_t(uint16_t(s.thumbRX)) | (uint64_t(uint16_t(s.thumbRY)) << 16);
}

PadState unpackState(uint64_t lo, uint64_t hi) {
    PadState s;
    s.buttons = uint16_t(lo);
    s.leftTrigger = uint8_t(lo >> 16);
    s.rightTrigger = uint8_t(lo >> 24);
    s.thumbLX = int16_t(uint16_t(lo >> 32));
    s.thumbLY = int16_t(uint16_t(lo >> 48));
    s.thumbRX = int16_t(uint16_t(hi));
    s.thumbRY = int16_t(uint16_t(hi >> 16));
    return s;
}

}  // namespace

std::string formatAddress(const sockaddr_in& a) {
    char ip[INET_ADDRSTRLEN] = {};
    inet_ntop(AF_INET, &a.sin_addr, ip, sizeof(ip));
    return ip;
}

Server::Server(ServerOptions options) : options_(options), hostName_(computerName()) {
    if (options_.dryRun) driverState_ = int(DriverState::DryRun);
}

Server::~Server() { stop(); }

bool Server::start(std::string* error) {
    sock_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock_ == INVALID_SOCKET) {
        *error = "socket() falhou (erro " + std::to_string(WSAGetLastError()) + ")";
        return false;
    }

    // Sem isso, um ICMP "porta inalcançável" (app do celular fechado) faz o recvfrom falhar.
    BOOL connReset = FALSE;
    DWORD bytes = 0;
    WSAIoctl(sock_, SIO_UDP_CONNRESET, &connReset, sizeof(connReset), nullptr, 0, &bytes, nullptr, nullptr);

    BOOL exclusive = TRUE;
    setsockopt(sock_, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
    int rcvBuf = 256 * 1024;
    setsockopt(sock_, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<const char*>(&rcvBuf), sizeof(rcvBuf));
    DWORD timeoutMs = 20;  // acorda periodicamente para checar timeouts
    setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(options_.port);
    if (bind(sock_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        *error = "não foi possível abrir a porta UDP " + std::to_string(options_.port) + " (erro " +
                 std::to_string(WSAGetLastError()) + "). Já existe outra instância aberta?";
        closesocket(sock_);
        sock_ = INVALID_SOCKET;
        return false;
    }

    if (!options_.dryRun && !tryOpenDriver(GetTickCount64())) {
        logEvent(std::string("Driver ViGEmBus: ") + bus_.lastError() + ". Procurando de novo a cada 3 s");
    }

    running_ = true;
    recvThread_ = std::thread(&Server::recvLoop, this);
    return true;
}

void Server::stop() {
    if (!running_.exchange(false)) return;
    if (recvThread_.joinable()) recvThread_.join();
    for (auto& c : clients_) {
        if (c.pad.isPlugged()) {
            c.pad.submit(PadState{});
            c.pad.unplug();
            c.hasPad = false;
        }
    }
    closesocket(sock_);
    sock_ = INVALID_SOCKET;
}

void Server::recvLoop() {
    // A thread passa quase todo o tempo bloqueada; prioridade máxima só reduz o
    // tempo entre o pacote chegar e o estado ir para o driver.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    alignas(8) uint8_t buf[2048];
    uint64_t lastMaintenance = 0;
    while (running_) {
        sockaddr_in from{};
        int fromLen = sizeof(from);
        const int n = recvfrom(sock_, reinterpret_cast<char*>(buf), sizeof(buf), 0,
                               reinterpret_cast<sockaddr*>(&from), &fromLen);
        const uint64_t now = nowMs();
        if (n > 0 && from.sin_family == AF_INET) {
            handlePacket(buf, size_t(n), from, now);
        } else if (n == SOCKET_ERROR) {
            const int err = WSAGetLastError();
            if (err != WSAETIMEDOUT && err != WSAECONNRESET && err != WSAEMSGSIZE) Sleep(1);
        }
        if (now - lastMaintenance >= 20) {
            maintenance(now);
            lastMaintenance = now;
        }
    }
}

void Server::handlePacket(const uint8_t* p, size_t len, const sockaddr_in& from, uint64_t now) {
    if (!validHeader(p, len)) return;
    const uint8_t type = p[3];
    if (type == kTypeDiscover) {
        sendDiscoverReply(from);
        return;
    }
    if (type != kTypeInput && type != kTypePing && type != kTypeBye) return;
    if (len < kHeaderSize + 4) return;

    const uint32_t clientId = rd32(p + 4);
    const uint32_t session = rd32(p + 8);
    Client* c = findClient(clientId);

    if (type == kTypeBye) {
        if (c && c->hasSession && c->session == session) {
            closed_[closedNext_] = {clientId, session, now};
            closedNext_ = (closedNext_ + 1) % closed_.size();
            releaseClient(*c, "desconectou");
        }
        return;
    }
    if (!c) {
        if (isClosedSession(clientId, session, now)) return;  // pacote atrasado depois do BYE
        c = acquireClient(clientId, from, now);
        if (!c) return;
    }

    const bool addrChanged =
        c->addr.sin_addr.s_addr != from.sin_addr.s_addr || c->addr.sin_port != from.sin_port;
    if (!c->hasSession || c->session != session || addrChanged) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!c->hasSession || c->session != session) {
            c->session = session;
            c->hasSession = true;
            c->hasSeq = false;  // app reconectou: sequência e estatísticas recomeçam
            resetSessionStats(*c);
        }
        c->addr = from;
        c->addressText = formatAddress(from);
    }

    c->lastRecvMs = now;
    c->lastRecvShared.store(now, std::memory_order_relaxed);
    c->packets.fetch_add(1, std::memory_order_relaxed);
    ++c->sessionRecv;

    if (type == kTypeInput) {
        handleInput(*c, p, len);
    } else {
        handlePing(*c, p, len, from);
    }
}

void Server::handleInput(Client& c, const uint8_t* p, size_t len) {
    if (len < kInputFixedSize) return;
    const uint32_t seq = rd32(p + 12);
    const int count = p[16];
    if (count < 1 || count > kMaxStatesPerPacket || len < kInputFixedSize + size_t(count) * kStateSize) return;

    const bool hadSeq = c.hasSeq;
    if (hadSeq) {
        const int32_t ahead = int32_t(seq - c.lastSeq);
        if (ahead < 0 || (ahead == 0 && !c.reapplyNewest)) return;  // atrasado ou repetido (heartbeat)
        if (ahead > count) c.lost.fetch_add(uint64_t(ahead - count), std::memory_order_relaxed);
    }

    // Aplica do mais antigo para o mais novo tudo que ainda não foi aplicado:
    // assim um toque rápido não se perde mesmo se o pacote dele sumiu no Wi-Fi.
    for (int i = count - 1; i >= 0; --i) {
        const uint32_t s = seq - uint32_t(i);
        const bool fresh = !c.hasSeq || int32_t(s - c.lastSeq) > 0 || (i == 0 && c.reapplyNewest);
        if (!fresh) continue;
        applyState(c, readState(p + kInputFixedSize + size_t(i) * kStateSize));
        if (i > 0 && hadSeq) c.recovered.fetch_add(1, std::memory_order_relaxed);
        c.lastSeq = s;
        c.hasSeq = true;
    }
    c.reapplyNewest = false;
}

void Server::applyState(Client& c, const PadState& s) {
    if (!options_.dryRun) c.pad.submit(s);  // primeiro o driver, depois a contabilidade
    c.neutral = false;
    c.timedOut.store(false, std::memory_order_relaxed);
    c.applied.fetch_add(1, std::memory_order_relaxed);
    uint64_t lo = 0, hi = 0;
    packState(s, lo, hi);
    c.stateLo.store(lo, std::memory_order_relaxed);
    c.stateHi.store(hi, std::memory_order_relaxed);
    if (options_.printStates) {
        std::printf("[%s] botoes=%04X LT=%3u RT=%3u L=(%6d,%6d) R=(%6d,%6d)\n", c.addressText.c_str(), s.buttons,
                    s.leftTrigger, s.rightTrigger, s.thumbLX, s.thumbLY, s.thumbRX, s.thumbRY);
    }
}

void Server::handlePing(Client& c, const uint8_t* p, size_t len, const sockaddr_in& from) {
    if (len < kPingFixedSize) return;
    const size_t extAt = kPingFixedSize + p[29];  // a extensão vem logo depois do nome
    if (len > extAt) readPhoneExtension(c, p + extAt, len - extAt);

    uint8_t out[kPongSize] = {};
    writeHeader(out, kTypePong, c.clientId);
    std::memcpy(out + 8, p + 8, 16);  // session, pingId, clientTimeNs
    if (c.rumbleTestStartMs.load(std::memory_order_relaxed) != 0) {
        // O PONG reforça a vibração atual: durante o teste, a do teste.
        out[24] = out[25] = c.rumbleTestOn ? kRumbleTestStrength : 0;
    } else {
        out[24] = c.largeMotor.load(std::memory_order_relaxed);
        out[25] = c.smallMotor.load(std::memory_order_relaxed);
    }
    const int player = c.player.load(std::memory_order_relaxed);
    out[26] = (player >= 0 && player <= 3) ? uint8_t(player) : 0xFF;
    out[27] = uint8_t((c.pad.isPlugged() ? kPongFlagPadActive : 0) | (options_.dryRun ? kPongFlagDryRun : 0) |
                      (driverState() == DriverState::Missing ? kPongFlagNoDriver : 0));
    const int loss = c.lossBasisPoints.load(std::memory_order_relaxed);
    wr16(out + 28, loss < 0 ? kUnknownLoss : uint16_t(loss));
    wr32(out + 32, uint32_t(c.lost.load(std::memory_order_relaxed)));
    wr32(out + 36, uint32_t(c.recovered.load(std::memory_order_relaxed)));
    sendto(sock_, reinterpret_cast<const char*>(out), sizeof(out), 0, reinterpret_cast<const sockaddr*>(&from),
           sizeof(from));

    c.rttUs.store(rd32(p + 24), std::memory_order_relaxed);
    c.paused.store((p[28] & kPingFlagPaused) != 0, std::memory_order_relaxed);
    const size_t nameLen = std::min<size_t>(p[29], kMaxNameLen);
    if (len >= kPingFixedSize + nameLen) {
        std::string name(reinterpret_cast<const char*>(p + kPingFixedSize), nameLen);
        if (name != c.name) {
            std::lock_guard<std::mutex> lock(mutex_);
            c.name = std::move(name);
        }
    }
}

void Server::readPhoneExtension(Client& c, const uint8_t* ext, size_t len) {
    if (ext[0] < kPingExtSize || len < 1 + kPingExtSize) return;  // extensão cortada ou de outro formato
    const uint8_t* e = ext + 1;
    PhoneInfo info;
    info.known = true;
    info.battery = e[0] <= 100 ? e[0] : kUnknownPercent;
    info.charging = (e[1] & kPhoneFlagCharging) != 0;
    info.powerSave = (e[1] & kPhoneFlagPowerSave) != 0;
    info.wifiGeneration = e[2];
    info.wifiMhz = rd16(e + 3);
    info.wifiDbm = int8_t(e[5]);
    info.linkMbps = rd16(e + 7);
    info.padBattery = e[13] <= 100 ? e[13] : kUnknownPercent;
    if (!(info == c.phone)) {
        std::lock_guard<std::mutex> lock(mutex_);
        c.phone = info;
    }

    // Perda = 1 - recebidos/enviados desde a amostra mais antiga da janela. Os dois
    // contadores incluem este PING; a primeira amostra da sessão é (0, 0).
    const uint32_t sent = rd32(e + 9);
    const uint32_t recv = uint32_t(c.sessionRecv);
    const int n = int(c.lossSamples.size());
    c.lossSamples[size_t(c.lossPos)] = {sent, recv};
    c.lossPos = (c.lossPos + 1) % n;
    if (c.lossCount < n) ++c.lossCount;
    const auto& oldest = c.lossSamples[size_t(c.lossCount < n ? 0 : c.lossPos)];
    const uint32_t dSent = sent - oldest.first;
    const uint32_t dRecv = recv - oldest.second;
    if (dSent == 0 || dSent > 0x7FFFFFFF) return;  // nada novo, ou contador do celular reiniciado
    // Pacotes atrasados (reordenados) podem chegar depois do PING: nunca mostra perda negativa.
    const uint64_t missing = dRecv < dSent ? uint64_t(dSent - dRecv) : 0;
    c.lossBasisPoints.store(int(missing * 10000 / dSent), std::memory_order_relaxed);
}

void Server::resetSessionStats(Client& c) {
    c.sessionRecv = 0;
    c.lost = 0;
    c.recovered = 0;
    c.lossSamples[0] = {0, 0};
    c.lossCount = 1;
    c.lossPos = 1;
    c.lossBasisPoints = -1;
}

void Server::testRumble(int slot) {
    if (slot < 0 || slot >= kMaxClients) return;
    Client& c = clients_[size_t(slot)];
    std::string who;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!c.active) return;
        who = c.name.empty() ? c.addressText : c.name;
    }
    c.rumbleTestStartMs.store(std::max<uint64_t>(nowMs(), 1), std::memory_order_relaxed);
    logEvent("Teste de vibração enviado para \"" + who + "\"");
}

void Server::runRumbleTest(Client& c, uint64_t now) {
    uint64_t start = c.rumbleTestStartMs.load(std::memory_order_relaxed);
    if (start == 0) return;
    const uint64_t t = now > start ? now - start : 0;
    if (t >= kRumbleTestMs) {
        // Outro clique no meio do caminho troca o início: o teste recomeça no próximo tique.
        if (!c.rumbleTestStartMs.compare_exchange_strong(start, 0, std::memory_order_relaxed)) return;
        c.rumbleTestOn = false;
        c.rumbleTestSeen = 0;
        sendRumble(c, c.largeMotor.load(std::memory_order_relaxed), c.smallMotor.load(std::memory_order_relaxed), 0);
        return;
    }
    const bool on = rumbleTestPulseOn(t);
    if (start == c.rumbleTestSeen && on == c.rumbleTestOn) return;
    c.rumbleTestSeen = start;
    c.rumbleTestOn = on;
    const uint8_t strength = on ? kRumbleTestStrength : 0;
    sendRumble(c, strength, strength, kRumbleFlagTest);
}

void Server::sendRumble(Client& c, uint8_t large, uint8_t weak, uint8_t flags) {
    sockaddr_in to{};
    uint32_t clientId = 0, session = 0;
    bool active = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        active = c.active;
        to = c.addr;
        clientId = c.clientId;
        session = c.session;
    }
    if (!active || to.sin_port == 0) return;

    const int player = c.player.load(std::memory_order_relaxed);
    uint8_t out[kRumbleSize];
    writeHeader(out, kTypeRumble, clientId);
    wr32(out + 8, session);
    out[12] = large;
    out[13] = weak;
    out[14] = (player >= 0 && player <= 3) ? uint8_t(player) : 0xFF;
    out[15] = flags;
    sendto(sock_, reinterpret_cast<const char*>(out), sizeof(out), 0, reinterpret_cast<const sockaddr*>(&to),
           sizeof(to));
}

void Server::sendDiscoverReply(const sockaddr_in& to) {
    uint8_t out[kHeaderSize + 3 + kMaxNameLen];
    writeHeader(out, kTypeDiscoverReply, 0);
    wr16(out + 8, options_.port);
    const size_t nameLen = std::min(hostName_.size(), kMaxNameLen);
    out[10] = uint8_t(nameLen);
    std::memcpy(out + 11, hostName_.data(), nameLen);
    sendto(sock_, reinterpret_cast<const char*>(out), int(11 + nameLen), 0, reinterpret_cast<const sockaddr*>(&to),
           sizeof(to));
}

void Server::maintenance(uint64_t now) {
    // Driver instalado com o programa aberto: passa a funcionar sem reiniciar.
    if (!options_.dryRun && !bus_.isOpen() && now >= nextDriverRetryMs_ && tryOpenDriver(now)) {
        logEvent("Driver ViGEmBus encontrado: controle virtual ativado");
    }

    for (auto& c : clients_) {
        if (!c.active) continue;
        if (!c.pad.isPlugged()) ensurePad(c, now);
        runRumbleTest(c, now);
        const uint64_t silent = now - c.lastRecvMs;
        if (silent >= kReleaseAfterMs) {
            releaseClient(c, "ficou 2 minutos sem sinal");
            continue;
        }
        if (c.neutral || silent < kNeutralTimeoutMs) continue;
        // Celular sumiu (Wi-Fi caiu, app em segundo plano...): solta tudo para não travar botões.
        if (!options_.dryRun) c.pad.submit(PadState{});
        c.stateLo.store(0, std::memory_order_relaxed);
        c.stateHi.store(0, std::memory_order_relaxed);
        c.neutral = true;
        c.reapplyNewest = true;
        c.timedOut.store(true, std::memory_order_relaxed);
        if (!c.paused.load(std::memory_order_relaxed)) {
            logEvent("Sem sinal de \"" + (c.name.empty() ? c.addressText : c.name) +
                     "\": controle em posição neutra até voltar");
        }
    }
}

bool Server::tryOpenDriver(uint64_t now) {
    nextDriverRetryMs_ = now + kDriverRetryMs;
    if (!bus_.open()) return false;
    driverState_.store(int(DriverState::Ready), std::memory_order_relaxed);
    return true;
}

void Server::ensurePad(Client& c, uint64_t now) {
    if (options_.dryRun || c.pad.isPlugged() || !bus_.isOpen() || now < nextPlugAttemptMs_) return;
    Client* target = &c;
    const bool plugged = c.pad.plug(
        bus_, [this, target](uint8_t large, uint8_t weak, uint8_t led) { onFeedback(*target, large, weak, led); });
    if (!plugged) {
        nextPlugAttemptMs_ = now + kPlugRetryMs;
        logEvent("ERRO: não foi possível criar o controle Xbox 360 virtual no ViGEmBus");
        return;
    }
    c.player.store(c.pad.userIndex());
    c.hasPad = true;
    c.reapplyNewest = true;  // o heartbeat seguinte (mesmo seq) já aplica o estado atual no controle novo
}

bool Server::isClosedSession(uint32_t clientId, uint32_t session, uint64_t now) const {
    for (const ClosedSession& s : closed_) {
        if (s.atMs != 0 && s.clientId == clientId && s.session == session && now - s.atMs < kClosedSessionMs) {
            return true;
        }
    }
    return false;
}

Server::Client* Server::findClient(uint32_t clientId) {
    for (auto& c : clients_) {
        if (c.active && c.clientId == clientId) return &c;
    }
    return nullptr;
}

Server::Client* Server::acquireClient(uint32_t clientId, const sockaddr_in& from, uint64_t now) {
    Client* slot = nullptr;
    for (auto& c : clients_) {
        if (!c.active) {
            slot = &c;
            break;
        }
    }
    if (!slot) {
        for (auto& c : clients_) {
            if (now - c.lastRecvMs >= kEvictAfterMs && (!slot || c.lastRecvMs < slot->lastRecvMs)) slot = &c;
        }
        if (!slot) return nullptr;  // já há 4 celulares ativos
        logEvent("Reaproveitando o controle de \"" + (slot->name.empty() ? slot->addressText : slot->name) +
                 "\" (parado há mais de 10 s)");
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        slot->active = true;
        slot->clientId = clientId;
        slot->hasSession = false;
        slot->addr = from;
        slot->addressText = formatAddress(from);
        slot->name.clear();
        slot->phone = {};
    }
    slot->hasSeq = false;
    slot->neutral = true;
    slot->reapplyNewest = false;
    slot->lastRecvMs = now;
    slot->packets = 0;
    slot->applied = 0;
    resetSessionStats(*slot);
    slot->rumbleTestStartMs = 0;
    slot->rumbleTestOn = false;
    slot->rumbleTestSeen = 0;
    slot->rttUs = 0;
    slot->largeMotor = 0;
    slot->smallMotor = 0;
    slot->timedOut = false;
    slot->paused = false;
    slot->stateLo = 0;
    slot->stateHi = 0;

    // Sem driver (ou se falhar) o celular continua conectado: o PC avisa no PONG e a
    // manutenção cria o controle virtual quando der. Num slot reaproveitado o controle
    // continua plugado (mantém o número do jogador).
    ensurePad(*slot, now);

    const char* detail = options_.dryRun                ? " (modo teste: sem controle virtual)"
                         : slot->pad.isPlugged()        ? ": controle Xbox 360 virtual pronto"
                                                        : " (sem o driver ViGEmBus a Steam não vê o controle)";
    logEvent("Celular conectado de " + slot->addressText + detail);
    return slot;
}

void Server::releaseClient(Client& c, const char* reason) {
    const std::string who = c.name.empty() ? c.addressText : c.name;
    const bool hadPad = c.pad.isPlugged();
    if (hadPad) {
        c.pad.submit(PadState{});
        c.pad.unplug();
        c.hasPad = false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        c.active = false;
        c.hasSession = false;
        c.addr = {};
    }
    c.player = -1;
    c.stateLo = 0;
    c.stateHi = 0;
    logEvent("\"" + who + "\" " + reason + (hadPad ? ": controle virtual removido" : ""));
}

void Server::onFeedback(Client& c, uint8_t large, uint8_t weak, uint8_t led) {
    // Roda na thread de notificação do controle: repassa a vibração na hora.
    c.largeMotor.store(large, std::memory_order_relaxed);
    c.smallMotor.store(weak, std::memory_order_relaxed);
    if (led <= 3) c.player.store(led, std::memory_order_relaxed);
    // Durante o teste de vibração o jogo espera: o fim do teste manda o valor atual dele.
    if (c.rumbleTestStartMs.load(std::memory_order_relaxed) != 0) return;

    // Muitos jogos repetem a vibração a cada quadro: só manda o que mudou
    // (o PONG, a cada 500 ms, reforça o estado atual caso um pacote se perca).
    if (large == c.sentLarge && weak == c.sentSmall) return;
    c.sentLarge = large;
    c.sentSmall = weak;
    sendRumble(c, large, weak, 0);
}

std::vector<ClientView> Server::clients() {
    std::vector<ClientView> out;
    const uint64_t now = nowMs();
    std::lock_guard<std::mutex> lock(mutex_);
    for (size_t i = 0; i < clients_.size(); ++i) {
        Client& c = clients_[i];
        if (!c.active) continue;
        ClientView v;
        v.slot = int(i);
        v.clientId = c.clientId;
        v.padActive = c.hasPad.load(std::memory_order_relaxed);
        v.name = c.name;
        v.address = c.addressText;
        v.player = c.player.load(std::memory_order_relaxed);
        v.rttUs = c.rttUs.load(std::memory_order_relaxed);
        v.packets = c.packets.load(std::memory_order_relaxed);
        v.applied = c.applied.load(std::memory_order_relaxed);
        v.lost = c.lost.load(std::memory_order_relaxed);
        v.recovered = c.recovered.load(std::memory_order_relaxed);
        const uint64_t last = c.lastRecvShared.load(std::memory_order_relaxed);
        v.silentMs = now > last ? now - last : 0;
        v.timedOut = c.timedOut.load(std::memory_order_relaxed);
        v.paused = c.paused.load(std::memory_order_relaxed);
        v.largeMotor = c.largeMotor.load(std::memory_order_relaxed);
        v.smallMotor = c.smallMotor.load(std::memory_order_relaxed);
        v.state = unpackState(c.stateLo.load(std::memory_order_relaxed), c.stateHi.load(std::memory_order_relaxed));
        v.phone = c.phone;
        v.lossBasisPoints = c.lossBasisPoints.load(std::memory_order_relaxed);
        v.rumbleTest = c.rumbleTestStartMs.load(std::memory_order_relaxed) != 0;
        out.push_back(std::move(v));
    }
    return out;
}

std::vector<std::string> Server::recentEvents() {
    std::lock_guard<std::mutex> lock(eventsMutex_);
    return {events_.begin(), events_.end()};
}

void Server::logEvent(std::string text) {
    char ts[16] = {};
    const time_t t = time(nullptr);
    tm local{};
    localtime_s(&local, &t);
    strftime(ts, sizeof(ts), "%H:%M:%S", &local);
    std::string line = std::string(ts) + "  " + text;
    if (options_.printEvents) std::printf("%s\n", line.c_str());
    std::lock_guard<std::mutex> lock(eventsMutex_);
    events_.push_back(std::move(line));
    while (events_.size() > 8) events_.pop_front();
}

}  // namespace swc
