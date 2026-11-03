# JoyHub

![JoyHub — Seu controle. Sem fios.](assets/brand/joyhub-logo.svg)

**Sem Bluetooth no PC? O celular vira a ponte.** O controle (Xbox, DualShock 4, GameSir X5...)
conecta no celular Android por Bluetooth ou USB, e o app repassa tudo ao PC pelo Wi-Fi. No
Windows ele aparece como um **controle Xbox 360** comum (XInput), que a Steam e os jogos reconhecem.

```
controle ──Bluetooth/USB──▶ app Android ──Wi-Fi/UDP──▶ JoyHub.exe ──▶ ViGEmBus ──▶ "Xbox 360 Controller" ──▶ Steam / jogos
```

**Controles:** funciona com qualquer controle que o Android reconheça como gamepad. Testado
com o GameSir X5. Os controles de Xbox (Bluetooth) e o DualShock 4 seguem o layout padrão do
Android e devem funcionar, mas ainda não foram testados. Recursos que o Xbox 360 não tem
(touchpad, giroscópio e light bar do DualShock 4) não chegam ao PC.

## 1. PC (Windows 10/11)

O aplicativo Windows compartilha o tema grafite/ciano do Android, com fundo embutido,
ícones de status e monitor visual de até quatro controles.
[Prévia com dados de exemplo](assets/screenshots/joyhub-preview.png) · [Detalhes do tema](Windows/DESIGN.md).

1. Instale o driver **ViGEmBus**: <https://github.com/nefarius/ViGEmBus/releases>
   (`ViGEmBus_1.22.0_x64_x86_arm64.exe`). É ele que cria o controle Xbox 360 virtual.
   Se o programa já estiver aberto, ele detecta o driver sozinho (sem reiniciar).
2. Baixe o `JoyHub.exe` na página de **Releases** do repositório e abra-o.
   Para compilar: `Windows\build.bat` (precisa do Visual Studio com C++ e do CMake), que gera
   `Windows\JoyHub.exe`.
3. Se o celular não achar o PC, clique em **Liberar no firewall** na janela (o Windows pede
   permissão uma vez). Isso libera a UDP 38500 só para a rede local e remove bloqueios antigos.

O programa abre uma janela e fica com um **ícone perto do relógio**:

| Cor do ícone | Significa |
|---|---|
| cinza | aguardando o celular |
| verde | controle conectado |
| amarelo | sem sinal do celular, app do celular em segundo plano ou modo teste |
| vermelho | falta o driver ViGEmBus |

- A janela mostra os IPs do PC, cada celular conectado (jogador, ping, pacotes/s) e um desenho
  do controle que acende ao vivo: dá para conferir cada botão, gatilho e analógico.
- Cada cartão também mostra a **perda de pacotes** do Wi-Fi (últimos ~5 s), os toques que a
  redundância recuperou, a **bateria do celular** e o **Wi-Fi dele** (geração, faixa de 2,4/5/6 GHz,
  sinal em dBm e velocidade). A linha fica âmbar com bateria fraca, economia de energia,
  Wi-Fi 2,4 GHz ou sinal fraco. Com 15% de bateria (sem carregar), o Windows avisa.
- **Testar vibração**: o botão no cartão manda dois pulsos curtos para o celular/controle e
  confirma qual aparelho é cada jogador. O app mostra um aviso quando o teste chega.
- Se a **Xbox Game Bar** estiver segurando o botão Guide (veja "Problemas comuns"), a janela
  mostra um aviso com o botão **Liberar para a Steam**.
- **Fechar a janela não encerra o programa**: ele continua perto do relógio. Clique no ícone
  para abrir; para sair de vez, botão direito > **Sair**.
- **Iniciar com o Windows** (na janela ou no menu do ícone) abre o programa direto na bandeja
  ao ligar o PC.
- Com a janela escondida, o Windows avisa quando um celular conecta ou desconecta.

Opções de linha de comando (para atalhos e diagnóstico):

| Opção | Efeito |
|---|---|
| `--tray` | abre direto perto do relógio, sem mostrar a janela |
| `--port N` | porta UDP (padrão 38500) |
| `--dry-run` | testa a rede sem o driver (não cria controle virtual) |
| `--headless` | sem janela: mostra os eventos num console |
| `--verbose` | com `--headless`, imprime cada estado recebido (adiciona latência) |

