// Protocolo UDP entre o app Android e o servidor do PC.
// Espelhado em Android/app/src/main/java/com/steamwifi/controller/Protocol.java
// Todos os campos multi-byte são little-endian.
//
// Cabeçalho (8 bytes), comum a todos os pacotes:
//   0  u8  'S'
//   1  u8  'W'
//   2  u8  versão (1)
//   3  u8  tipo
//   4  u32 clientId   (aleatório, fixo por instalação do app)
//
// INPUT  (celular -> PC), 20 + 12*count bytes
//   8  u32 session    (aleatório a cada conexão; zera o controle de sequência)
//   12 u32 seq        (sequência do estado mais novo = states[0])
//   16 u8  count      (1..4) estados no pacote, do mais novo para o mais antigo
//   17 u8  flags      (reservado)
//   18 u16 reservado
//   20 states[count]  12 bytes cada, mesmo layout de XUSB_REPORT; states[i] tem seq-i
//
//   Cada estado novo incrementa seq. O pacote leva os últimos estados (redundância),
//   então a perda de até count-1 pacotes seguidos não perde nenhum toque de botão.
//   O celular reenvia o último pacote periodicamente (heartbeat) quando parado.
//
// PING   (celular -> PC), 30 + nameLen (+ 15 da extensão) bytes
//   8  u32 session
//   12 u32 pingId
//   16 u64 clientTimeNs  (relógio do celular, devolvido no PONG)
//   24 u32 lastRttUs     (último RTT medido pelo celular, só para exibição)
//   28 u8  flags         (bit0 = app em segundo plano: só pings, sem heartbeat de INPUT)
//   29 u8  nameLen
//   30 nome do aparelho (UTF-8)
//   Extensão, logo depois do nome (apps antigos não mandam; PCs antigos ignoram):
//   +0  u8  extLen         (bytes que seguem: 14; aceita mais, para extensões futuras)
//   +1  u8  bateria do celular em % (0xFF = desconhecida)
//   +2  u8  flags do celular (bit0 = carregando, bit1 = economia de energia ligada)
//   +3  u8  geração do Wi-Fi (4, 5, 6, 7; 0 = desconhecida)
//   +4  u16 frequência do Wi-Fi em MHz (0 = sem Wi-Fi)
//   +6  i8  sinal do Wi-Fi em dBm (0 = desconhecido)
//   +7  u8  reservado
//   +8  u16 velocidade do link Wi-Fi em Mbps (0 = desconhecida)
//   +10 u32 pacotes enviados ao PC nesta sessão, contando este PING (o PC calcula a perda)
//   +14 u8  bateria do controle em % (0xFF = sem bateria ou desconhecida)
//
// PONG   (PC -> celular), 40 bytes (PCs antigos mandam só os 28 primeiros)
//   8..23 eco de session/pingId/clientTimeNs
//   24 u8 largeMotor   25 u8 smallMotor   26 u8 ledNumber (0xFF = desconhecido)
//   27 u8 flags (bit0 = controle virtual ativo, bit1 = modo dry-run, bit2 = PC sem o driver ViGEmBus)
//   28 u16 perda de pacotes celular -> PC nos últimos ~5 s, em centésimos de % (0xFFFF = sem dados)
//   30 u16 reservado
//   32 u32 estados perdidos nesta sessão (nem a redundância salvou)
//   36 u32 estados recuperados pela redundância nesta sessão
//
// RUMBLE (PC -> celular), 16 bytes
//   8  u32 session   12 u8 largeMotor   13 u8 smallMotor   14 u8 ledNumber
//   15 u8  flags (bit0 = teste de vibração pedido na janela do PC)
//
// DISCOVER (celular -> broadcast), 8 bytes
// DISCOVER_REPLY (PC -> celular), 11 + nameLen bytes
//   8  u16 porta   10 u8 nameLen   11 nome do PC (UTF-8)
//
// BYE    (celular -> PC), 12 bytes
//   8  u32 session
#pragma once

