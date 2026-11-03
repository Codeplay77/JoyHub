package com.steamwifi.controller;

import android.annotation.TargetApi;
import android.app.Activity;
import android.content.SharedPreferences;
import android.content.res.Configuration;
import android.graphics.Insets;
import android.hardware.input.InputManager;
import android.net.ConnectivityManager;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.Looper;
import android.os.Process;
import android.os.StrictMode;
import android.os.SystemClock;
import android.provider.Settings;
import android.text.TextUtils;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputMethodManager;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import java.io.IOException;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.security.SecureRandom;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

public final class MainActivity extends Activity implements InputManager.InputDeviceListener, NetClient.Listener {
    private static final String PREFS = "swc";
    private static final String KEY_HOST = "host";
    private static final String KEY_HOST_NAME = "hostName";  // nome do PC achado pela busca (o IP pode mudar)
    private static final String KEY_CLIENT_ID = "clientId";
    private static final String KEY_PHONE_RUMBLE = "phoneRumble";
    private static final String KEY_AUTO_CONNECT = "autoConnect";
    private static final long UI_REFRESH_MS = 100;
    private static final long NO_REPLY_MS = 2000;
    private static final long PHONE_STATUS_MS = 2000;
    private static final long RUMBLE_TOAST_MS = 2000;
    private static final int HIGH_LOSS_BASIS_POINTS = 200;  // 2%
    private static final int UNBUFFERED_SOURCES = InputDevice.SOURCE_CLASS_JOYSTICK | InputDevice.SOURCE_CLASS_BUTTON;

    private final GamepadMapper mapper = new GamepadMapper();
    private final Handler ui = new Handler(Looper.getMainLooper());
    private final Set<Integer> gamepadIds = new HashSet<>();
    private SharedPreferences prefs;
    private InputManager inputManager;
    private ConnectivityManager connectivity;
    private WifiManager.WifiLock wifiLock;
    private Rumble rumble;
    private NetClient net;
    private boolean connecting;
    private boolean discovering;
    private boolean autoConnectArmed = true;  // uma vez por abertura do app; conectar/desconectar à mão desarma
    private String host = "";
    private long lastBackMs;
    private long rateBasePackets;
    private long rateBaseMs;
    private long packetRate;
    private PhoneStatus phoneStatus;
    private volatile InputDevice statusPad;  // controle cuja bateria aparece na tela
    private long lastRumbleToastMs;
    // Bateria e Wi-Fi são lidos fora da thread principal: ela envia os comandos do controle, e
    // uma chamada ao sistema demorada atrasaria um toque.
    private HandlerThread statusThread;
    private Handler statusHandler;
    private volatile boolean statusRunning;
    private final Runnable statusTask = new Runnable() {
        @Override
        public void run() {
            if (!statusRunning) return;
            final PhoneStatus s = PhoneStatus.read(MainActivity.this, statusPad);
            ui.post(() -> onPhoneStatus(s));
            statusHandler.postDelayed(this, PHONE_STATUS_MS);
        }
    };

    private View content;
    private View darkOverlay;
    private TextView controllerText;
    private TextView statusText;
    private TextView pingText;
    private TextView statsText;
    private TextView monitorText;
    private TextView sessionText;
    private TextView pingDetailText;
    private TextView statsDetailText;
    private TextView wifiText;
    private TextView wifiDetailText;
    private TextView batteryText;
    private TextView batteryDetailText;
    private GamepadView gamepadView;
    private EditText hostInput;
    private Button connectButton;
    private Button discoverButton;
    private LinearLayout discoveredList;

