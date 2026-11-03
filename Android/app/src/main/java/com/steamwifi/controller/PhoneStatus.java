package com.steamwifi.controller;

import android.content.Context;
import android.hardware.BatteryState;
import android.net.wifi.ScanResult;
import android.net.wifi.WifiInfo;
import android.net.wifi.WifiManager;
import android.os.BatteryManager;
import android.os.Build;
import android.os.PowerManager;
import android.view.InputDevice;

/**
 * Bateria e Wi-Fi do celular (e a bateria do controle, se ele tiver), lidos a cada 2 s numa
 * thread de fundo. Aparecem na tela e vão para o PC na extensão do PING. Imutável.
 */
final class PhoneStatus {
    static final int WEAK_SIGNAL_DBM = -75;

    final int battery;         // 0-100; -1 = desconhecida
    final boolean charging;
    final boolean powerSave;
    final int wifiGeneration;  // 4, 5, 6, 7; 0 = desconhecida
    final int wifiMhz;         // 0 = sem Wi-Fi
    final int wifiDbm;         // 0 = desconhecido
    final int linkMbps;        // 0 = desconhecida
    final int padBattery;      // 0-100; -1 = controle sem bateria ou desconhecida

    private PhoneStatus(int battery, boolean charging, boolean powerSave, int wifiGeneration, int wifiMhz,
                        int wifiDbm, int linkMbps, int padBattery) {
        this.battery = battery;
        this.charging = charging;
        this.powerSave = powerSave;
        this.wifiGeneration = wifiGeneration;
        this.wifiMhz = wifiMhz;
        this.wifiDbm = wifiDbm;
        this.linkMbps = linkMbps;
        this.padBattery = padBattery;
    }

    @SuppressWarnings("deprecation")  // getConnectionInfo: sinal, frequência e velocidade não exigem localização
    static PhoneStatus read(Context context, InputDevice pad) {
        int battery = -1;
        boolean charging = false;
        final BatteryManager bm = context.getSystemService(BatteryManager.class);
        if (bm != null) {
            final int capacity = bm.getIntProperty(BatteryManager.BATTERY_PROPERTY_CAPACITY);
            if (capacity >= 0 && capacity <= 100) battery = capacity;
            charging = bm.isCharging();
        }
        final PowerManager pm = context.getSystemService(PowerManager.class);
        final boolean powerSave = pm != null && pm.isPowerSaveMode();

        int generation = 0, mhz = 0, dbm = 0, link = 0;
        final WifiManager wm = context.getApplicationContext().getSystemService(WifiManager.class);
        final WifiInfo info = wm != null ? wm.getConnectionInfo() : null;
        if (info != null && info.getFrequency() > 0) {
            mhz = info.getFrequency();
            final int rssi = info.getRssi();
            if (rssi > -127 && rssi < 0) dbm = rssi;
            link = Math.max(0, info.getLinkSpeed());
            if (Build.VERSION.SDK_INT >= 30) generation = generation(info.getWifiStandard());
        }
        return new PhoneStatus(battery, charging, powerSave, generation, mhz, dbm, link, padBattery(pad));
    }

    boolean hasWifi() {
        return wifiMhz > 0;
    }

    boolean slowBand() {
        return wifiMhz >= 2400 && wifiMhz < 2500;
    }

    boolean weakSignal() {
        return wifiDbm < 0 && wifiDbm <= WEAK_SIGNAL_DBM;
    }

    boolean lowBattery() {
        return battery >= 0 && battery <= 15 && !charging;
    }

    /** "2,4", "5", "6" (GHz); null se desconhecida. */
    String bandGhz() {
        if (wifiMhz >= 2400 && wifiMhz < 2500) return "2,4";
        if (wifiMhz >= 4900 && wifiMhz < 5925) return "5";
        if (wifiMhz >= 5925 && wifiMhz < 7200) return "6";
        if (wifiMhz >= 57000) return "60";
        return null;
    }

    private static int generation(int standard) {
        switch (standard) {
            case ScanResult.WIFI_STANDARD_11N: return 4;
            case ScanResult.WIFI_STANDARD_11AC: return 5;
            case ScanResult.WIFI_STANDARD_11AX: return 6;
            case ScanResult.WIFI_STANDARD_11BE: return 7;
            default: return 0;
        }
    }

    private static int padBattery(InputDevice pad) {
        if (pad == null || Build.VERSION.SDK_INT < 31) return -1;
        final BatteryState state = pad.getBatteryState();
        if (!state.isPresent()) return -1;
        final float capacity = state.getCapacity();
        return Float.isNaN(capacity) ? -1 : Math.round(Math.max(0f, Math.min(1f, capacity)) * 100f);
    }
}
