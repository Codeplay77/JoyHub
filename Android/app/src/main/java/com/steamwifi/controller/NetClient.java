package com.steamwifi.controller;

import android.net.ConnectivityManager;
import android.net.Network;
import android.os.Process;
import android.os.SystemClock;

import java.io.IOException;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetSocketAddress;
import java.net.SocketException;
import java.net.SocketTimeoutException;
import java.nio.charset.StandardCharsets;
import java.security.SecureRandom;
import java.util.concurrent.locks.LockSupport;

/**
 * Conexão UDP com o PC.
 *
 * sendState() envia na hora, na própria thread que recebeu o evento do controle (sem
 * fila nem troca de thread). Cada pacote leva os últimos 4 estados, então perdas
 * isoladas no Wi-Fi não perdem toques. A thread de heartbeat reenvia o último pacote a
 * cada 10 ms com o controle parado (recupera perdas e mantém o rádio Wi-Fi acordado) e
 * mede o ping a cada 500 ms.
 */
final class NetClient {
    interface Listener {
        /** Vibração pedida pelo jogo. Chamado na thread de recepção. */
        void onFeedback(int largeMotor, int smallMotor);

        /** O PC pediu um teste de vibração (botão na janela dele). Chamado na thread de recepção. */
        void onRumbleTest();
    }

    private static final long HEARTBEAT_NS = 10_000_000L;
    private static final long PING_INTERVAL_NS = 500_000_000L;
    private static final long PAUSED_PING_INTERVAL_NS = 1_000_000_000L;
    private static final long TICK_NS = 2_000_000L;
    private static final int TOS_EXPEDITED = 0xB8;  // DSCP EF: fila de voz (WMM AC_VO) no Wi-Fi

    final InetSocketAddress pc;
    private final DatagramSocket socket;
    private final int clientId;
    private final int session;
    private final Listener listener;

    private final Object sendLock = new Object();
    private final byte[] inputBuf = new byte[Protocol.INPUT_FIXED_SIZE + Protocol.STATE_SIZE * Protocol.MAX_STATES];
    private final DatagramPacket inputPacket = new DatagramPacket(inputBuf, inputBuf.length);
    private final byte[][] history = new byte[Protocol.MAX_STATES][Protocol.STATE_SIZE];
    private int historyCount;
    private int seq;
    private long lastInputSendNs;
    private final byte[] pingBuf;
    private final DatagramPacket pingPacket;
    private final int pingExtOffset;
    private int pingId;
    private int datagramsSent;  // INPUT + PING desta sessão; vai no PING para o PC medir a perda
    private volatile PhoneStatus phoneStatus;

    private volatile boolean running;
    private volatile boolean paused;
    private volatile boolean pingNow;
    private Thread heartbeatThread;
    private Thread receiverThread;

    // Estatísticas lidas pela interface.
    volatile long packetsSent;
    volatile int lastRttUs = -1;
    volatile int minRttUs;
    volatile int maxRttUs;
    volatile long lastPongMs;  // SystemClock.uptimeMillis() do último PONG
    volatile int player = -1;  // 0..3
    volatile int pcFlags;
    volatile int lossBasisPoints = -1;  // perda celular -> PC medida pelo PC (centésimos de %); -1 = sem dados
    volatile long recoveredStates;      // toques que a redundância salvou nesta sessão
    private final int[] rttWindow = new int[20];
    private int rttCount;
    private int rttPos;

    private NetClient(DatagramSocket socket, InetSocketAddress pc, int clientId, String deviceName, Listener l) {
        this.socket = socket;
        this.pc = pc;
        this.clientId = clientId;
        this.session = new SecureRandom().nextInt();
        this.listener = l;

        Protocol.writeHeader(inputBuf, Protocol.TYPE_INPUT, clientId);
        Protocol.writeInt(inputBuf, 8, session);

        byte[] name = deviceName.getBytes(StandardCharsets.UTF_8);
        int nameLen = Math.min(name.length, Protocol.MAX_NAME_LEN);
        pingExtOffset = Protocol.PING_FIXED_SIZE + nameLen;
        pingBuf = new byte[pingExtOffset + 1 + Protocol.PING_EXT_SIZE];
        Protocol.writeHeader(pingBuf, Protocol.TYPE_PING, clientId);
        Protocol.writeInt(pingBuf, 8, session);
        pingBuf[29] = (byte) nameLen;
        System.arraycopy(name, 0, pingBuf, Protocol.PING_FIXED_SIZE, nameLen);
        pingPacket = new DatagramPacket(pingBuf, pingBuf.length);
    }

