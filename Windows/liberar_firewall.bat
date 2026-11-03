@echo off
rem Libera a porta UDP 38500 no Firewall do Windows, apenas para aparelhos da rede local.
net session >nul 2>&1
if errorlevel 1 (
    echo Execute como administrador: clique com o botao direito ^> "Executar como administrador".
    pause
    exit /b 1
)

netsh advfirewall firewall delete rule name="SteamWifiController" >nul 2>&1
netsh advfirewall firewall delete rule name="JoyHub" >nul 2>&1
netsh advfirewall firewall add rule name="JoyHub" dir=in action=allow protocol=UDP localport=38500 remoteip=localsubnet profile=any
if errorlevel 1 (
    echo Falha ao criar a regra.
) else (
    echo Regra criada: UDP 38500 liberada para a rede local.
)
pause
