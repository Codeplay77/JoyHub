package com.steamwifi.controller;

import android.annotation.TargetApi;
import android.content.Context;
import android.os.Build;
import android.os.CombinedVibration;
import android.os.SystemClock;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.os.VibratorManager;
import android.view.InputDevice;

/**
 * Repassa a vibração do jogo para os motores do controle ou, se ele não tiver
 * (opcional), para o celular. Cada efeito dura 2 s e é renovado enquanto o PC manda
 * a vibração atual (a cada RUMBLE e a cada PONG): se a conexão cair, para sozinho.
 */
final class Rumble {
    private static final long EFFECT_MS = 2000;
    private static final long REARM_MS = 900;

    private final Vibrator phone;
    private volatile boolean phoneEnabled;
    private VibratorManager padManager;  // API 31+
    private int[] padIds = new int[0];
    private Vibrator padVibrator;  // API < 31
    private int padDeviceId = Integer.MIN_VALUE;
    private int lastLarge;
    private int lastSmall;
    private long lastArmMs;

    Rumble(Context context) {
        Vibrator v;
        if (Build.VERSION.SDK_INT >= 31) {
            VibratorManager vm = context.getSystemService(VibratorManager.class);
            v = vm != null ? vm.getDefaultVibrator() : null;
        } else {
            v = context.getSystemService(Vibrator.class);
        }
        phone = v != null && v.hasVibrator() ? v : null;
    }

    void setPhoneEnabled(boolean enabled) {
        phoneEnabled = enabled;
        if (!enabled && phone != null) phone.cancel();
    }

    synchronized void setController(InputDevice d) {
        // onInputDeviceChanged dispara por coisas como nível de bateria: o mesmo controle
        // continua valendo e a vibração em andamento não deve ser cortada.
        final int id = d != null ? d.getId() : Integer.MIN_VALUE;
        if (id == padDeviceId) return;
        padDeviceId = id;
        stopAll();
        padManager = null;
        padIds = new int[0];
        padVibrator = null;
        if (d == null) return;
        if (Build.VERSION.SDK_INT >= 31) {
            VibratorManager vm = d.getVibratorManager();
            int[] ids = vm.getVibratorIds();
            if (ids.length > 0) {
                padManager = vm;
                padIds = ids;
            }
        } else {
            Vibrator v = d.getVibrator();
            if (v != null && v.hasVibrator()) padVibrator = v;
        }
    }

    synchronized boolean controllerHasMotors() {
        return padManager != null || padVibrator != null;
    }

    /** Se uma vibração agora seria sentida (motores do controle ou, ativada, a do celular). */
    synchronized boolean canVibrate() {
        return controllerHasMotors() || (phoneEnabled && phone != null);
    }

    static boolean hasMotors(InputDevice d) {
        if (Build.VERSION.SDK_INT >= 31) return d.getVibratorManager().getVibratorIds().length > 0;
        Vibrator v = d.getVibrator();
        return v != null && v.hasVibrator();
    }

    synchronized void update(int large, int small) {
        final boolean changed = large != lastLarge || small != lastSmall;
        if (large == 0 && small == 0) {
            if (changed) stopAll();
            return;
        }
        final long now = SystemClock.uptimeMillis();
        if (!changed && now - lastArmMs < REARM_MS) return;
        lastLarge = large;
        lastSmall = small;
        lastArmMs = now;
        if (padManager != null) {
            vibratePad(large, small);
        } else if (padVibrator != null) {
            padVibrator.vibrate(oneShot(Math.max(large, small)));
        } else if (phoneEnabled && phone != null) {
            final int strength = Math.max(large, small);
            if (phone.hasAmplitudeControl()) {
                phone.vibrate(oneShot(strength));
            } else if (strength > 40) {  // motor sem controle de força: ignora vibrações sutis
                phone.vibrate(VibrationEffect.createOneShot(EFFECT_MS, VibrationEffect.DEFAULT_AMPLITUDE));
            }
        }
    }

    synchronized void stop() {
        stopAll();
    }

    @TargetApi(31)
    private void vibratePad(int large, int small) {
        if (padIds.length == 1) {
            padManager.vibrate(CombinedVibration.createParallel(oneShot(Math.max(large, small))));
            return;
        }
        // Convenção do Android: primeiro motor = esquerdo (forte), segundo = direito (fraco).
        CombinedVibration.ParallelCombination c = CombinedVibration.startParallel();
        if (large > 0) c.addVibrator(padIds[0], oneShot(large));
        if (small > 0) c.addVibrator(padIds[1], oneShot(small));
        padManager.vibrate(c.combine());
    }

    private void stopAll() {
        if (Build.VERSION.SDK_INT >= 31 && padManager != null) padManager.cancel();
        if (padVibrator != null) padVibrator.cancel();
        if (phone != null) phone.cancel();
        lastLarge = 0;
        lastSmall = 0;
    }

    private static VibrationEffect oneShot(int amplitude) {
        return VibrationEffect.createOneShot(EFFECT_MS, Math.max(1, Math.min(255, amplitude)));
    }
}
