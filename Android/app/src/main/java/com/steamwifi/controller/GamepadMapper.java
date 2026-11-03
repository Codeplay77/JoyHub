package com.steamwifi.controller;

import android.util.SparseArray;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;

import java.util.ArrayList;
import java.util.List;

/**
 * Converte eventos de controle do Android no estado de um controle Xbox 360 (XUSB).
 * Roda na thread principal e não aloca nada por evento.
 */
final class GamepadMapper {
    static final int DPAD_UP = 0x0001;
    static final int DPAD_DOWN = 0x0002;
    static final int DPAD_LEFT = 0x0004;
    static final int DPAD_RIGHT = 0x0008;
    static final int START = 0x0010;
    static final int BACK = 0x0020;
    static final int LEFT_THUMB = 0x0040;
    static final int RIGHT_THUMB = 0x0080;
    static final int LEFT_SHOULDER = 0x0100;
    static final int RIGHT_SHOULDER = 0x0200;
    static final int GUIDE = 0x0400;
    static final int A = 0x1000;
    static final int B = 0x2000;
    static final int X = 0x4000;
    static final int Y = 0x8000;

    // Estado de saída (o último calculado por update()).
    int buttons, leftTrigger, rightTrigger, thumbLX, thumbLY, thumbRX, thumbRY;

    private int keyButtons, hatButtons;
    private boolean digitalLT, digitalRT;
    private int analogLT, analogRT, lx, ly, rx, ry;
    private final SparseArray<AxisMap> maps = new SparseArray<>();

