# JoyHub — tema Windows

Identidade compartilhada com o Android: grafite `#080E17`, superfícies `#101B29`,
ciano `#65E2EF`, texto `#F0F6FC` e texto secundário `#A3B4C8`.
Verde, âmbar e coral distinguem conexão ativa, avisos e driver ausente.

O tema inclui a marca JoyHub (controle com J e três conexões), ícone do executável em oito tamanhos,
quatro estados do ícone de bandeja, fundo original, cartões de sessão, indicadores
de comandos, avisos e botões. O fundo é o mesmo PNG do Android e está embutido
no executável: não é necessário distribuir arquivos de imagem separados.

- `src/theme.h`: paleta.
- `src/ui.cpp`: layout, desenho Direct2D e tipografia DirectWrite.
- `src/icon.cpp`: marca vetorial e ícones da bandeja.
- `res/app.ico`: ícone do executável, gerado pelo alvo `icongen`.
- `res/signal_background.png`: arte compartilhada; origem e prompt em `../Android/DESIGN.md`.
- `res/app.rc`: recursos embutidos no `.exe`.

O layout se adapta ao tamanho da janela. Em grades pequenas, o controle aparece
ao lado das métricas. A imagem é decodificada uma única vez; bitmaps de GPU são
recriados quando o render target muda. O tema não acrescenta trabalho à thread UDP.

## Build

```powershell
.\Windows\build.bat
```

O build regenera automaticamente o ícone após editar sua geometria. Para gerar apenas o ícone:

```powershell
cmake --build Windows/build --config Release --target joyhub_icon
```

Executável: `Windows/JoyHub.exe`.

## Verificação visual

Prévia JoyHub: [janela com dois controles](../assets/screenshots/joyhub-preview.png),
[janela compacta](../assets/screenshots/joyhub-compacto.png) e
[ícones em diferentes tamanhos](../assets/screenshots/joyhub-preview-icones.png).
O layout, a paleta e a arte abstrata de fundo foram preservados na mudança de marca.

O modo de captura usa o mesmo renderer da janela, com dados fictícios. Não abre
o servidor nem cria controles virtuais. Para regerar as capturas acima, a partir da raiz do projeto:

```powershell
.\Windows\JoyHub.exe --snapshot assets/screenshots/joyhub-preview.png 2
.\Windows\JoyHub.exe --snapshot assets/screenshots/joyhub-compacto.png 4 --snapshot-size 760 640 --snapshot-scale 1 --snapshot-state missing
```

`--snapshot-state` aceita `ready`, `missing`, `test` e `guide` (driver pronto com o aviso
do botão Guide preso na Xbox Game Bar); `--snapshot-scale` aceita valores de 1 a 3. Cada
captura também produz uma folha de ícones `*-icones.png`.
Os IPs, telefones e métricas das capturas são exemplos, não leituras de dispositivos reais.

## Cartões dos celulares

- Cartão grande (um ou dois celulares): perda de pacotes, recuperados e perdidos; abaixo, a
  linha do celular (bateria, Wi-Fi, faixa, sinal, velocidade, bateria do controle). Quando
  não cabe, saem primeiro geração e velocidade, depois sinal e bateria do controle. Rodapé com a
  vibração pedida pelo jogo e o botão com contorno **Testar vibração** (vira "Vibrando…").
- Cartão compacto (grade 2x2): bateria e faixa do Wi-Fi na linha do endereço; o botão de teste
  aparece a partir de 160 DIPs de altura.
- Âmbar indica atenção: perda acima de 2%, bateria em 15% ou menos sem carregar, economia de
  energia, Wi-Fi 2,4 GHz ou sinal a −75 dBm ou pior.
- Aviso do Guide: faixa âmbar com **Liberar para a Steam** (botão cheio) e **Agora não** (contorno).

## Validação realizada

- Release x64 compilado com MSVC, sem erros ou avisos de compilação.
- Oito capturas geradas; inspecionados estados com zero, um, dois e quatro controles,
  grades compactas, driver ausente, modo teste e controles pausados/sem sinal.
- Escalas de renderização 100%, 125% e 200%.
- Teste local do executável em `--headless --dry-run --port 38501`: descoberta e
  20/20 respostas de ping; envio de estados, perda simulada, timeout e desconexão.
- Testes feitos sem controle físico ou jogo; não medem latência real do Wi-Fi.

Em 31/10/2026, versão com bateria/Wi-Fi/perda e teste de vibração:

- Release x64 com MSVC `/W4`, sem avisos. Capturas regeneradas, incluindo o aviso do botão Guide.
- `teste_pc.py` contra o servidor real (ViGEmBus 1.21): perda medida pelo PC igual à esperada
  (10 de 110 pacotes na janela = 9,09%), PONG com perdidos/recuperados, UDP → XInput com
  mediana de 53 µs e p99 de 104 µs.
- Teste de vibração (servidor em modo teste e celular simulado): pulsos em 0, 248 e 464 ms,
  retorno ao valor do jogo em 709 ms; os PONGs no meio repetem o valor do teste.
- Botão Guide: com a opção da Xbox Game Bar no padrão, o `XInputGetStateEx` não mostra o bit
  0x0400; com `UseNexusForGameBarEnabled = 0`, mostra, e a Steam registrou "Guide button sent to
  JS" e abriu o Big Picture. O aviso da janela e o clique em "Liberar para a Steam" foram
  conferidos na janela real.