    /** Cria o socket. Faz rede (resolução, bind): chamar fora da thread principal. */
    static NetClient open(ConnectivityManager cm, InetSocketAddress pc, int clientId, String deviceName,
                          Listener listener) throws IOException {
        DatagramSocket s = new DatagramSocket();
        try {
            // Se o Wi-Fi não tem internet, o Android pode mandar o tráfego pelos dados
            // móveis. Prender o socket à rede onde o PC está evita isso.
            Network lan = Networks.findNetworkFor(cm, pc.getAddress());
            if (lan != null) lan.bindSocket(s);
            try {
                s.setTrafficClass(TOS_EXPEDITED);
            } catch (SocketException ignored) {
                // alguns aparelhos não permitem; segue sem prioridade
            }
            s.setSoTimeout(250);
            s.connect(pc);
        } catch (IOException | RuntimeException e) {
            s.close();
            throw e;
        }
        return new NetClient(s, pc, clientId, deviceName, listener);
    }

    void start() {
        running = true;
        heartbeatThread = new Thread(this::heartbeatLoop, "joyhub-heartbeat");
        receiverThread = new Thread(this::receiveLoop, "joyhub-receiver");
        heartbeatThread.start();
        receiverThread.start();
    }

    /** Envia um estado novo imediatamente. Chamado na thread principal, a cada mudança. */
    void sendState(int buttons, int lt, int rt, int lx, int ly, int rx, int ry) {
        synchronized (sendLock) {
            // Histórico do mais novo para o mais antigo, sem alocar: gira as referências.
            byte[] slot = history[Protocol.MAX_STATES - 1];
            System.arraycopy(history, 0, history, 1, Protocol.MAX_STATES - 1);
            history[0] = slot;
            Protocol.writeState(slot, 0, buttons, lt, rt, lx, ly, rx, ry);
            if (historyCount < Protocol.MAX_STATES) historyCount++;
            seq++;

            Protocol.writeInt(inputBuf, 12, seq);
            inputBuf[16] = (byte) historyCount;
            for (int i = 0; i < historyCount; i++) {
                System.arraycopy(history[i], 0, inputBuf, Protocol.INPUT_FIXED_SIZE + i * Protocol.STATE_SIZE,
                        Protocol.STATE_SIZE);
            }
            inputPacket.setLength(Protocol.INPUT_FIXED_SIZE + historyCount * Protocol.STATE_SIZE);
            sendInputLocked();
        }
    }

    /**
     * App em segundo plano: para o heartbeat de INPUT e manda só um ping por segundo, com a
     * flag de pausa. O PC mantém o controle virtual plugado, em posição neutra.
     */
    void setPaused(boolean value) {
        paused = value;
        pingNow = true;  // avisa o PC já, antes do timeout dele
    }

    /** Bateria e Wi-Fi do celular, mandados ao PC no próximo PING. */
    void setPhoneStatus(PhoneStatus status) {
        phoneStatus = status;
    }

    /** Avisa o PC (BYE), fecha o socket e espera as threads terminarem. */
    void stop() {
        final boolean wasRunning = running;
        running = false;
        joinQuietly(heartbeatThread);  // nada pode sair depois do BYE (o PC recriaria o controle)
        if (wasRunning) {
            byte[] bye = new byte[Protocol.BYE_SIZE];
            Protocol.writeHeader(bye, Protocol.TYPE_BYE, clientId);
            Protocol.writeInt(bye, 8, session);
            DatagramPacket p = new DatagramPacket(bye, bye.length);
            synchronized (sendLock) {
                for (int i = 0; i < 3; i++) {  // UDP: repete para garantir
                    try {
                        socket.send(p);
                    } catch (IOException ignored) {
                        break;
                    }
                }
            }
        }
        socket.close();
        joinQuietly(receiverThread);
    }

    private void sendInputLocked() {
        datagramsSent++;  // conta também os que falham: para o PC, perdidos
        try {
            socket.send(inputPacket);
            lastInputSendNs = System.nanoTime();
            packetsSent++;
        } catch (IOException ignored) {
            // Wi-Fi caiu ou PC não está ouvindo (ICMP); o heartbeat tenta de novo
        }
    }