#include <cstddef>
#include <cstdint>

namespace swc {

constexpr uint16_t kDefaultPort = 38500;
constexpr uint8_t kMagic0 = 'S';
constexpr uint8_t kMagic1 = 'W';
constexpr uint8_t kProtocolVersion = 1;

enum PacketType : uint8_t {
    kTypeInput = 0x01,
    kTypePing = 0x02,
    kTypePong = 0x03,
    kTypeRumble = 0x04,
    kTypeDiscover = 0x05,
    kTypeDiscoverReply = 0x06,
    kTypeBye = 0x07,
};

constexpr size_t kHeaderSize = 8;
constexpr size_t kInputFixedSize = 20;
constexpr size_t kStateSize = 12;
constexpr int kMaxStatesPerPacket = 4;
constexpr size_t kPingFixedSize = 30;
constexpr size_t kPingExtSize = 14;  // bytes da extensão depois do extLen
constexpr size_t kPongSize = 40;
constexpr size_t kPongLegacySize = 28;
constexpr size_t kRumbleSize = 16;
constexpr size_t kByeSize = 12;
constexpr size_t kMaxNameLen = 63;

constexpr uint8_t kPingFlagPaused = 0x01;
constexpr uint8_t kPongFlagPadActive = 0x01;
constexpr uint8_t kPongFlagDryRun = 0x02;
constexpr uint8_t kPongFlagNoDriver = 0x04;
constexpr uint8_t kPhoneFlagCharging = 0x01;
constexpr uint8_t kPhoneFlagPowerSave = 0x02;
constexpr uint8_t kRumbleFlagTest = 0x01;
constexpr uint8_t kUnknownPercent = 0xFF;
constexpr uint16_t kUnknownLoss = 0xFFFF;

// Mesmo layout de XUSB_REPORT / XINPUT_GAMEPAD.
struct PadState {
    uint16_t buttons = 0;
    uint8_t leftTrigger = 0;
    uint8_t rightTrigger = 0;
    int16_t thumbLX = 0;
    int16_t thumbLY = 0;
    int16_t thumbRX = 0;
    int16_t thumbRY = 0;
};
static_assert(sizeof(PadState) == kStateSize, "PadState precisa ter o layout de XUSB_REPORT");

inline bool operator==(const PadState& a, const PadState& b) {
    return a.buttons == b.buttons && a.leftTrigger == b.leftTrigger &&
           a.rightTrigger == b.rightTrigger && a.thumbLX == b.thumbLX &&
           a.thumbLY == b.thumbLY && a.thumbRX == b.thumbRX && a.thumbRY == b.thumbRY;
}

inline uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
inline uint32_t rd32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
inline void wr16(uint8_t* p, uint16_t v) {
    p[0] = uint8_t(v);
    p[1] = uint8_t(v >> 8);
}
inline void wr32(uint8_t* p, uint32_t v) {
    p[0] = uint8_t(v);
    p[1] = uint8_t(v >> 8);
    p[2] = uint8_t(v >> 16);
    p[3] = uint8_t(v >> 24);
}

inline PadState readState(const uint8_t* p) {
    PadState s;
    s.buttons = rd16(p);
    s.leftTrigger = p[2];
    s.rightTrigger = p[3];
    s.thumbLX = int16_t(rd16(p + 4));
    s.thumbLY = int16_t(rd16(p + 6));
    s.thumbRX = int16_t(rd16(p + 8));
    s.thumbRY = int16_t(rd16(p + 10));
    return s;
}

inline void writeHeader(uint8_t* p, PacketType type, uint32_t clientId) {
    p[0] = kMagic0;
    p[1] = kMagic1;
    p[2] = kProtocolVersion;
    p[3] = type;
    wr32(p + 4, clientId);
}

inline bool validHeader(const uint8_t* p, size_t len) {
    return len >= kHeaderSize && p[0] == kMagic0 && p[1] == kMagic1 && p[2] == kProtocolVersion;
}

}  // namespace swc
