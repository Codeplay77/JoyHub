#!/usr/bin/env python3
"""Simula o app Android para testar o servidor do PC.

Uso (com o JoyHub aberto, ou "JoyHub.exe --headless --dry-run" para testar sem o driver):
  python teste_pc.py                 testa contra 127.0.0.1 (servidor rodando neste PC)
  python teste_pc.py 192.168.0.10    testa contra outro PC
  python teste_pc.py --xinput        também mede a latência até o XInput (precisa do ViGEmBus)
  python teste_pc.py --guide         confere se o botão Guide chega ao XInput (e à Steam)

O teste de XInput aperta e solta o botão A no controle virtual várias vezes, e o de
Guide aperta o Guide por 300 ms (a Steam pode abrir o Big Picture): não rode com um jogo aberto.
"""
import argparse
import ctypes
import random
import socket
import statistics
import struct
import sys
import time

PORT = 38500
T_INPUT, T_PING, T_PONG, T_RUMBLE, T_DISCOVER, T_DISCOVER_REPLY, T_BYE = range(1, 8)
BTN_A = 0x1000
BTN_GUIDE = 0x0400
LOSS_WINDOW = 10  # mesmo kLossWindowPings do servidor


def header(ptype, client_id):
    return struct.pack("<2sBBI", b"SW", 1, ptype, client_id)


def pad_state(buttons=0, lt=0, rt=0, lx=0, ly=0, rx=0, ry=0):
    return struct.pack("<HBBhhhh", buttons, lt, rt, lx, ly, rx, ry)


class FakePhone:
    def __init__(self, host, port):
        self.addr = (host, port)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        self.client_id = random.getrandbits(32)
        self.session = random.getrandbits(32)
        self.seq = 0
        self.history = []  # estados, do mais novo para o mais antigo
        self.last_packet = b""
        self.sent = 0       # pacotes "enviados" nesta sessão (INPUT + PING), como conta o app
        self.delivered = 0  # os que saíram de fato (sem os descartados de propósito)
        self.loss_samples = [(0, 0)]  # espelho da janela de perda do servidor

    def send_state(self, state, drop=False):
        self.seq += 1
        self.history.insert(0, state)
        del self.history[4:]
        body = struct.pack("<IIBBH", self.session, self.seq, len(self.history), 0, 0)
        self.last_packet = header(T_INPUT, self.client_id) + body + b"".join(self.history)
        self.sent += 1
        if not drop:
            self.delivered += 1
            self.sock.sendto(self.last_packet, self.addr)

    def resend_last(self):
        self.sent += 1
        self.delivered += 1
        self.sock.sendto(self.last_packet, self.addr)

    def expected_loss_bp(self, extra=1):
        """Perda que o servidor deve calcular no próximo PING (mesma janela de LOSS_WINDOW PINGs)."""
        old_sent, old_recv = self.loss_samples[-LOSS_WINDOW] if len(self.loss_samples) >= LOSS_WINDOW else (0, 0)
        d_sent, d_recv = self.sent + extra - old_sent, self.delivered + extra - old_recv
        return (d_sent - d_recv) * 10000 // d_sent if d_sent else None

    def ping(self, ping_id, timeout=1.0):
        name = b"Teste Python"
        t0 = time.perf_counter_ns()
        self.sent += 1
        self.delivered += 1
        self.loss_samples.append((self.sent, self.delivered))
        del self.loss_samples[:-(LOSS_WINDOW + 1)]
        pkt = header(T_PING, self.client_id) + struct.pack("<IIQIBB", self.session, ping_id, t0, 0, 0, len(name))
        # Extensão: bateria 77% carregando, Wi-Fi 6 em 5180 MHz, -52 dBm, 866 Mbps, controle sem bateria.
        ext = struct.pack("<BBBBHbBHIB", 14, 77, 1, 6, 5180, -52, 0, 866, self.sent, 0xFF)
        self.sock.sendto(pkt + name + ext, self.addr)
        self.sock.settimeout(timeout)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                data, _ = self.sock.recvfrom(2048)
            except (socket.timeout, ConnectionResetError):
                return None
            if len(data) >= 28 and data[3] == T_PONG:
                _, pid, echoed = struct.unpack_from("<IIQ", data, 8)
                if pid == ping_id and echoed == t0:
                    rtt_us = (time.perf_counter_ns() - t0) / 1000
                    large, small, led, flags = data[24:28]
                    stats = struct.unpack_from("<HHII", data, 28) if len(data) >= 40 else None
                    return rtt_us, led, flags, stats  # stats: (perda em 0,01%, reservado, perdidos, recuperados)
        return None

    def bye(self):
        self.sock.sendto(header(T_BYE, self.client_id) + struct.pack("<I", self.session), self.addr)