    private void writePingExtensionLocked() {
        final PhoneStatus s = phoneStatus;
        if (s == null) {
            Protocol.writePingExtension(pingBuf, pingExtOffset, -1, false, false, 0, 0, 0, 0, datagramsSent, -1);
        } else {
            Protocol.writePingExtension(pingBuf, pingExtOffset, s.battery, s.charging, s.powerSave, s.wifiGeneration,
                    s.wifiMhz, s.wifiDbm, s.linkMbps, datagramsSent, s.padBattery);
        }
    }

    private void heartbeatLoop() {
        Process.setThreadPriority(Process.THREAD_PRIORITY_URGENT_DISPLAY);
        long nextPingNs = 0;
        while (running) {
            final long now = System.nanoTime();
            final boolean isPaused = paused;
            synchronized (sendLock) {
                if (!isPaused && historyCount > 0 && now - lastInputSendNs >= HEARTBEAT_NS) sendInputLocked();
                if (pingNow || now - nextPingNs >= 0) {
                    pingNow = false;
                    pingId++;
                    Protocol.writeInt(pingBuf, 12, pingId);
                    Protocol.writeLong(pingBuf, 16, now);
                    Protocol.writeInt(pingBuf, 24, Math.max(lastRttUs, 0));
                    pingBuf[28] = (byte) (isPaused ? Protocol.PING_FLAG_PAUSED : 0);
                    datagramsSent++;  // o PING conta a si mesmo
                    writePingExtensionLocked();
                    try {
                        socket.send(pingPacket);
                    } catch (IOException ignored) {
                        // tenta no próximo intervalo
                    }
                    nextPingNs = now + (isPaused ? PAUSED_PING_INTERVAL_NS : PING_INTERVAL_NS);
                }
            }
            LockSupport.parkNanos(TICK_NS);
        }
    }

    private void receiveLoop() {
        Process.setThreadPriority(Process.THREAD_PRIORITY_URGENT_DISPLAY);
        final byte[] buf = new byte[512];
        final DatagramPacket p = new DatagramPacket(buf, buf.length);
        while (running) {
            try {
                p.setLength(buf.length);
                socket.receive(p);
            } catch (SocketTimeoutException e) {
                continue;
            } catch (IOException e) {
                if (!running) break;
                SystemClock.sleep(20);  // ex.: ICMP "porta inalcançável" enquanto o servidor está fechado
                continue;
            }
            handlePacket(buf, p.getLength());
        }
    }

    private void handlePacket(byte[] b, int len) {
        if (!Protocol.validHeader(b, len) || len < 12 || Protocol.readInt(b, 8) != session) return;
        if (b[3] == Protocol.TYPE_PONG && len >= Protocol.PONG_SIZE) {
            final long rttNs = System.nanoTime() - Protocol.readLong(b, 16);
            if (rttNs < 0 || rttNs > 10_000_000_000L) return;
            recordRtt((int) (rttNs / 1000));
            lastPongMs = SystemClock.uptimeMillis();
            final int led = b[26] & 0xFF;
            player = led <= 3 ? led : -1;
            pcFlags = b[27] & 0xFF;
            if (len >= Protocol.PONG_EXT_SIZE) {
                final int loss = Protocol.readUShort(b, 28);
                lossBasisPoints = loss == Protocol.UNKNOWN_LOSS ? -1 : loss;
                recoveredStates = Protocol.readInt(b, 36) & 0xFFFFFFFFL;
            }
            listener.onFeedback(b[24] & 0xFF, b[25] & 0xFF);  // o PONG também reforça a vibração atual
        } else if (b[3] == Protocol.TYPE_RUMBLE && len >= Protocol.RUMBLE_SIZE) {
            final int led = b[14] & 0xFF;
            if (led <= 3) player = led;
            listener.onFeedback(b[12] & 0xFF, b[13] & 0xFF);
            if ((b[15] & Protocol.RUMBLE_FLAG_TEST) != 0) listener.onRumbleTest();
        }
    }

    private void recordRtt(int us) {
        rttWindow[rttPos] = us;
        rttPos = (rttPos + 1) % rttWindow.length;
        if (rttCount < rttWindow.length) rttCount++;
        int min = Integer.MAX_VALUE;
        int max = 0;
        for (int i = 0; i < rttCount; i++) {
            min = Math.min(min, rttWindow[i]);
            max = Math.max(max, rttWindow[i]);
        }
        minRttUs = min;
        maxRttUs = max;
        lastRttUs = us;
    }

    private static void joinQuietly(Thread t) {
        if (t == null) return;
        try {
            t.join(1000);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
    }
}
