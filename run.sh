#!/bin/bash
# ============================================================================
# DevicesForge - Correr el capturador de la compilacion de desarrollo
# ============================================================================
#
# No empaqueta. Usa el Qt de esta Mac y el binario de ./build.sh.
# Para instalar la app en Aplicaciones, cuando ya este estable: ./package.sh
#
# Uso:
#   ./build.sh
#   ./run.sh
#

set -euo pipefail

WORKDIR="$(cd "$(dirname "$0")" && pwd)"
APP_BIN="$WORKDIR/build/Capturador.app/Contents/MacOS/Capturador"

if [[ ! -x "$APP_BIN" ]]; then
    echo "No esta build/Capturador.app. Compila primero con ./build.sh"
    exit 1
fi

QT_PREFIX=""
if [[ -d /opt/homebrew/opt/qt/plugins ]]; then
    QT_PREFIX="/opt/homebrew/opt/qt"
elif [[ -d /usr/local/opt/qt/plugins ]]; then
    QT_PREFIX="/usr/local/opt/qt"
elif command -v brew >/dev/null 2>&1; then
    QT_PREFIX="$(brew --prefix qt 2>/dev/null || true)"
fi

if [[ -n "$QT_PREFIX" && -d "$QT_PREFIX/plugins" ]]; then
    export QT_PLUGIN_PATH="$QT_PREFIX/plugins"
fi

echo "=== Capturador ==="
exec "$APP_BIN"
