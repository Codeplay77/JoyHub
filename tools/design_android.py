"""Gera de novo os recursos de design do Android (cores, estilos, textos, vetores) e os SVGs da marca."""
from pathlib import Path

RES = Path(__file__).resolve().parents[1] / 'Android/app/src/main/res'

def write(name, content):
    path = RES / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content.strip() + '\n', encoding='utf-8')

write('values/colors.xml', '''<?xml version="1.0" encoding="utf-8"?>
<resources>
    <color name="bg">#080E17</color>
    <color name="surface">#101B29</color>
    <color name="surface_raised">#172638</color>
    <color name="stroke">#2A3B4F</color>
    <color name="text">#F0F6FC</color>
    <color name="dim">#A3B4C8</color>
    <color name="accent">#65E2EF</color>
    <color name="accent_ink">#071E29</color>
    <color name="success">#83E6B5</color>
    <color name="warn">#FFD18A</color>
    <color name="error">#FF9A9E</color>
    <color name="icon_bg">#101D30</color>
</resources>''')

write('values/themes.xml', '''<?xml version="1.0" encoding="utf-8"?>
<resources>
    <style name="AppTheme" parent="@android:style/Theme.Material.NoActionBar">
        <item name="android:windowBackground">@color/bg</item>
        <item name="android:fontFamily">sans</item>
        <item name="android:colorAccent">@color/accent</item>
        <item name="android:colorPrimary">@color/bg</item>
        <item name="android:textColorPrimary">@color/text</item>
        <item name="android:textColorSecondary">@color/dim</item>
        <item name="android:statusBarColor">@color/bg</item>
        <item name="android:navigationBarColor">@color/bg</item>
        <item name="android:windowLightStatusBar">false</item>
        <item name="android:windowActionModeOverlay">true</item>
        <item name="android:buttonStyle">@style/SecondaryButton</item>
    </style>
    <style name="SectionLabel">
        <item name="android:layout_width">match_parent</item>
        <item name="android:layout_height">wrap_content</item>
        <item name="android:textSize">11sp</item>
        <item name="android:textColor">@color/dim</item>
        <item name="android:fontFamily">sans-serif-medium</item>
        <item name="android:letterSpacing">0.13</item>
    </style>
    <style name="BodyText">
        <item name="android:layout_width">match_parent</item>
        <item name="android:layout_height">wrap_content</item>
        <item name="android:textSize">13sp</item>
        <item name="android:textColor">@color/dim</item>
        <item name="android:lineSpacingExtra">3dp</item>
    </style>
    <style name="SecondaryButton" parent="@android:style/Widget.Material.Button">
        <item name="android:layout_width">match_parent</item>
        <item name="android:layout_height">wrap_content</item>
        <item name="android:minHeight">48dp</item>
        <item name="android:minWidth">0dp</item>
        <item name="android:paddingStart">14dp</item>
        <item name="android:paddingEnd">14dp</item>
        <item name="android:paddingTop">10dp</item>
        <item name="android:paddingBottom">10dp</item>
        <item name="android:background">@drawable/bg_button_secondary</item>
        <item name="android:backgroundTint">@null</item>
        <item name="android:textColor">@color/button_secondary_text</item>
        <item name="android:textSize">13sp</item>
        <item name="android:textAllCaps">false</item>
        <item name="android:fontFamily">sans-serif-medium</item>
        <item name="android:stateListAnimator">@null</item>
        <item name="android:drawablePadding">10dp</item>
    </style>
    <style name="PrimaryButton" parent="SecondaryButton">
        <item name="android:background">@drawable/bg_button_primary</item>
        <item name="android:textColor">@color/button_primary_text</item>
        <item name="android:textSize">14sp</item>
    </style>
</resources>''')