    private final Runnable refreshTask = new Runnable() {
        @Override
        public void run() {
            updateStatus();
            ui.postDelayed(this, UI_REFRESH_MS);
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // O pacote UDP sai na thread principal, dentro do próprio evento do controle, sem
        // fila nem troca de thread. Envio UDP não bloqueia, então liberamos rede aqui.
        StrictMode.setThreadPolicy(StrictMode.ThreadPolicy.LAX);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        setContentView(R.layout.activity_main);

        content = findViewById(R.id.content);
        darkOverlay = findViewById(R.id.darkOverlay);
        controllerText = findViewById(R.id.controllerText);
        statusText = findViewById(R.id.statusText);
        pingText = findViewById(R.id.pingText);
        statsText = findViewById(R.id.statsText);
        monitorText = findViewById(R.id.monitorText);
        sessionText = findViewById(R.id.sessionText);
        pingDetailText = findViewById(R.id.pingDetailText);
        statsDetailText = findViewById(R.id.statsDetailText);
        wifiText = findViewById(R.id.wifiText);
        wifiDetailText = findViewById(R.id.wifiDetailText);
        batteryText = findViewById(R.id.batteryText);
        batteryDetailText = findViewById(R.id.batteryDetailText);
        gamepadView = findViewById(R.id.gamepadView);
        findViewById(R.id.sessionHero).setClipToOutline(true);
        adaptLayout(getResources().getConfiguration());
        hostInput = findViewById(R.id.hostInput);
        connectButton = findViewById(R.id.connectButton);
        discoverButton = findViewById(R.id.discoverButton);
        discoveredList = findViewById(R.id.discoveredList);
        CheckBox phoneRumble = findViewById(R.id.phoneRumble);
        CheckBox autoConnect = findViewById(R.id.autoConnect);

        prefs = getSharedPreferences(PREFS, MODE_PRIVATE);
        inputManager = getSystemService(InputManager.class);
        connectivity = getSystemService(ConnectivityManager.class);
        rumble = new Rumble(this);
        statusThread = new HandlerThread("joyhub-status", Process.THREAD_PRIORITY_BACKGROUND);
        statusThread.start();
        statusHandler = new Handler(statusThread.getLooper());

        hostInput.setText(prefs.getString(KEY_HOST, ""));
        hostInput.setOnEditorActionListener((v, actionId, event) -> {
            if (actionId != EditorInfo.IME_ACTION_GO && actionId != EditorInfo.IME_ACTION_DONE) return false;
            toggleConnection();
            return true;
        });
        connectButton.setOnClickListener(v -> toggleConnection());
        discoverButton.setOnClickListener(v -> startDiscovery());
        phoneRumble.setChecked(prefs.getBoolean(KEY_PHONE_RUMBLE, true));
        rumble.setPhoneEnabled(phoneRumble.isChecked());
        phoneRumble.setOnCheckedChangeListener((b, checked) -> {
            prefs.edit().putBoolean(KEY_PHONE_RUMBLE, checked).apply();
            rumble.setPhoneEnabled(checked);
        });
        autoConnect.setChecked(prefs.getBoolean(KEY_AUTO_CONNECT, true));
        autoConnect.setOnCheckedChangeListener((b, checked) -> prefs.edit().putBoolean(KEY_AUTO_CONNECT, checked).apply());
        findViewById(R.id.darkButton).setOnClickListener(v -> setDark(true));
        darkOverlay.setOnClickListener(v -> setDark(false));

        if (Build.VERSION.SDK_INT >= 30) applyInsets();
        inputManager.registerInputDeviceListener(this, ui);
        refreshControllers();
        updateConnectButton();
        updateStatus();
        startDiscovery();
    }

    @Override
    public void onConfigurationChanged(Configuration config) {
        super.onConfigurationChanged(config);
        adaptLayout(config);
    }

    /** Painéis lado a lado ou empilhados: tablets e multijanela podem ignorar a orientação paisagem. */
    private void adaptLayout(Configuration config) {
        boolean narrow = config.screenWidthDp < 600;
        LinearLayout panels = findViewById(R.id.panels);
        panels.setOrientation(narrow ? LinearLayout.VERTICAL : LinearLayout.HORIZONTAL);
        View connection = findViewById(R.id.connectionScroll);
        View session = findViewById(R.id.sessionScroll);
        LinearLayout.LayoutParams left = new LinearLayout.LayoutParams(
                narrow ? -1 : 0, narrow ? 0 : -1, narrow ? 1 : 0.39f);
        LinearLayout.LayoutParams right = new LinearLayout.LayoutParams(
                narrow ? -1 : 0, narrow ? 0 : -1, narrow ? 1.2f : 0.61f);
        if (narrow) right.topMargin = dp(12); else right.setMarginStart(dp(12));
        connection.setLayoutParams(left);
        session.setLayoutParams(right);
        findViewById(R.id.networkBadge).setVisibility(narrow ? View.GONE : View.VISIBLE);
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    @Override
    protected void onStart() {
        super.onStart();
        if (net != null) {
            net.setPaused(false);
            acquireWifiLock();
        }
    }

    @Override
    protected void onStop() {
        // Em segundo plano o controle não chega ao app: para o tráfego (economiza bateria)
        // mas mantém a sessão, para voltar ao jogo sem reconectar.
        if (net != null) {
            neutralize();
            net.setPaused(true);
            releaseWifiLock();
        }
        super.onStop();
    }

    @Override
    protected void onResume() {
        super.onResume();
        ui.post(refreshTask);
        statusRunning = true;
        statusHandler.removeCallbacks(statusTask);
        statusHandler.post(statusTask);
        requestUnbufferedInput();
    }

    @Override
    protected void onPause() {
        super.onPause();
        ui.removeCallbacks(refreshTask);
        statusRunning = false;
        statusHandler.removeCallbacks(statusTask);
        neutralize();
    }

    @Override
    protected void onDestroy() {
        inputManager.unregisterInputDeviceListener(this);
        disconnect();
        rumble.stop();
        statusThread.quitSafely();
        super.onDestroy();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) {
            enterImmersive();
            requestUnbufferedInput();
        } else {
            neutralize();  // notificação/outro app na frente: os eventos param de chegar, então solta tudo
        }
    }

    // ---------------------------------------------------------------- entrada do controle

    @Override
    public boolean dispatchKeyEvent(KeyEvent e) {
        if (isControllerKey(e) && mapper.onKey(e)) {
            pushState();
            return true;  // consumido: o B não vira "voltar", o direcional não move o foco
        }
        if (e.getKeyCode() == KeyEvent.KEYCODE_BACK) return onBackKey(e);
        return super.dispatchKeyEvent(e);
    }

    /**
     * Teclas do controle. Em vários controles o botão Home/Guide chega por uma interface à
     * parte, que o Android não classifica como gamepad: BUTTON_MODE vale de qualquer aparelho físico.
     */
    private static boolean isControllerKey(KeyEvent e) {
        final InputDevice d = e.getDevice();
        if (GamepadMapper.isGamepad(d)) return true;
        return e.getKeyCode() == KeyEvent.KEYCODE_BUTTON_MODE && d != null && !d.isVirtual();
    }

    @Override
    public boolean dispatchGenericMotionEvent(MotionEvent e) {
        if ((e.getSource() & InputDevice.SOURCE_CLASS_JOYSTICK) != 0 && GamepadMapper.isGamepad(e.getDevice())) {
            if (mapper.onMotion(e)) pushState();
            return true;
        }
        return super.dispatchGenericMotionEvent(e);
    }

    private void pushState() {
        if (!mapper.update()) return;
        final NetClient n = net;
        if (n != null) {
            n.sendState(mapper.buttons, mapper.leftTrigger, mapper.rightTrigger, mapper.thumbLX, mapper.thumbLY,
                    mapper.thumbRX, mapper.thumbRY);
        }
    }

    private void neutralize() {
        mapper.reset();
        pushState();
    }

    private void requestUnbufferedInput() {
        if (Build.VERSION.SDK_INT < 30) return;
        // Sem isto o Android junta os eventos de analógico e só os entrega no próximo
        // quadro da tela: até 16 ms a mais de atraso a 60 Hz.
        content.requestUnbufferedDispatch(UNBUFFERED_SOURCES);
        getWindow().getDecorView().requestUnbufferedDispatch(UNBUFFERED_SOURCES);
    }

    @Override
    public void onInputDeviceAdded(int deviceId) {
        refreshControllers();
    }

    @Override
    public void onInputDeviceChanged(int deviceId) {
        mapper.forgetDevice(deviceId);
        refreshControllers();
    }

    @Override
    public void onInputDeviceRemoved(int deviceId) {
        mapper.forgetDevice(deviceId);
        final boolean wasGamepad = gamepadIds.contains(deviceId);
        refreshControllers();
        if (wasGamepad) neutralize();
    }

    private void refreshControllers() {
        gamepadIds.clear();
        final List<String> names = new ArrayList<>();
        InputDevice first = null;
        InputDevice withMotors = null;
        for (int id : InputDevice.getDeviceIds()) {
            final InputDevice d = InputDevice.getDevice(id);
            if (!GamepadMapper.isGamepad(d)) continue;
            gamepadIds.add(id);
            names.add(d.getName());
            if (first == null) first = d;
            if (withMotors == null && Rumble.hasMotors(d)) withMotors = d;
        }
        rumble.setController(withMotors != null ? withMotors : first);
        statusPad = first;
        if (statusRunning) {  // relê já: o controle (e a bateria dele) pode ter mudado
            statusHandler.removeCallbacks(statusTask);
            statusHandler.post(statusTask);
        }
        if (names.isEmpty()) {
            controllerText.setText(R.string.controller_none);
            controllerText.setTextColor(getColor(R.color.dim));
        } else {
            String text = getString(R.string.controller_found, TextUtils.join(", ", names));
            if (!rumble.controllerHasMotors()) text += " " + getString(R.string.controller_no_motors);
            controllerText.setText(text);
            controllerText.setTextColor(getColor(R.color.success));
        }
    }

    // ---------------------------------------------------------------- conexão

    private void toggleConnection() {
        autoConnectArmed = false;
        if (net != null) {
            disconnect();
        } else {
            connect(hostInput.getText().toString(), null);
        }
    }

    /** pcName: nome anunciado pelo PC na busca; null quando o endereço foi digitado. */
    private void connect(String text, String pcName) {
        if (connecting) return;
        final String target = text.trim();
        if (target.isEmpty()) {
            Toast.makeText(this, R.string.invalid_host, Toast.LENGTH_SHORT).show();
            return;
        }
        String hostPart = target;
        int port = Protocol.DEFAULT_PORT;
        final int colon = target.lastIndexOf(':');
        if (colon > 0 && target.indexOf(':') == colon) {  // "ip:porta"
            try {
                port = Integer.parseInt(target.substring(colon + 1));
                hostPart = target.substring(0, colon);
            } catch (NumberFormatException ignored) {
                // não era porta: usa o texto inteiro como endereço
            }
        }

        disconnect();
        connecting = true;
        host = target;
        final SharedPreferences.Editor saved = prefs.edit().putString(KEY_HOST, target);
        if (pcName != null) {
            saved.putString(KEY_HOST_NAME, pcName);
        } else if (!target.equals(prefs.getString(KEY_HOST, ""))) {
            saved.remove(KEY_HOST_NAME);  // outro endereço digitado: o nome antigo não vale mais
        }
        saved.apply();
        hideKeyboard();
        updateConnectButton();

        final String h = hostPart;
        final int p = port;
        final int clientId = clientId();
        final String name = deviceName();
        new Thread(() -> {
            NetClient client = null;
            String error = null;
            try {
                client = NetClient.open(connectivity, new InetSocketAddress(InetAddress.getByName(h), p), clientId,
                        name, this);
            } catch (IOException | RuntimeException ex) {
                error = ex.getMessage() != null ? ex.getMessage() : ex.getClass().getSimpleName();
            }
            final NetClient c = client;
            final String err = error;
            ui.post(() -> onConnectFinished(c, err));
        }, "joyhub-connect").start();
    }

    private void onConnectFinished(NetClient c, String error) {
        connecting = false;
        if (c == null) {
            Toast.makeText(this, getString(R.string.connect_failed, error), Toast.LENGTH_LONG).show();
            updateConnectButton();
            return;
        }
        if (isFinishing() || isDestroyed()) {
            c.stop();
            return;
        }
        net = c;
        acquireWifiLock();
        c.setPhoneStatus(phoneStatus);  // o primeiro PING já leva bateria e Wi-Fi
        c.start();
        mapper.invalidate();
        pushState();  // o PC recebe o estado atual na hora
        content.requestFocus();
        requestUnbufferedInput();
        rateBasePackets = 0;
        rateBaseMs = SystemClock.uptimeMillis();
        packetRate = 0;
        updateConnectButton();
    }

    private void disconnect() {
        final NetClient n = net;
        if (n == null) return;
        net = null;
        n.stop();
        rumble.stop();
        releaseWifiLock();
        updateConnectButton();
    }

    @Override
    public void onFeedback(int largeMotor, int smallMotor) {
        rumble.update(largeMotor, smallMotor);
    }

    @Override
    public void onRumbleTest() {
        ui.post(() -> {
            final long now = SystemClock.uptimeMillis();
            if (now - lastRumbleToastMs < RUMBLE_TOAST_MS) return;  // o teste chega em vários pacotes
            lastRumbleToastMs = now;
            Toast.makeText(this, rumble.canVibrate() ? R.string.rumble_test : R.string.rumble_test_off,
                    Toast.LENGTH_SHORT).show();
        });
    }

    private void acquireWifiLock() {
        if (wifiLock == null) {
            final WifiManager wm = getApplicationContext().getSystemService(WifiManager.class);
            if (wm == null) return;
            // Desliga a economia de energia do Wi-Fi enquanto o app está na tela: sem isso o
            // rádio "cochila" entre pacotes e um envio pode atrasar dezenas de ms.
            final int mode = Build.VERSION.SDK_INT >= 29
                    ? WifiManager.WIFI_MODE_FULL_LOW_LATENCY
                    : WifiManager.WIFI_MODE_FULL_HIGH_PERF;
            wifiLock = wm.createWifiLock(mode, "JoyHub:latency");
            wifiLock.setReferenceCounted(false);
        }
        wifiLock.acquire();
    }

    private void releaseWifiLock() {
        if (wifiLock != null && wifiLock.isHeld()) wifiLock.release();
    }

    private void startDiscovery() {
        if (discovering) return;
        discovering = true;
        discoveredList.removeAllViews();
        discoverButton.setEnabled(false);
        discoverButton.setText(R.string.discovering);
        Discovery.run(connectivity, Protocol.DEFAULT_PORT, ui, new Discovery.Callback() {
            @Override
            public void onFound(String name, String ip, int port) {
                addDiscovered(name, ip, port);
            }

            @Override
            public void onFinished(int found) {
                discovering = false;
                discoverButton.setEnabled(true);
                discoverButton.setText(R.string.discover);
                if (found == 0) {
                    final TextView t = new TextView(MainActivity.this);
                    t.setText(R.string.discover_none);
                    t.setTextColor(getColor(R.color.dim));
                    t.setTextSize(13);
                    t.setLineSpacing(dp(3), 1);
                    t.setPadding(dp(4), dp(4), dp(4), dp(4));
                    discoveredList.addView(t);
                }
            }
        });
    }

    private void addDiscovered(String name, String ip, int port) {
        final String target = port == Protocol.DEFAULT_PORT ? ip : ip + ":" + port;
        final Button b = new Button(this);
        b.setAllCaps(false);
        b.setText(getString(R.string.discovered_pc, name, target));
        b.setCompoundDrawablesRelativeWithIntrinsicBounds(R.drawable.ic_pc, 0, 0, 0);
        b.setGravity(android.view.Gravity.START | android.view.Gravity.CENTER_VERTICAL);
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(-1, -2);
        params.bottomMargin = dp(8);
        b.setLayoutParams(params);
        b.setOnClickListener(v -> {
            autoConnectArmed = false;
            hostInput.setText(target);
            connect(target, name);
        });
        discoveredList.addView(b);
        maybeAutoConnect(name, target);
    }

    /** Ao abrir o app: conecta sozinho se a busca achar o último PC (pelo endereço ou, se o IP mudou, pelo nome). */
    private void maybeAutoConnect(String name, String target) {
        if (!autoConnectArmed || net != null || connecting || !prefs.getBoolean(KEY_AUTO_CONNECT, true)) return;
        final String savedName = prefs.getString(KEY_HOST_NAME, "");
        final boolean samePc = target.equals(prefs.getString(KEY_HOST, ""))
                || (!savedName.isEmpty() && savedName.equals(name));
        if (!samePc) return;
        autoConnectArmed = false;
        hostInput.setText(target);
        connect(target, name);
    }

    // ---------------------------------------------------------------- interface

    private void updateConnectButton() {
        connectButton.setEnabled(!connecting);
        connectButton.setText(connecting ? R.string.connecting : (net != null ? R.string.disconnect : R.string.connect));
        connectButton.setBackgroundResource(net != null ? R.drawable.bg_button_secondary : R.drawable.bg_button_primary);
        connectButton.setTextColor(getColorStateList(net != null ? R.color.button_secondary_text : R.color.button_primary_text));
        hostInput.setEnabled(net == null && !connecting);
    }

    private void onPhoneStatus(PhoneStatus s) {
        if (isDestroyed()) return;
        phoneStatus = s;
        final NetClient n = net;
        if (n != null) n.setPhoneStatus(s);
        showPhoneStatus(s);
    }

    private void updateStatus() {
        final NetClient n = net;
        final long now = SystemClock.uptimeMillis();
        statsDetailText.setTextColor(getColor(R.color.dim));
        if (n == null) {
            statusText.setText(connecting ? R.string.status_connecting : R.string.status_disconnected);
            statusText.setTextColor(getColor(R.color.text));
            sessionText.setText(connecting ? getString(R.string.session_connecting, host) : getString(R.string.session_idle));
            pingText.setText(R.string.ping_none);
            pingDetailText.setText(R.string.ping_idle);
            statsText.setText(R.string.stats_none);
            statsDetailText.setText(R.string.stats_idle);
        } else {
            final boolean alive = n.lastPongMs != 0 && now - n.lastPongMs < NO_REPLY_MS;
            if (!alive) {
                statusText.setText(R.string.status_no_reply);
                sessionText.setText(getString(R.string.session_no_reply, host));
                statusText.setTextColor(getColor(R.color.error));
            } else if ((n.pcFlags & Protocol.PONG_FLAG_DRY_RUN) != 0) {
                statusText.setText(R.string.status_dry_run);
                sessionText.setText(getString(R.string.session_dry_run, host));
                statusText.setTextColor(getColor(R.color.warn));
            } else if ((n.pcFlags & Protocol.PONG_FLAG_NO_DRIVER) != 0) {
                statusText.setText(R.string.status_no_driver);
                sessionText.setText(getString(R.string.session_no_driver, host));
                statusText.setTextColor(getColor(R.color.warn));
            } else if (n.player >= 0) {
                statusText.setText(getString(R.string.status_connected_player, n.player + 1));
                sessionText.setText(getString(R.string.session_host, host));
                statusText.setTextColor(getColor(R.color.success));
            } else {
                statusText.setText(R.string.status_connected);
                sessionText.setText(getString(R.string.session_host, host));
                statusText.setTextColor(getColor(R.color.success));
            }
            final int rtt = n.lastRttUs;
            if (alive && rtt >= 0) {
                pingText.setText(getString(R.string.ping_value, rtt / 1000f));
                pingDetailText.setText(getString(R.string.ping_range, n.minRttUs / 1000f, n.maxRttUs / 1000f));
            } else {
                pingText.setText(R.string.ping_none);
                pingDetailText.setText(R.string.ping_idle);
            }
            final long sent = n.packetsSent;
            if (now - rateBaseMs >= 1000) {
                packetRate = (sent - rateBasePackets) * 1000 / (now - rateBaseMs);
                rateBasePackets = sent;
                rateBaseMs = now;
            }
            statsText.setText(getString(R.string.stats, packetRate));
            final int loss = n.lossBasisPoints;
            if (alive && loss >= 0) {  // PC novo: perda medida por ele e toques salvos pela redundância
                statsDetailText.setText(getString(R.string.stats_loss, loss / 100f, n.recoveredStates));
                if (loss >= HIGH_LOSS_BASIS_POINTS) statsDetailText.setTextColor(getColor(R.color.warn));
            } else {
                statsDetailText.setText(getString(R.string.stats_total, sent));
            }
        }
        gamepadView.setState(mapper.buttons, mapper.leftTrigger, mapper.rightTrigger,
                mapper.thumbLX, mapper.thumbLY, mapper.thumbRX, mapper.thumbRY);
        monitorText.setText(getString(R.string.monitor,
                GamepadMapper.describeButtons(mapper.buttons < 0 ? 0 : mapper.buttons),
                mapper.leftTrigger, mapper.rightTrigger, mapper.thumbLX, mapper.thumbLY, mapper.thumbRX,
                mapper.thumbRY));
        if (darkOverlay.getVisibility() == View.VISIBLE) {
            // Tela escura: uma linha discreta confirma que a conexão continua de pé.
            ((TextView) darkOverlay).setText(n != null
                    ? getString(R.string.dark_overlay_status, statusText.getText(), pingText.getText())
                    : getString(R.string.dark_overlay_hint));
        }
    }

    private void showPhoneStatus(PhoneStatus s) {
        if (!s.hasWifi()) {
            wifiText.setText(R.string.wifi_none);
            wifiText.setTextColor(getColor(R.color.text));
            wifiDetailText.setText(R.string.wifi_off);
        } else {
            final String band = s.bandGhz();
            wifiText.setText(band != null ? getString(R.string.wifi_band, band) : getString(R.string.wifi_mhz, s.wifiMhz));
            wifiText.setTextColor(getColor(s.slowBand() || s.weakSignal() ? R.color.warn : R.color.accent));
            if (s.slowBand()) {
                wifiDetailText.setText(R.string.wifi_slow_band);
            } else if (s.weakSignal()) {
                wifiDetailText.setText(getString(R.string.wifi_weak, s.wifiDbm));
            } else {
                final List<String> parts = new ArrayList<>(3);
                if (s.wifiGeneration > 0) parts.add(getString(R.string.wifi_generation, s.wifiGeneration));
                if (s.wifiDbm < 0) parts.add(getString(R.string.wifi_signal, s.wifiDbm));
                if (s.linkMbps > 0) parts.add(getString(R.string.wifi_speed, s.linkMbps));
                wifiDetailText.setText(parts.isEmpty() ? getString(R.string.wifi_connected) : TextUtils.join(" · ", parts));
            }
        }

        batteryText.setText(s.battery >= 0 ? getString(R.string.battery_value, s.battery) : getString(R.string.battery_none));
        batteryText.setTextColor(getColor(s.lowBattery() ? R.color.warn : R.color.text));
        final List<String> parts = new ArrayList<>(2);
        parts.add(getString(s.powerSave ? R.string.battery_saver
                : s.charging ? R.string.battery_charging : R.string.battery_discharging));
        if (s.padBattery >= 0) parts.add(getString(R.string.battery_pad, s.padBattery));
        batteryDetailText.setText(TextUtils.join(" · ", parts));
        batteryDetailText.setTextColor(getColor(s.powerSave ? R.color.warn : R.color.dim));
    }

    private boolean onBackKey(KeyEvent e) {
        if (e.getAction() != KeyEvent.ACTION_UP) return true;
        if (darkOverlay.getVisibility() == View.VISIBLE) {
            setDark(false);
            return true;
        }
        final long now = SystemClock.uptimeMillis();
        if (net == null || now - lastBackMs < 2000) {
            finish();
        } else {
            // Um gesto de voltar sem querer no meio do jogo não derruba a conexão.
            lastBackMs = now;
            Toast.makeText(this, R.string.press_back_again, Toast.LENGTH_SHORT).show();
        }
        return true;
    }

    private void setDark(boolean on) {
        darkOverlay.setVisibility(on ? View.VISIBLE : View.GONE);
        final WindowManager.LayoutParams lp = getWindow().getAttributes();
        lp.screenBrightness = on ? 0.01f : WindowManager.LayoutParams.BRIGHTNESS_OVERRIDE_NONE;
        getWindow().setAttributes(lp);
    }

    private void hideKeyboard() {
        final InputMethodManager imm = getSystemService(InputMethodManager.class);
        if (imm != null) imm.hideSoftInputFromWindow(hostInput.getWindowToken(), 0);
        hostInput.clearFocus();
        content.requestFocus();
    }

    @SuppressWarnings("deprecation")
    private void enterImmersive() {
        if (Build.VERSION.SDK_INT >= 30) {
            final WindowInsetsController c = getWindow().getInsetsController();
            if (c != null) {
                c.hide(WindowInsets.Type.systemBars());
                c.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                    | View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION);
        }
    }

    @TargetApi(30)
    private void applyInsets() {
        // Com edge-to-edge (Android 15+) o conteúdo fica atrás do recorte da câmera e do teclado.
        final int base = content.getPaddingLeft();
        content.setOnApplyWindowInsetsListener((v, insets) -> {
            final Insets i = insets.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout()
                    | WindowInsets.Type.ime());
            v.setPadding(base + i.left, base + i.top, base + i.right, base + i.bottom);
            return insets;
        });
    }

    private int clientId() {
        int id = prefs.getInt(KEY_CLIENT_ID, 0);
        if (id == 0) {
            id = new SecureRandom().nextInt();
            if (id == 0) id = 1;
            prefs.edit().putInt(KEY_CLIENT_ID, id).apply();
        }
        return id;
    }

    private String deviceName() {
        final String name = Settings.Global.getString(getContentResolver(), Settings.Global.DEVICE_NAME);
        return name != null && !name.isEmpty() ? name : Build.MODEL;
    }
}
