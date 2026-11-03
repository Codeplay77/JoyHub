package com.steamwifi.controller;

/**
 * Protocolo UDP com o servidor do PC. Espelha Windows/src/protocol.h, onde cada
 * pacote está documentado. Todos os campos multi-byte são little-endian.
 */
final class Protocol {
    static final int DEFAULT_PORT = 38500;

    static final byte TYPE_INPUT = 0x01;
    static final byte TYPE_PING = 0x02;
    static final byte TYPE_PONG = 0x03;
    static final byte TYPE_RUMBLE = 0x04;
    static final byte TYPE_DISCOVER = 0x05;
    static final byte TYPE_DISCOVER_REPLY = 0x06;
    static final byte TYPE_BYE = 0x07;

    static final int HEADER_SIZE = 8;
    static final int INPUT_FIXED_SIZE = 20;
    static final int STATE_SIZE = 12;
    static final int MAX_STATES = 4;
    static final int PING_FIXED_SIZE = 30;
    static final int PING_EXT_SIZE = 14;  // bytes da extensão depois do extLen
    static final int PONG_SIZE = 28;  // PCs antigos mandam só isto
    static final int PONG_EXT_SIZE = 40;
    static final int RUMBLE_SIZE = 16;
    static final int BYE_SIZE = 12;
    static final int MAX_NAME_LEN = 63;

    static final int PING_FLAG_PAUSED = 0x01;
    static final int PONG_FLAG_PAD_ACTIVE = 0x01;
    static final int PONG_FLAG_DRY_RUN = 0x02;
    static final int PONG_FLAG_NO_DRIVER = 0x04;
    static final int PHONE_FLAG_CHARGING = 0x01;
    static final int PHONE_FLAG_POWER_SAVE = 0x02;
    static final int RUMBLE_FLAG_TEST = 0x01;
    static final int UNKNOWN_PERCENT = 0xFF;
    static final int UNKNOWN_LOSS = 0xFFFF;

    private static final byte MAGIC0 = 'S';
    private static final byte MAGIC1 = 'W';
    private static final byte VERSION = 1;

    private Protocol() {}

    static void writeHeader(byte[] b, byte type, int clientId) {
        b[0] = MAGIC0;
        b[1] = MAGIC1;
        b[2] = VERSION;
        b[3] = type;
        writeInt(b, 4, clientId);
    }

    static boolean validHeader(byte[] b, int len) {
        return len >= HEADER_SIZE && b[0] == MAGIC0 && b[1] == MAGIC1 && b[2] == VERSION;
    }

    /**
     * Extensão do PING (1 + PING_EXT_SIZE bytes, logo depois do nome): estado do celular e
     * quantos pacotes saíram nesta sessão, para o PC calcular a perda. Valores negativos = desconhecido.
     */
    static void writePingExtension(byte[] b, int off, int battery, boolean charging, boolean powerSave,
                                   int wifiGeneration, int wifiMhz, int wifiDbm, int linkMbps, int sent,
                                   int padBattery) {
        b[off] = (byte) PING_EXT_SIZE;
        b[off + 1] = (byte) (battery >= 0 && battery <= 100 ? battery : UNKNOWN_PERCENT);
        b[off + 2] = (byte) ((charging ? PHONE_FLAG_CHARGING : 0) | (powerSave ? PHONE_FLAG_POWER_SAVE : 0));
        b[off + 3] = (byte) Math.max(0, Math.min(255, wifiGeneration));
        writeShort(b, off + 4, Math.max(0, Math.min(0xFFFF, wifiMhz)));
        b[off + 6] = (byte) Math.max(-128, Math.min(0, wifiDbm));
        b[off + 7] = 0;
        writeShort(b, off + 8, Math.max(0, Math.min(0xFFFF, linkMbps)));
        writeInt(b, off + 10, sent);
        b[off + 14] = (byte) (padBattery >= 0 && padBattery <= 100 ? padBattery : UNKNOWN_PERCENT);
    }

    /** Estado no layout de XUSB_REPORT (o PC copia direto para o driver). */
    static void writeState(byte[] b, int off, int buttons, int lt, int rt, int lx, int ly, int rx, int ry) {
        writeShort(b, off, buttons);
        b[off + 2] = (byte) lt;
        b[off + 3] = (byte) rt;
        writeShort(b, off + 4, lx);
        writeShort(b, off + 6, ly);
        writeShort(b, off + 8, rx);
        writeShort(b, off + 10, ry);
    }

    static void writeShort(byte[] b, int off, int v) {
        b[off] = (byte) v;
        b[off + 1] = (byte) (v >> 8);
    }

    static void writeInt(byte[] b, int off, int v) {
        b[off] = (byte) v;
        b[off + 1] = (byte) (v >> 8);
        b[off + 2] = (byte) (v >> 16);
        b[off + 3] = (byte) (v >> 24);
    }

    static void writeLong(byte[] b, int off, long v) {
        for (int i = 0; i < 8; i++) b[off + i] = (byte) (v >>> (8 * i));
    }

    static int readUShort(byte[] b, int off) {
        return (b[off] & 0xFF) | ((b[off + 1] & 0xFF) << 8);
    }

    static int readInt(byte[] b, int off) {
        return (b[off] & 0xFF) | ((b[off + 1] & 0xFF) << 8) | ((b[off + 2] & 0xFF) << 16) | ((b[off + 3] & 0xFF) << 24);
    }

    static long readLong(byte[] b, int off) {
        long v = 0;
        for (int i = 7; i >= 0; i--) v = (v << 8) | (b[off + i] & 0xFF);
        return v;
    }
}
