package com.steamwifi.controller;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.Typeface;
import android.util.AttributeSet;
import android.view.View;

/** Desenho do controle, só para leitura; atualizado junto com a interface (a cada 100 ms). */
public final class GamepadView extends View {
    private static final int INK = Color.rgb(8, 14, 23);
    private static final int EDGE = Color.rgb(65, 89, 116);
    private static final int MUTED = Color.rgb(163, 180, 200);
    private static final int CYAN = Color.rgb(101, 226, 239);
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path shell = new Path();
    private int buttons, lt, rt, lx, ly, rx, ry;

    public GamepadView(Context context, AttributeSet attrs) {
        super(context, attrs);
        setWillNotDraw(false);
        paint.setTypeface(Typeface.create("sans-serif-medium", Typeface.NORMAL));
        shell.moveTo(100, 54);
        shell.cubicTo(73, 52, 61, 66, 53, 94);
        shell.lineTo(33, 158);
        shell.cubicTo(20, 204, 50, 214, 72, 187);
        shell.lineTo(103, 153);
        shell.cubicTo(148, 164, 212, 164, 257, 153);
        shell.lineTo(288, 187);
        shell.cubicTo(310, 214, 340, 204, 327, 158);
        shell.lineTo(307, 94);
        shell.cubicTo(299, 66, 287, 52, 260, 54);
        shell.cubicTo(221, 63, 139, 63, 100, 54);
        shell.close();
    }

    void setState(int buttons, int lt, int rt, int lx, int ly, int rx, int ry) {
        buttons = Math.max(0, buttons);
        if (this.buttons == buttons && this.lt == lt && this.rt == rt && this.lx == lx
                && this.ly == ly && this.rx == rx && this.ry == ry) return;
        this.buttons = buttons;
        this.lt = lt;
        this.rt = rt;
        this.lx = lx;
        this.ly = ly;
        this.rx = rx;
        this.ry = ry;
        invalidate();
    }

    @Override protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        float scale = Math.min(getWidth() / 360f, getHeight() / 220f);
        canvas.save();
        canvas.translate((getWidth() - 360 * scale) / 2, (getHeight() - 220 * scale) / 2);
        canvas.scale(scale, scale);
        fill(Color.rgb(18, 34, 49));
        canvas.drawOval(38, 176, 322, 213, paint);
        trigger(canvas, 74, lt, "LT");
        trigger(canvas, 236, rt, "RT");
        pill(canvas, 72, 42, 52, 16, "LB", GamepadMapper.LEFT_SHOULDER);
        pill(canvas, 236, 42, 52, 16, "RB", GamepadMapper.RIGHT_SHOULDER);
        fill(Color.rgb(26, 43, 62));
        canvas.drawPath(shell, paint);
        stroke(EDGE, 1.6f);
        canvas.drawPath(shell, paint);
        stick(canvas, 102, 99, lx, ly, GamepadMapper.LEFT_THUMB);
        stick(canvas, 224, 144, rx, ry, GamepadMapper.RIGHT_THUMB);

        key(canvas, 270, 79, "Y", GamepadMapper.Y, Color.rgb(255, 209, 138));
        key(canvas, 292, 101, "B", GamepadMapper.B, Color.rgb(255, 154, 158));
        key(canvas, 248, 101, "X", GamepadMapper.X, Color.rgb(133, 187, 255));
        key(canvas, 270, 123, "A", GamepadMapper.A, Color.rgb(131, 230, 181));

        dpad(canvas, 137, 125, GamepadMapper.DPAD_UP, "↑");
        dpad(canvas, 137, 161, GamepadMapper.DPAD_DOWN, "↓");
        dpad(canvas, 119, 143, GamepadMapper.DPAD_LEFT, "‹");
        dpad(canvas, 155, 143, GamepadMapper.DPAD_RIGHT, "›");
        fill(INK);
        canvas.drawRect(129, 135, 145, 151, paint);

        pill(canvas, 148, 91, 22, 16, "−", GamepadMapper.BACK);
        pill(canvas, 190, 91, 22, 16, "≡", GamepadMapper.START);
        fill(pressed(GamepadMapper.GUIDE) ? CYAN : INK);
        canvas.drawCircle(180, 76, 9, paint);
        stroke(pressed(GamepadMapper.GUIDE) ? INK : CYAN, 1.5f);
        canvas.drawArc(175, 70, 185, 80, 210, 120, false, paint);
        canvas.drawArc(177, 74, 183, 80, 210, 120, false, paint);
        fill(pressed(GamepadMapper.GUIDE) ? INK : CYAN);
        canvas.drawCircle(180, 79, 1, paint);
        canvas.restore();
    }

    private boolean pressed(int mask) { return (buttons & mask) != 0; }
    private void fill(int color) { paint.setStyle(Paint.Style.FILL); paint.setColor(color); }
    private void stroke(int color, float width) {
        paint.setStyle(Paint.Style.STROKE);
        paint.setColor(color);
        paint.setStrokeWidth(width);
    }
    private void label(Canvas c, String text, float x, float y, float size, int color) {
        fill(color);
        paint.setTextAlign(Paint.Align.CENTER);
        paint.setTextSize(size);
        c.drawText(text, x, y - (paint.ascent() + paint.descent()) / 2, paint);
    }
    private void trigger(Canvas c, float x, int value, String text) {
        label(c, text, x + 25, 12, 10, MUTED);
        fill(INK);
        c.drawRoundRect(x, 23, x + 50, 29, 3, 3, paint);
        if (value > 0) {
            fill(CYAN);
            c.drawRoundRect(x, 23, x + 50 * Math.min(255, value) / 255f, 29, 3, 3, paint);
        }
    }
    private void pill(Canvas c, float x, float y, float w, float h, String text, int mask) {
        fill(pressed(mask) ? CYAN : INK);
        c.drawRoundRect(x, y, x + w, y + h, 6, 6, paint);
        label(c, text, x + w / 2, y + h / 2, 10, pressed(mask) ? INK : MUTED);
    }
    private void key(Canvas c, float x, float y, String text, int mask, int color) {
        fill(pressed(mask) ? color : INK);
        c.drawCircle(x, y, 12, paint);
        stroke(pressed(mask) ? color : EDGE, 1);
        c.drawCircle(x, y, 12, paint);
        label(c, text, x, y, 11, pressed(mask) ? INK : color);
    }
    private void dpad(Canvas c, float x, float y, int mask, String text) {
        fill(pressed(mask) ? CYAN : INK);
        c.drawRoundRect(x - 8, y - 8, x + 8, y + 8, 3, 3, paint);
        label(c, text, x, y, 12, pressed(mask) ? INK : MUTED);
    }
    private void stick(Canvas c, float x, float y, int dx, int dy, int mask) {
        fill(INK);
        c.drawCircle(x, y, 25, paint);
        stroke(EDGE, 1);
        c.drawCircle(x, y, 25, paint);
        float cx = x + dx / 32768f * 10;
        float cy = y - dy / 32768f * 10;
        boolean moved = Math.abs(dx) > 1000 || Math.abs(dy) > 1000 || pressed(mask);
        fill(pressed(mask) ? CYAN : Color.rgb(42, 62, 83));
        c.drawCircle(cx, cy, 17, paint);
        stroke(moved ? CYAN : EDGE, 2);
        c.drawCircle(cx, cy, 17, paint);
        fill(pressed(mask) ? INK : MUTED);
        c.drawCircle(cx, cy, 2, paint);
    }
}