def discover(host, port):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    s.settimeout(0.5)
    s.sendto(header(T_DISCOVER, 0), (host, port))
    try:
        data, addr = s.recvfrom(2048)
    except (socket.timeout, ConnectionResetError):  # ConnectionReset = ninguém escutando (Windows)
        return None
    if len(data) >= 11 and data[3] == T_DISCOVER_REPLY:
        name_len = data[10]
        return addr[0], data[11:11 + name_len].decode("utf-8", "replace")
    return None


class XInputGamepad(ctypes.Structure):
    _fields_ = [("wButtons", ctypes.c_ushort), ("bLeftTrigger", ctypes.c_ubyte), ("bRightTrigger", ctypes.c_ubyte),
                ("sThumbLX", ctypes.c_short), ("sThumbLY", ctypes.c_short),
                ("sThumbRX", ctypes.c_short), ("sThumbRY", ctypes.c_short)]


class XInputState(ctypes.Structure):
    _fields_ = [("dwPacketNumber", ctypes.c_uint), ("Gamepad", XInputGamepad)]


def xinput_get_state_ex():
    """XInputGetStateEx (ordinal 100 do xinput1_4): igual ao XInputGetState, mas inclui o Guide."""
    proto = ctypes.WINFUNCTYPE(ctypes.c_uint, ctypes.c_uint, ctypes.POINTER(XInputState))
    return proto((100, ctypes.windll.xinput1_4))


def guide_visible(phone, user_index):
    """Aperta o Guide por 300 ms; retorna True se o XInput enxergou o botão."""
    get_state_ex = xinput_get_state_ex()
    st = XInputState()
    seen = False
    phone.send_state(pad_state(buttons=BTN_GUIDE))
    end = time.monotonic() + 0.3
    while time.monotonic() < end:
        phone.resend_last()
        if get_state_ex(user_index, ctypes.byref(st)) == 0 and st.Gamepad.wButtons & BTN_GUIDE:
            seen = True
        time.sleep(0.01)
    phone.send_state(pad_state())
    return seen