## 2. Android

O app tem identidade visual em grafite e ciano, ícone adaptativo, fundo original,
painel de conexão e monitor gráfico dos botões, analógicos e gatilhos.
Os arquivos de design e o prompt da arte estão em [Android/DESIGN.md](Android/DESIGN.md).

APK pronto para instalar: `JoyHub.apk`, na página de **Releases** do repositório.
O release é assinado com a chave de debug (uso pessoal), não com uma chave de publicação.

- **Android Studio:** abra a pasta `Android` e clique em Run, **ou**
- **Linha de comando:** `cd Android` e depois `gradlew assembleDebug`, e instale
  `app\build\outputs\apk\debug\app-debug.apk` (`adb install -r ...`).

Requer Android 8.0 ou superior.

### Atualização para JoyHub (1.2)

JoyHub é o novo nome do SteamWifiController. O layout, as cores e o funcionamento
continuam os mesmos; a marca e os ícones agora usam um controle com a letra J e
três conexões que representam o hub. Os vetores estão em `assets/brand/`.

No Android, instale `JoyHub.apk` por cima da versão anterior: o identificador de
instalação e a assinatura foram preservados para manter as preferências e o último PC.
No Windows, encerre a versão anterior pela bandeja e abra `JoyHub.exe`.
Uma opção de inicialização automática já ativada é migrada para o novo executável
na primeira abertura. As preferências e regras de firewall antigas continuam reconhecidas.
Referências técnicas a `com.steamwifi.controller`, `swc` e à chave antiga do Registro
são intencionais para compatibilidade; referências à Steam dizem respeito à integração com jogos.

Além da latência e do envio de comandos, o app mostra a **rede Wi-Fi** do celular (faixa,
geração, sinal e velocidade, com aviso em 2,4 GHz ou sinal fraco), a **bateria** (e a do
controle, se ele tiver uma) e a **perda de pacotes medida pelo PC**. Bateria e Wi-Fi também
aparecem na janela do PC. Versões antigas do app e do PC continuam se entendendo: só não
mostram as informações novas.

## 3. Jogando

1. Conecte o controle ao celular (pareado por Bluetooth, no cabo USB ou encaixado, como no
   GameSir X5) e abra o app.
2. Toque no PC que aparece em "Buscar na rede" (ou digite o IP e toque em **Conectar ao PC**).
   Nas próximas vezes o app conecta sozinho ao último PC assim que o encontra (opção
   "Conectar sozinho ao último PC", em "Do seu jeito").
3. O status fica verde: **Conectado · Jogador 1**. Abra a Steam/o jogo.
4. Deixe o app na tela enquanto joga. **Tela escura** apaga a tela sem desconectar.

O monitor do app (e o do PC) mostra cada botão, gatilho e analógico, o que serve para
conferir o mapeamento. Até 4 celulares podem se conectar ao mesmo PC (Jogador 1 a 4).

## Latência: o que o projeto faz

| Etapa | O que foi feito |
|---|---|
| Controle → app | `requestUnbufferedDispatch`: o Android entrega cada movimento do analógico na hora, em vez de agrupar até o próximo quadro da tela (economiza até 16 ms). |
| app → rede | O pacote UDP sai **dentro do próprio evento do controle**, sem fila nem troca de thread, e sem alocação de memória. |
| Wi-Fi do celular | `WIFI_MODE_FULL_LOW_LATENCY` desliga a economia de energia do rádio. Os pacotes saem com DSCP EF, a fila de "voz" (WMM) do Wi-Fi, que tem prioridade. Um heartbeat a cada 10 ms mantém o rádio acordado. |
| Perda de pacote | Cada pacote leva os **4 últimos estados**. Um pacote perdido é coberto pelo próximo, sem esperar retransmissão, e um toque rápido nunca some. |
| PC | Thread de recepção com prioridade máxima, processo fora do "modo de eficiência" do Windows. O estado vai para o driver em microssegundos. |
| Segurança | Se o celular some por 500 ms, o controle vai para neutro (nada fica "travado" apertado) e volta sozinho quando o sinal retorna. |

