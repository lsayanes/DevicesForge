#!/bin/bash
# ============================================================================
# DevicesForge - Script de Instalación para macOS
# ============================================================================

set -e

echo "============================================"
echo "  DevicesForge - Instalando deps..."
echo "============================================"

WORKDIR="$(cd "$(dirname "$0")/.." && pwd)"
EXTERNAL_DIR="$WORKDIR/external"

# ============================================================================
# 1. Verificar/instalar Homebrew
# ============================================================================
echo ""
echo "[1/5] Verificando Homebrew..."
if ! command -v brew &> /dev/null; then
    echo "    Instalando Homebrew..."
    /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
    
    # Agregar Homebrew al PATH
    if [[ -f /opt/homebrew/bin/brew ]]; then
        eval "$(/opt/homebrew/bin/brew shellenv)"
    elif [[ -f /usr/local/bin/brew ]]; then
        eval "$(/usr/local/bin/brew shellenv)"
    fi
else
    echo "    Homebrew ya instalado"
fi

# ============================================================================
# 2. Instalar herramientas de build
# ============================================================================
echo ""
echo "[2/5] Instalando herramientas de build..."
brew install cmake git

# ============================================================================
# 3. Verificar Xcode Command Line Tools
# ============================================================================
echo ""
echo "[3/5] Verificando Xcode Command Line Tools..."
if ! xcode-select -p &> /dev/null; then
    echo "    Instalando Xcode Command Line Tools..."
    xcode-select --install
    echo ""
    echo "    Por favor, completa la instalación y vuelve a ejecutar este script."
    exit 1
else
    echo "    Xcode Command Line Tools OK"
fi

# ============================================================================
# 4. Crear directorio external
# ============================================================================
echo ""
echo "[4/5] Preparando directorio external..."
mkdir -p "$EXTERNAL_DIR"
cd "$EXTERNAL_DIR"

# ============================================================================
# 5. Descargar VST3 SDK
# ============================================================================
echo ""
echo "[5/5] Descargando VST3 SDK..."
if [ ! -d "vst3sdk" ]; then
    git clone --depth 1 https://github.com/steinbergmedia/vst3sdk.git
    echo "    VST3 SDK descargado"
else
    echo "    VST3 SDK ya existe, omitiendo..."
fi

# ============================================================================
# Resumen
# ============================================================================
echo ""
echo "============================================"
echo "  Instalación completada!"
echo "============================================"
echo ""
echo "Estructura creada:"
echo "  $EXTERNAL_DIR/vst3sdk/"
echo ""
echo "Próximos pasos:"
echo "  cd $WORKDIR"
echo "  mkdir build && cd build"
echo "  cmake .."
echo "  cmake --build ."
echo ""
echo "Para testing:"
echo "  cd build"
echo "  ctest"
echo ""