write('values/strings.xml', r'''<?xml version="1.0" encoding="utf-8"?>
<resources xmlns:tools="http://schemas.android.com/tools" tools:locale="pt">
    <string name="app_name">JoyHub</string>
    <string name="brand_title">JoyHub</string>
    <string name="brand_caption">SEU CONTROLE. SEM FIOS.</string>
    <string name="local_network">CONEXÃO LOCAL</string>
    <string name="connection_heading">Conecte ao seu PC</string>
    <string name="connection_caption">Celular e computador na mesma rede.</string>
    <string name="host_label">ENDEREÇO DO COMPUTADOR</string>
    <string name="host_hint">Ex.: 192.168.0.10</string>
    <string name="connect">Conectar ao PC</string>
    <string name="disconnect">Desconectar</string>
    <string name="connecting">Conectando…</string>
    <string name="invalid_host">Digite o IP do computador</string>
    <string name="connect_failed">Falha ao conectar: %1$s</string>
    <string name="discover">Buscar na rede</string>
    <string name="discovering">Buscando computadores…</string>
    <string name="discovered_pc">%1$s\n%2$s  ·  Toque para conectar</string>
    <string name="discover_none">Nenhum PC por aqui ainda. Abra o JoyHub no computador ou informe o IP acima.</string>
    <string name="discover_help">Se o PC não aparecer, confira o firewall (UDP 38500) e se ambos estão na mesma rede.</string>
    <string name="preferences_label">DO SEU JEITO</string>
    <string name="phone_rumble">Vibração no celular</string>
    <string name="rumble_hint">Usada quando o controle não tem motor.</string>
    <string name="auto_connect">Conectar sozinho ao último PC</string>
    <string name="auto_connect_hint">Quando o app abre e encontra o mesmo computador na rede.</string>
    <string name="dark_mode">Escurecer tela</string>
    <string name="dark_hint">Mantenha a conexão com o brilho mínimo.</string>
    <string name="dark_overlay_hint">Toque para voltar</string>
    <string name="dark_overlay_status">%1$s · %2$s\n\nToque para voltar</string>
    <string name="session_label">SUA SESSÃO</string>
    <string name="status_disconnected">Vamos jogar?</string>
    <string name="status_connecting">Preparando a conexão…</string>
    <string name="status_no_reply">Aguardando o computador</string>
    <string name="status_connected">Tudo pronto para jogar</string>
    <string name="status_connected_player">Você é o jogador %1$d</string>
    <string name="status_dry_run">Conectado em modo teste</string>
    <string name="status_no_driver">Falta o driver no PC</string>
    <string name="session_no_driver">%1$s · Instale o ViGEmBus no computador.</string>
    <string name="session_idle">Conecte um controle e escolha seu PC.</string>
    <string name="session_connecting">Estabelecendo conexão com %1$s</string>
    <string name="session_host">PC · %1$s</string>
    <string name="session_no_reply">%1$s · Confira o servidor e o firewall.</string>
    <string name="session_dry_run">%1$s · Sem controle virtual no PC.</string>
    <string name="latency_label">LATÊNCIA</string>
    <string name="traffic_label">ENVIO DE COMANDOS</string>
    <string name="ping_none">— ms</string>
    <string name="ping_value">%1$.1f ms</string>
    <string name="ping_range">mín %1$.1f · máx %2$.1f ms</string>
    <string name="ping_idle">Aguardando conexão</string>
    <string name="stats">%1$d /s</string>
    <string name="stats_total">Total de pacotes: %1$d</string>
    <string name="stats_none">— /s</string>
    <string name="stats_idle">Nenhum pacote enviado</string>
    <string name="stats_loss">Perda %1$.1f%% · %2$d recuperados</string>
    <string name="wifi_label">REDE WI-FI</string>
    <string name="wifi_none">—</string>
    <string name="wifi_off">Sem Wi-Fi conectado</string>
    <string name="wifi_connected">Conectado</string>
    <string name="wifi_band">%1$s GHz</string>
    <string name="wifi_mhz">%1$d MHz</string>
    <string name="wifi_generation">Wi-Fi %1$d</string>
    <string name="wifi_signal">%1$d dBm</string>
    <string name="wifi_speed">%1$d Mbps</string>
    <string name="wifi_slow_band">Prefira 5 GHz: menos atraso</string>
    <string name="wifi_weak">Sinal fraco (%1$d dBm): chegue perto do roteador</string>
    <string name="battery_label">BATERIA</string>
    <string name="battery_none">—</string>
    <string name="battery_value">%1$d%%</string>
    <string name="battery_charging">Carregando</string>
    <string name="battery_discharging">Na bateria</string>
    <string name="battery_saver">Economia de energia ligada</string>
    <string name="battery_pad">Controle %1$d%%</string>
    <string name="controller_label">CONTROLE EM TEMPO REAL</string>
    <string name="controller_none">Conecte seu controle por USB ou Bluetooth.</string>
    <string name="controller_found">%1$s</string>
    <string name="controller_no_motors">· Vibração no celular disponível</string>
    <string name="controller_preview">Monitor visual do controle. Os comandos também são exibidos em texto abaixo.</string>
    <string name="monitor_label">LEITURA DOS COMANDOS</string>
    <string name="monitor">Botões: %1$s\nLT %2$3d   RT %3$3d\nL (%4$d, %5$d)   R (%6$d, %7$d)</string>
    <string name="hint">Dica de conexão: use Wi-Fi 5 GHz e conecte o PC por cabo. Deixe este app aberto enquanto joga.</string>
    <string name="press_back_again">Pressione voltar de novo para sair</string>
    <string name="rumble_test">O PC testou a vibração</string>
    <string name="rumble_test_off">O PC testou a vibração. Ative \"Vibração no celular\" para sentir.</string>
</resources>'''.replace('\\\\n', '\\n'))

