# JoyHub — identidade visual do Android

Marca compartilhada com o Windows: controle ciano com um J recortado e três
conexões claras que convergem em um hub. Presente no cabeçalho, no ícone adaptativo
e na versão monocromática. Vetores para uso externo em `../assets/brand/`.
O layout, a paleta e o fundo abstrato sem texto foram preservados.

O app usa um painel nativo em grafite e ciano, com cartões de conexão e sessão,
tipografia do sistema, botões com área mínima de 48 dp e textos em português.
As duas áreas têm rolagem independente para manter as funções acessíveis em
telas pequenas e com fontes maiores. Em janelas estreitas, elas ficam empilhadas.

O desenho do controle é um monitor, não um controle por toque. Botões, direcionais,
gatilhos e analógicos refletem o estado do `GamepadMapper`, na atualização de
interface de 100 ms. O desenho só é invalidado quando os comandos mudam.

Os indicadores da sessão ficam em duas fileiras de dois quadros (`bg_metric`), com o
mesmo rótulo, valor de 23 sp e detalhe de 11 sp: Latência e Envio de comandos (com a
perda medida pelo PC), depois Rede Wi-Fi e Bateria (`PhoneStatus`, relido a cada 2 s).
O valor fica âmbar (`@color/warn`) para Wi-Fi 2,4 GHz, sinal fraco ou bateria em 15%.

## Arquivos

- `app/src/main/res/layout/activity_main.xml`: tela completa.
- `app/src/main/res/values/colors.xml` e `themes.xml`: cores e componentes.
- `app/src/main/res/drawable/ic_*.xml`: ícones vetoriais originais.
- `app/src/main/res/mipmap-anydpi/ic_launcher.xml`: ícone adaptativo com versão monocromática.
- `app/src/main/res/drawable-nodpi/signal_background.png`: arte original de fundo.
- `app/src/main/java/com/steamwifi/controller/GamepadView.java`: ilustração interativa do controle.
- `../tools/design_android.py`: gera novamente os vetores, cores, textos e estilos.
  Se editar esses recursos manualmente, atualize o gerador antes de executá-lo de novo.

## Arte de fundo

`signal_background.png` foi gerada por IA a partir do prompt abaixo. Os ícones e a
ilustração do controle são vetores e código nativos, sem imagens remotas.

Prompt final:

> Use case: stylized-concept. Asset type: wide background artwork for a native Android gaming controller dashboard. Create a premium abstract macro photograph / 3D render of flowing wireless signal waves: thin luminous cyan and ice-blue concentric arcs sweeping diagonally across the right half of a near-black midnight navy background, subtle atmospheric blue haze, a few very fine technical grid lines, elegant restrained industrial gaming aesthetic. Wide landscape 3:2 composition. Left half mostly dark negative space to overlay white interface headings. Low contrast overall with small crisp cyan highlights confined to the right. No text, no letters, no numbers, no logos, no watermark, no interface, no devices. High quality smooth gradients.

## Build

Na pasta `Android`, execute:

```powershell
.\gradlew.bat assembleDebug assembleRelease lintDebug --console=plain
```

O release mantém a configuração existente de assinatura com a chave de debug
para instalação pessoal. Não é uma assinatura de publicação na Play Store.

- Release: `app/build/outputs/apk/release/app-release.apk`
- Debug: `app/build/outputs/apk/debug/app-debug.apk`
- Lint: `app/build/reports/lint-results-debug.html`

Instalação com um aparelho conectado:

```powershell
adb install -r app/build/outputs/apk/release/app-release.apk
```
