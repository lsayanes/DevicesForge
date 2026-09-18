@echo off
REM ============================================================================
REM DevicesForge - Script de Instalacion para Windows
REM ============================================================================

echo ============================================
echo   DevicesForge - Instalando deps...
echo ============================================

set WORKDIR=%~dp0..
set EXTERNAL_DIR=%WORKDIR%\external

REM ============================================================================
REM 1. Verificar/instalar Chocolatey
REM ============================================================================
echo.
echo [1/6] Verificando Chocolatey...
where choco >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo    Instalando Chocolatey...
    powershell -Command "Set-ExecutionPolicy Bypass -Scope Process -Force; [System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072; iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))"
    call refreshenv
) else (
    echo    Chocolatey ya instalado
)

REM ============================================================================
REM 2. Instalar herramientas de build
REM ============================================================================
echo.
echo [2/6] Instalando herramientas de build...
choco install cmake git -y
call refreshenv

REM ============================================================================
REM 3. Verificar Visual Studio
REM ============================================================================
echo.
echo [3/6] Verificando Visual Studio...
where msbuild >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo.
    echo    ADVERTENCIA: Visual Studio con workloads de C++ no encontrado.
    echo.
    echo    Por favor, instala Visual Studio 2019/2022 con:
    echo      - "Desktop development with C++"
    echo      - Windows 10 SDK
    echo.
    echo    Descarga: https://visualstudio.microsoft.com/downloads/
    echo.
    echo    Despues de instalar, ejecuta este script de nuevo.
    pause
    exit /b 1
) else (
    echo    Visual Studio OK
)

REM ============================================================================
REM 4. Crear directorio external
REM ============================================================================
echo.
echo [4/6] Preparando directorio external...
if not exist "%EXTERNAL_DIR%" mkdir "%EXTERNAL_DIR%"
cd /d "%EXTERNAL_DIR%"

REM ============================================================================
REM 5. Descargar VST3 SDK
REM ============================================================================
echo.
echo [5/6] Descargando VST3 SDK...
if not exist "vst3sdk" (
    git clone --depth 1 https://github.com/steinbergmedia/vst3sdk.git
    echo    VST3 SDK descargado
) else (
    echo    VST3 SDK ya existe, omitiendo...
)

REM ============================================================================
REM 6. Descargar ONNX Runtime
REM ============================================================================
echo.
echo [6/6] Descargando ONNX Runtime...
set ORT_VERSION=1.24.4

if not exist "onnxruntime" (
    echo    Descargando onnxruntime-win-x64-%ORT_VERSION%.zip...
    powershell -Command "Invoke-WebRequest -Uri 'https://github.com/microsoft/onnxruntime/releases/download/v%ORT_VERSION%/onnxruntime-win-x64-%ORT_VERSION%.zip' -OutFile 'onnxruntime.zip'"
    
    echo    Extrayendo...
    powershell -Command "Expand-Archive -Path 'onnxruntime.zip' -DestinationPath '.'"
    
    REM Renombrar directorio
    if exist "onnxruntime-win-x64-%ORT_VERSION%" (
        ren "onnxruntime-win-x64-%ORT_VERSION%" "onnxruntime"
    )
    
    del onnxruntime.zip
    echo    ONNX Runtime descargado
) else (
    echo    ONNX Runtime ya existe, omitiendo...
)

REM ============================================================================
REM Resumen
REM ============================================================================
echo.
echo ============================================
echo   Instalacion completada!
echo ============================================
echo.
echo Estructura creada:
echo   %EXTERNAL_DIR%\vst3sdk\
echo   %EXTERNAL_DIR%\onnxruntime\
echo.
echo Proximos pasos:
echo   cd %WORKDIR%
echo   mkdir build ^&^& cd build
echo   cmake .. -G "Visual Studio 17 2022" -A x64
echo   cmake --build . --config Release
echo.
echo Para testing:
echo   cd build
echo   ctest -C Release
echo.
pause