for kind, color in [('primary', 'accent_ink'), ('secondary', 'text')]:
    write(f'color/button_{kind}_text.xml', f'''<selector xmlns:android="http://schemas.android.com/apk/res/android">
        <item android:state_enabled="false" android:color="@color/dim" />
        <item android:color="@color/{color}" />
    </selector>''')

def shape(color, radius=12, stroke=None, width=1):
    return f'<shape><solid android:color="{color}" /><corners android:radius="{radius}dp" />' + (f'<stroke android:width="{width}dp" android:color="{stroke}" />' if stroke else '') + '</shape>'

for name, color, radius in [('bg_card', '@color/surface', 20), ('bg_metric', '#E6122030', 12)]:
    write(f'drawable/{name}.xml', shape(color, radius, '@color/stroke').replace('<shape>', '<shape xmlns:android="http://schemas.android.com/apk/res/android">'))
write('drawable/bg_input.xml', '<selector xmlns:android="http://schemas.android.com/apk/res/android"><item android:state_focused="true">' + shape('@color/bg', stroke='@color/accent', width=2) + '</item><item>' + shape('@color/bg', stroke='@color/stroke') + '</item></selector>')
write('drawable/bg_button_primary.xml', '<ripple xmlns:android="http://schemas.android.com/apk/res/android" android:color="#4065E2EF"><item><selector><item android:state_enabled="false">' + shape('@color/surface_raised') + '</item><item android:state_focused="true">' + shape('#B2F5FC') + '</item><item>' + shape('@color/accent') + '</item></selector></item></ripple>')
write('drawable/bg_button_secondary.xml', '<ripple xmlns:android="http://schemas.android.com/apk/res/android" android:color="#3065E2EF"><item><selector><item android:state_focused="true">' + shape('@color/surface_raised', stroke='@color/accent', width=2) + '</item><item>' + shape('@color/surface_raised', stroke='@color/stroke') + '</item></selector></item></ripple>')
write('drawable/bg_screen.xml', '<shape xmlns:android="http://schemas.android.com/apk/res/android"><gradient android:angle="315" android:startColor="#13243A" android:centerColor="#0B1421" android:endColor="#080E17" /></shape>')

# Ícones da interface: grade de 24 unidades e traço arredondado.
icons = {
    'wifi': 'M3,8 Q12,0 21,8 M6,12 Q12,6 18,12 M9,16 Q12,13 15,16 M12,20 L12,20.1',
    'pc': 'M4,4 L20,4 Q21,4 21,5 L21,15 Q21,16 20,16 L4,16 Q3,16 3,15 L3,5 Q3,4 4,4 M8,21 L16,21 M12,16 L12,21',
    'search': 'M16,10 A6,6 0,1 1,4 10 A6,6 0,1 1,16 10 M15,15 L21,21',
    'moon': 'M20,15 A9,9 0,1 1,9 3 A8,8 0,0 0,20 15',
}
for name, path in icons.items():
    write(f'drawable/ic_{name}.xml', f'''<vector xmlns:android="http://schemas.android.com/apk/res/android" android:width="20dp" android:height="20dp" android:viewportWidth="24" android:viewportHeight="24">
    <path android:pathData="{path}" android:strokeColor="@color/accent" android:strokeWidth="1.8" android:strokeLineCap="round" android:strokeLineJoin="round" android:fillColor="@android:color/transparent" />
    </vector>''')