def xinput_latency(phone, user_index, rounds=200):
    xinput = ctypes.windll.xinput1_4
    st = XInputState()
    samples = []
    for i in range(rounds):
        want = BTN_A if i % 2 == 0 else 0
        t0 = time.perf_counter_ns()
        phone.send_state(pad_state(buttons=want))
        while True:
            if xinput.XInputGetState(user_index, ctypes.byref(st)) != 0:
                return None
            if (st.Gamepad.wButtons & BTN_A) == want:
                break
            if time.perf_counter_ns() - t0 > 200_000_000:
                return None
        samples.append((time.perf_counter_ns() - t0) / 1000)
        time.sleep(0.004)
    return samples


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("host", nargs="?", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=PORT)
    ap.add_argument("--xinput", action="store_true", help="mede a latência até o XInput (precisa do ViGEmBus)")
    ap.add_argument("--guide", action="store_true", help="confere se o Guide chega ao XInput (precisa do ViGEmBus)")
    args = ap.parse_args()

    found = discover(args.host, args.port)
    print(f"[descoberta] {'PC encontrado: %s (%s)' % (found[1], found[0]) if found else 'sem resposta'}")
    if not found:
        print("O servidor está rodando? A porta UDP está liberada no firewall?")
        return 1

    phone = FakePhone(args.host, args.port)
    rtts, led, flags, stats = [], 0xFF, 0, None
    for i in range(20):
        r = phone.ping(i + 1)
        if r:
            rtts.append(r[0])
            led, flags, stats = r[1], r[2], r[3]
    if not rtts:
        print("[ping] sem resposta")
        return 1
    print(f"[ping] {len(rtts)}/20 respostas, RTT mediano {statistics.median(rtts):.0f} us, máx {max(rtts):.0f} us")
    print(f"[ping] controle virtual ativo={bool(flags & 1)} dry-run={bool(flags & 2)} "
          f"sem-driver={bool(flags & 4)} jogador={led + 1 if led <= 3 else '?'}")
    if stats is None:
        print("[ping] PONG sem estatísticas (servidor de versão antiga)")

    # Perda de pacotes: o PING leva quantos pacotes o celular enviou; o PC compara com o que recebeu.
    for i in range(100):
        phone.send_state(pad_state(lx=i * 100), drop=i % 10 == 0)  # 10% descartados de propósito
    expected = phone.expected_loss_bp()
    r = phone.ping(100)
    if r and r[3]:
        got = r[3][0]
        verdict = "ok" if got == expected else f"DIFERENTE do esperado ({expected / 100:.2f}%)"
        print(f"[perda] 10 de 100 INPUT descartados: PC mediu {got / 100:.2f}% na janela de ~5 s ({verdict})")
    else:
        print("[perda] sem resposta com estatísticas")

    # Toque rápido cujo pacote se perde: a redundância do pacote seguinte recupera.
    phone.send_state(pad_state())
    phone.send_state(pad_state(buttons=BTN_A), drop=True)
    phone.send_state(pad_state(lx=12000, ly=-8000))
    print("[redundância] enviado: A apertado (pacote descartado de propósito), depois analógico mexido")

    # Botão segurado + Wi-Fi caindo por 700 ms: servidor solta tudo e reaplica ao voltar.
    phone.send_state(pad_state(buttons=BTN_A, rt=255))
    time.sleep(0.7)
    phone.resend_last()
    print("[timeout] A + RT segurados, 700 ms sem pacotes, depois heartbeat repetido")
    time.sleep(0.1)
    r = phone.ping(101)
    if r and r[3]:
        print(f"[contadores] PONG: perdidos={r[3][2]} recuperados={r[3][3]} (os toques descartados contam como recuperados)")

    if args.xinput:
        if not (flags & 1):
            print("[xinput] o servidor não criou controle virtual (dry-run ou sem ViGEmBus)")
        elif led > 3:
            print("[xinput] o Windows ainda não atribuiu um slot ao controle virtual")
        else:
            samples = xinput_latency(phone, led)
            if not samples:
                print("[xinput] o estado não apareceu no XInput")
            else:
                samples.sort()
                p99 = samples[int(len(samples) * 0.99) - 1]
                print(f"[xinput] UDP -> servidor -> driver -> XInput: mediana {statistics.median(samples):.0f} us,"
                      f" p99 {p99:.0f} us, máx {samples[-1]:.0f} us ({len(samples)} amostras)")

    # Por último: o Guide pode abrir o Big Picture, e o teste de XInput não deve apertar A lá dentro.
    if args.guide:
        if not (flags & 1) or led > 3:
            print("[guide] sem controle virtual (dry-run, sem ViGEmBus ou slot ainda não atribuído)")
        else:
            print("[guide] esperando 5 s para a Steam reconhecer o controle novo...")
            end = time.monotonic() + 5
            while time.monotonic() < end:  # heartbeat: sem ele o PC solta tudo depois de 500 ms
                phone.resend_last()
                time.sleep(0.01)
            if guide_visible(phone, led):
                print("[guide] o Guide chegou ao XInput: a Steam recebe o botão (o Big Picture deve abrir)")
            else:
                print("[guide] o Guide NÃO apareceu no XInput. Provável causa: a Xbox Game Bar reserva o botão Xbox.\n"
                      "        Desligue 'Abrir a Xbox Game Bar usando este botão no controle' (Configurações > Jogos)\n"
                      "        ou clique em 'Liberar para a Steam' na janela do JoyHub.")

    phone.send_state(pad_state())
    phone.bye()
    print("[bye] desconectado")
    return 0


if __name__ == "__main__":
    sys.exit(main())