    static boolean isGamepad(InputDevice d) {
        if (d == null || d.isVirtual()) return false;
        int s = d.getSources();
        return (s & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
                || (s & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
    }

    /** Retorna true se a tecla pertence ao controle (o evento deve ser consumido). */
    boolean onKey(KeyEvent e) {
        final int code = e.getKeyCode();
        final int action = e.getAction();
        if (action != KeyEvent.ACTION_DOWN && action != KeyEvent.ACTION_UP) return false;
        final boolean down = action == KeyEvent.ACTION_DOWN;
        if (code == KeyEvent.KEYCODE_BUTTON_L2 || code == KeyEvent.KEYCODE_BUTTON_R2) {
            // Muitos controles mandam a tecla L2/R2 junto com o eixo analógico ao passar da
            // metade do curso. Se o gatilho é analógico a tecla é ignorada; senão o valor
            // pularia para 100% no meio do curso.
            final boolean left = code == KeyEvent.KEYCODE_BUTTON_L2;
            final AxisMap m = mapFor(e.getDevice());
            final boolean analog = m != null && (left ? m.left.length : m.right.length) > 0;
            if (!analog) {
                if (left) digitalLT = down;
                else digitalRT = down;
            }
            return true;
        }
        final int bit = buttonForKey(code);
        if (bit == 0) return false;
        keyButtons = down ? (keyButtons | bit) : (keyButtons & ~bit);
        return true;
    }

    /** Retorna true se o evento de eixo foi lido. */
    boolean onMotion(MotionEvent e) {
        if (e.getActionMasked() != MotionEvent.ACTION_MOVE) return false;
        final AxisMap m = mapFor(e.getDevice());
        if (m == null) return false;
        // Android: Y positivo = para baixo. XInput: Y positivo = para cima.
        lx = stick(e.getAxisValue(MotionEvent.AXIS_X));
        ly = stick(-e.getAxisValue(MotionEvent.AXIS_Y));
        rx = stick(e.getAxisValue(m.rightX));
        ry = stick(-e.getAxisValue(m.rightY));
        analogLT = m.readLeft(e);
        analogRT = m.readRight(e);
        final float hx = e.getAxisValue(MotionEvent.AXIS_HAT_X);
        final float hy = e.getAxisValue(MotionEvent.AXIS_HAT_Y);
        hatButtons = (hx < -0.5f ? DPAD_LEFT : 0) | (hx > 0.5f ? DPAD_RIGHT : 0)
                | (hy < -0.5f ? DPAD_UP : 0) | (hy > 0.5f ? DPAD_DOWN : 0);
        return true;
    }

    /** Recalcula o estado de saída; retorna true se mudou desde a última chamada. */
    boolean update() {
        final int b = keyButtons | hatButtons;
        final int lt = digitalLT ? 255 : analogLT;
        final int rt = digitalRT ? 255 : analogRT;
        if (b == buttons && lt == leftTrigger && rt == rightTrigger && lx == thumbLX && ly == thumbLY
                && rx == thumbRX && ry == thumbRY) {
            return false;
        }
        buttons = b;
        leftTrigger = lt;
        rightTrigger = rt;
        thumbLX = lx;
        thumbLY = ly;
        thumbRX = rx;
        thumbRY = ry;
        return true;
    }

    /** Faz o próximo update() retornar true (para reenviar o estado atual). */
    void invalidate() {
        buttons = -1;
    }

    /** Solta tudo (perda de foco, controle desconectado). */
    void reset() {
        keyButtons = hatButtons = 0;
        digitalLT = digitalRT = false;
        analogLT = analogRT = lx = ly = rx = ry = 0;
    }

    void forgetDevice(int deviceId) {
        maps.remove(deviceId);
    }

    static String describeButtons(int b) {
        if (b == 0) return "—";
        final StringBuilder s = new StringBuilder();
        append(s, b, A, "A");
        append(s, b, B, "B");
        append(s, b, X, "X");
        append(s, b, Y, "Y");
        append(s, b, LEFT_SHOULDER, "LB");
        append(s, b, RIGHT_SHOULDER, "RB");
        append(s, b, LEFT_THUMB, "LS");
        append(s, b, RIGHT_THUMB, "RS");
        append(s, b, DPAD_UP, "↑");
        append(s, b, DPAD_DOWN, "↓");
        append(s, b, DPAD_LEFT, "←");
        append(s, b, DPAD_RIGHT, "→");
        append(s, b, START, "START");
        append(s, b, BACK, "BACK");
        append(s, b, GUIDE, "GUIDE");
        return s.toString();
    }

    private static void append(StringBuilder s, int buttons, int mask, String label) {
        if ((buttons & mask) == 0) return;
        if (s.length() > 0) s.append("  ");
        s.append(label);
    }

    private static int stick(float v) {
        final int s = Math.round(v * 32767f);
        return s > 32767 ? 32767 : Math.max(s, -32768);
    }

    private static int buttonForKey(int code) {
        switch (code) {
            case KeyEvent.KEYCODE_BUTTON_A: return A;
            case KeyEvent.KEYCODE_BUTTON_B: return B;
            case KeyEvent.KEYCODE_BUTTON_X: return X;
            case KeyEvent.KEYCODE_BUTTON_Y: return Y;
            case KeyEvent.KEYCODE_BUTTON_L1: return LEFT_SHOULDER;
            case KeyEvent.KEYCODE_BUTTON_R1: return RIGHT_SHOULDER;
            case KeyEvent.KEYCODE_BUTTON_THUMBL: return LEFT_THUMB;
            case KeyEvent.KEYCODE_BUTTON_THUMBR: return RIGHT_THUMB;
            case KeyEvent.KEYCODE_BUTTON_START:
            case KeyEvent.KEYCODE_MENU: return START;
            case KeyEvent.KEYCODE_BUTTON_SELECT:
            case KeyEvent.KEYCODE_BACK: return BACK;  // alguns controles mandam BACK no botão "select"
            case KeyEvent.KEYCODE_BUTTON_MODE: return GUIDE;
            case KeyEvent.KEYCODE_DPAD_UP: return DPAD_UP;
            case KeyEvent.KEYCODE_DPAD_DOWN: return DPAD_DOWN;
            case KeyEvent.KEYCODE_DPAD_LEFT: return DPAD_LEFT;
            case KeyEvent.KEYCODE_DPAD_RIGHT: return DPAD_RIGHT;
            case KeyEvent.KEYCODE_DPAD_UP_LEFT: return DPAD_UP | DPAD_LEFT;
            case KeyEvent.KEYCODE_DPAD_UP_RIGHT: return DPAD_UP | DPAD_RIGHT;
            case KeyEvent.KEYCODE_DPAD_DOWN_LEFT: return DPAD_DOWN | DPAD_LEFT;
            case KeyEvent.KEYCODE_DPAD_DOWN_RIGHT: return DPAD_DOWN | DPAD_RIGHT;
            default: return 0;
        }
    }

    private AxisMap mapFor(InputDevice d) {
        if (d == null) return null;
        AxisMap m = maps.get(d.getId());
        if (m == null) {
            m = AxisMap.build(d);
            maps.put(d.getId(), m);
        }
        return m;
    }

    /** Quais eixos do Android correspondem ao analógico direito e aos gatilhos neste controle. */
    private static final class AxisMap {
        int rightX = MotionEvent.AXIS_Z;
        int rightY = MotionEvent.AXIS_RZ;
        TriggerAxis[] left = new TriggerAxis[0];
        TriggerAxis[] right = new TriggerAxis[0];

        static AxisMap build(InputDevice d) {
            final AxisMap m = new AxisMap();
            final boolean z = range(d, MotionEvent.AXIS_Z) != null;
            final boolean rz = range(d, MotionEvent.AXIS_RZ) != null;
            final boolean rxAxis = range(d, MotionEvent.AXIS_RX) != null;
            final boolean ryAxis = range(d, MotionEvent.AXIS_RY) != null;
            final List<TriggerAxis> l = new ArrayList<>(2);
            final List<TriggerAxis> r = new ArrayList<>(2);
            // Padrão Android: gatilhos em LTRIGGER/RTRIGGER e/ou BRAKE/GAS (alguns mandam os dois).
            add(d, MotionEvent.AXIS_LTRIGGER, l);
            add(d, MotionEvent.AXIS_BRAKE, l);
            add(d, MotionEvent.AXIS_RTRIGGER, r);
            add(d, MotionEvent.AXIS_GAS, r);
            if (z && rz) {
                // Analógico direito em Z/RZ (padrão). Controles antigos (ex.: DualShock 4
                // em Androids antigos) põem os gatilhos em RX/RY.
                if (l.isEmpty() && r.isEmpty() && rxAxis && ryAxis) {
                    add(d, MotionEvent.AXIS_RX, l);
                    add(d, MotionEvent.AXIS_RY, r);
                }
            } else if (rxAxis && ryAxis) {
                m.rightX = MotionEvent.AXIS_RX;
                m.rightY = MotionEvent.AXIS_RY;
            }
            m.left = l.toArray(new TriggerAxis[0]);
            m.right = r.toArray(new TriggerAxis[0]);
            return m;
        }

        int readLeft(MotionEvent e) {
            return read(left, e);
        }

        int readRight(MotionEvent e) {
            return read(right, e);
        }

        private static int read(TriggerAxis[] axes, MotionEvent e) {
            int v = 0;
            for (TriggerAxis a : axes) v = Math.max(v, a.read(e));
            return v;
        }

        private static InputDevice.MotionRange range(InputDevice d, int axis) {
            InputDevice.MotionRange r = d.getMotionRange(axis, InputDevice.SOURCE_JOYSTICK);
            return r != null ? r : d.getMotionRange(axis);
        }

        private static void add(InputDevice d, int axis, List<TriggerAxis> out) {
            final InputDevice.MotionRange r = range(d, axis);
            if (r != null) out.add(new TriggerAxis(axis, r.getMin(), r.getRange()));
        }
    }

    /** Gatilho analógico normalizado para 0..255 a partir da faixa que o controle informa. */
    private static final class TriggerAxis {
        final int axis;
        final float min;
        final float range;

        TriggerAxis(int axis, float min, float range) {
            this.axis = axis;
            this.min = min;
            this.range = range > 0f ? range : 1f;
        }

        int read(MotionEvent e) {
            final float v = (e.getAxisValue(axis) - min) / range;
            if (v <= 0f) return 0;
            if (v >= 1f) return 255;
            return Math.round(v * 255f);
        }
    }
}
