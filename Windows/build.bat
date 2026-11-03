@echo off
setlocal
cd /d "%~dp0"

cmake -S . -B build -A x64 || goto :erro
cmake --build build --config Release || goto :erro
copy /y build\Release\JoyHub.exe . >nul || goto :erro

echo.
echo Pronto: %~dp0JoyHub.exe
exit /b 0

:erro
echo.
echo Falha no build. Precisa do Visual Studio (C++) e do CMake instalados.
exit /b 1