# Marca JoyHub: controle com o J recortado e três conexões que se encontram no hub.
# Mesma geometria de 108 unidades de Windows/src/icon.cpp, dentro da área segura do ícone adaptativo.
body = 'M38,46 L70,46 Q77,46 79,55 L83,70 Q85,80 77,80 Q73,80 66,71 L42,71 Q35,80 31,80 Q23,80 25,70 L29,55 Q31,46 38,46 Z'
cutouts = 'M48,52 L48,62 Q48,69 41,69 Q34,69 34,62 L38,62 Q38,65 41,65 Q44,65 44,62 L44,52 Z M66,55 A2.5,2.5 0,1 0,71 55 A2.5,2.5 0,1 0,66 55 M72,61 A2.5,2.5 0,1 0,77 61 A2.5,2.5 0,1 0,72 61'
hub = 'M40,31 L54,39 L68,31 M54,26 L54,46'
nodes = 'M37,31 A3,3 0,1 0,43 31 A3,3 0,1 0,37 31 M51,26 A3,3 0,1 0,57 26 A3,3 0,1 0,51 26 M65,31 A3,3 0,1 0,71 31 A3,3 0,1 0,65 31'

def brand_mark(color, connection_color):
    return f'''<path android:fillColor="{color}" android:fillType="evenOdd" android:pathData="{body} {cutouts}" />
    <path android:fillColor="#00000000" android:strokeColor="{connection_color}" android:strokeWidth="2.5" android:strokeLineCap="round" android:strokeLineJoin="round" android:pathData="{hub}" />
    <path android:fillColor="{connection_color}" android:pathData="{nodes}" />'''

mark = brand_mark('#65E2EF', '#F0F6FC')
write('drawable/ic_launcher_foreground.xml', '<vector xmlns:android="http://schemas.android.com/apk/res/android" android:width="108dp" android:height="108dp" android:viewportWidth="108" android:viewportHeight="108">' + mark + '</vector>')
write('drawable/ic_brand.xml', '<vector xmlns:android="http://schemas.android.com/apk/res/android" android:width="48dp" android:height="48dp" android:viewportWidth="72" android:viewportHeight="72"><path android:fillColor="#172638" android:pathData="M18,0 L54,0 Q72,0 72,18 L72,54 Q72,72 54,72 L18,72 Q0,72 0,54 L0,18 Q0,0 18,0" /><group android:translateX="-18" android:translateY="-18">' + mark + '</group></vector>')
write('drawable/ic_launcher_monochrome.xml', '<vector xmlns:android="http://schemas.android.com/apk/res/android" android:width="108dp" android:height="108dp" android:viewportWidth="108" android:viewportHeight="108">' + brand_mark('#FFFFFF', '#FFFFFF') + '</vector>')
write('mipmap-anydpi/ic_launcher.xml', '''<adaptive-icon xmlns:android="http://schemas.android.com/apk/res/android">
    <background android:drawable="@color/icon_bg" />
    <foreground android:drawable="@drawable/ic_launcher_foreground" />
    <monochrome android:drawable="@drawable/ic_launcher_monochrome" />
</adaptive-icon>''')

brand_dir = Path(__file__).resolve().parents[1] / 'assets/brand'
brand_dir.mkdir(parents=True, exist_ok=True)
svg_mark = f'''<rect width="108" height="108" rx="24" fill="#101D30"/>
<path fill="#65E2EF" fill-rule="evenodd" d="{body} {cutouts}"/>
<path fill="none" stroke="#F0F6FC" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round" d="{hub}"/>
<path fill="#F0F6FC" d="{nodes}"/>'''
(brand_dir / 'joyhub-icon.svg').write_text('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 108 108" role="img" aria-label="JoyHub">' + svg_mark + '</svg>\n', encoding='utf-8')
(brand_dir / 'joyhub-logo.svg').write_text('''<svg xmlns="http://www.w3.org/2000/svg" width="520" height="144" viewBox="0 0 520 144" role="img" aria-label="JoyHub — Seu controle. Sem fios.">
<rect width="520" height="144" rx="24" fill="#080E17"/>
<g transform="translate(18 18)">''' + svg_mark + '''</g>
<text x="148" y="78" fill="#F0F6FC" font-family="Segoe UI, sans-serif" font-size="48" font-weight="600">JoyHub</text>
<text x="150" y="106" fill="#A3B4C8" font-family="Segoe UI, sans-serif" font-size="13" letter-spacing="1.2">SEU CONTROLE. SEM FIOS.</text>
</svg>\n''', encoding='utf-8')
print('Recursos de design do Android e SVGs da marca gerados.')