Medido no PC, o processamento interno do servidor leva microssegundos: do pacote UDP sair até
o estado aparecer no XInput, mediana de 53 µs e p99 de 104 µs (`teste_pc.py --xinput`, 200
amostras, com o ViGEmBus). **Quem domina a latência é o Wi-Fi.** Para o melhor resultado:

- celular no **Wi-Fi 5 GHz** (ou 6 GHz), perto do roteador;
- **PC no cabo de rede** até o roteador;
- evite downloads/streaming pesados na mesma rede enquanto joga.

Numa rede 5 GHz boa, o ping mostrado no app (ida e volta) costuma ficar em poucos
milissegundos. Picos frequentes acima de ~20 ms no "máx" indicam problema de Wi-Fi
(canal congestionado, distância, roteador em economia de energia).

## Teste sem o celular

Com o programa aberto (ou `JoyHub.exe --headless --dry-run` para testar sem o driver):

```
python tools\teste_pc.py            # descoberta, ping, perda de pacotes, redundância, timeout
python tools\teste_pc.py --xinput   # mede UDP → driver → XInput (precisa do ViGEmBus; não rode com jogo aberto)
python tools\teste_pc.py --guide    # confere se o Guide chega ao XInput/Steam (a Steam pode abrir o Big Picture)
```

## Problemas comuns

- **"Sem resposta do PC"**: o servidor está aberto? O firewall está liberado
  (`liberar_firewall.bat`)? PC e celular estão na mesma rede? Redes "de convidados" e
  roteadores com "isolamento de AP" bloqueiam a comunicação entre aparelhos.
- **O servidor diz que o ViGEmBus não foi encontrado**: instale o driver (passo 1) e abra o
  programa de novo.
- **Ping alto ou com picos**: Wi-Fi 2,4 GHz, distância, paredes ou rede congestionada.
  O valor "máx" no app mostra os picos. O cartão do celular no PC e o quadro "Rede Wi-Fi" no
  app mostram a faixa e o sinal; a "perda" acima de ~2% fica âmbar.
- **O botão Guide (Xbox/Home) não abre a Steam**: com a opção *Abrir a Xbox Game Bar usando
  este botão no controle* ligada (o padrão do Windows 10/11), o Windows esconde o botão Xbox de
  todos os outros programas, e a Steam nunca o recebe. Clique em **Liberar para a Steam** no
  aviso da janela (ou no menu do ícone), ou desligue a opção em *Configurações > Jogos > Xbox
  Game Bar*. Vale na hora, sem reiniciar. Se ainda assim nada acontecer, confira no monitor do
  app se o círculo do Guide acende ao apertar o botão Home do controle.
- **Analógico direito ou gatilhos trocados** em algum controle diferente do GameSir: veja no
  monitor do app quais valores mudam e ajuste `GamepadMapper.AxisMap.build`.

## Estrutura

```
Windows/   servidor do PC (C++20, sem dependências além do driver ViGEmBus)
  src/protocol.h   formato dos pacotes (documentado)
  src/vigem.*      cliente mínimo do ViGEmBus (controle Xbox 360)
  src/server.*     UDP, sequência/redundância, timeouts, vibração, detecção do driver
  src/ui.*         tela (Direct2D/DirectWrite); --snapshot gera um PNG de exemplo
  src/window.*     janela, ícone perto do relógio, menu e avisos
  src/system.*     firewall, iniciar com o Windows, IPs locais
  src/icon.*       desenho do ícone (bandeja e .exe); tools/icongen.cpp gera res/app.ico
Android/   app (Java, sem bibliotecas externas)
  GamepadMapper    eventos do Android → estado Xbox 360
  NetClient        envio UDP imediato, heartbeat, ping, vibração
  PhoneStatus      bateria e Wi-Fi do celular (tela e extensão do PING)
  Discovery        acha o PC por broadcast
assets/    marca (SVG) e capturas da janela do PC
tools/
  teste_pc.py        simulador do celular para testar o PC
  design_android.py  gera os recursos de design do Android e os SVGs da marca
```

## Licença

Distribuído sob a licença MIT. Veja [LICENSE](LICENSE).
