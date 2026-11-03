package com.steamwifi.controller;

import android.net.ConnectivityManager;
import android.net.Network;
import android.os.Handler;
import android.os.SystemClock;

import java.io.IOException;
import java.net.DatagramPacket;
import java.net.DatagramSocket;
import java.net.InetAddress;
import java.net.SocketTimeoutException;
import java.nio.charset.StandardCharsets;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/** Procura servidores na rede local por broadcast UDP (~1,5 s). */
final class Discovery {
    interface Callback {
        void onFound(String name, String ip, int port);

        void onFinished(int found);
    }

    private static final long DURATION_MS = 1500;
    private static final long RESEND_MS = 500;

    private Discovery() {}

    /** Roda numa thread própria; os callbacks chegam pelo handler (thread principal). */
    static void run(ConnectivityManager cm, int port, Handler handler, Callback cb) {
        new Thread(() -> {
            final Set<String> seen = new HashSet<>();
            final byte[] request = new byte[Protocol.HEADER_SIZE];
            Protocol.writeHeader(request, Protocol.TYPE_DISCOVER, 0);
            try (DatagramSocket s = new DatagramSocket()) {
                Network local = Networks.findLocalNetwork(cm);
                if (local != null) {
                    try {
                        local.bindSocket(s);
                    } catch (IOException ignored) {
                        // segue pela rota padrão
                    }
                }
                s.setBroadcast(true);
                s.setSoTimeout(100);
                final List<InetAddress> targets = Networks.broadcastAddresses();
                final byte[] buf = new byte[256];
                final DatagramPacket reply = new DatagramPacket(buf, buf.length);
                final long end = SystemClock.uptimeMillis() + DURATION_MS;
                long nextSend = 0;
                long now;
                while ((now = SystemClock.uptimeMillis()) < end) {
                    if (now >= nextSend) {
                        for (InetAddress t : targets) {
                            try {
                                s.send(new DatagramPacket(request, request.length, t, port));
                            } catch (IOException ignored) {
                                // interface sem rota para broadcast
                            }
                        }
                        nextSend = now + RESEND_MS;
                    }
                    try {
                        reply.setLength(buf.length);
                        s.receive(reply);
                    } catch (SocketTimeoutException e) {
                        continue;
                    }
                    final int len = reply.getLength();
                    if (!Protocol.validHeader(buf, len) || buf[3] != Protocol.TYPE_DISCOVER_REPLY || len < 11) continue;
                    final int replyPort = Protocol.readUShort(buf, 8);
                    final int nameLen = Math.min(buf[10] & 0xFF, len - 11);
                    final String name = new String(buf, 11, nameLen, StandardCharsets.UTF_8);
                    final String ip = reply.getAddress().getHostAddress();
                    if (seen.add(ip)) handler.post(() -> cb.onFound(name, ip, replyPort));
                }
            } catch (IOException ignored) {
                // sem rede: termina sem resultados
            }
            final int found = seen.size();
            handler.post(() -> cb.onFinished(found));
        }, "joyhub-discovery").start();
    }
}
