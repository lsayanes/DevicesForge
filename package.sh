#!/bin/bash
# Empaqueta Capturador.app para esta Mac e instala en /Applications.
#   ./package.sh           instala
#   ./package.sh --local   solo deja dist/Capturador-<version>.app

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

INSTALL=1
if [ "${1:-}" = "--local" ] || [ "${1:-}" = "--no-install" ]; then
    INSTALL=0
fi

BUILD_DIR="build-release"
APP_NAME="Capturador.app"
APP_PATH="$BUILD_DIR/$APP_NAME"
VERSION=$(grep -E '^project\(DevicesForge VERSION ' CMakeLists.txt | sed -E 's/.*VERSION ([0-9.]+).*/\1/')

echo "=== Empaquetado Capturador $VERSION ==="

QT_PREFIX=""
if [ -d /opt/homebrew/opt/qt/lib/cmake/Qt6 ]; then
    QT_PREFIX="/opt/homebrew/opt/qt"
elif [ -d /usr/local/opt/qt/lib/cmake/Qt6 ]; then
    QT_PREFIX="/usr/local/opt/qt"
elif command -v brew >/dev/null 2>&1; then
    QT_PREFIX=$(brew --prefix qt 2>/dev/null || true)
fi

MACDEPLOYQT=""
if [ -n "$QT_PREFIX" ] && [ -x "$QT_PREFIX/bin/macdeployqt" ]; then
    MACDEPLOYQT="$QT_PREFIX/bin/macdeployqt"
elif command -v macdeployqt >/dev/null 2>&1; then
    MACDEPLOYQT="$(command -v macdeployqt)"
fi

if [ -z "$MACDEPLOYQT" ]; then
    echo "Error: no se encontro macdeployqt. Instala Qt 6 con Homebrew: brew install qt"
    exit 1
fi

mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

CMAKE_ARGS=(-DCMAKE_BUILD_TYPE=Release -DBUILD_PLUGIN=OFF -DBUILD_TESTS=OFF -DBUILD_TEST_HOST=OFF)
if [ -n "$QT_PREFIX" ] && [ -d "$QT_PREFIX/lib/cmake/Qt6" ]; then
    echo "Qt6 encontrado en: $QT_PREFIX"
    CMAKE_ARGS+=("-DCMAKE_PREFIX_PATH=$QT_PREFIX")
fi
if [ -d "$SCRIPT_DIR/build/_deps/pffft-src" ]; then
    CMAKE_ARGS+=("-DFETCHCONTENT_SOURCE_DIR_PFFFT=$SCRIPT_DIR/build/_deps/pffft-src")
fi

echo "Configurando Release..."
cmake .. "${CMAKE_ARGS[@]}"

echo "Compilando..."
cmake --build . --target Capturador --parallel "$(sysctl -n hw.ncpu)"

if [ ! -d "$APP_NAME" ]; then
    echo "Error: no se genero $APP_NAME"
    exit 1
fi

echo "Empaquetando dependencias Qt (macdeployqt)..."
DEPLOY_ARGS=(-always-overwrite -codesign=-)
if [ -n "$QT_PREFIX" ]; then
    DEPLOY_ARGS+=(-libpath="$QT_PREFIX/lib")
fi
"$MACDEPLOYQT" "$APP_NAME" "${DEPLOY_ARGS[@]}"

# El rpath de desarrollo hace que la app instalada cargue el Qt de Homebrew ademas del empaquetado.
BIN="$APP_NAME/Contents/MacOS/Capturador"
if [ -n "$QT_PREFIX" ]; then
    install_name_tool -delete_rpath "$QT_PREFIX/lib" "$BIN" 2>/dev/null || true
fi

echo "Re-firmando bundle (ad-hoc)..."
codesign --force --deep --sign - "$APP_NAME"

cd "$SCRIPT_DIR"

ARCHIVE_DIR="dist"
ARCHIVE_PATH="$ARCHIVE_DIR/Capturador-$VERSION.app"
mkdir -p "$ARCHIVE_DIR"
rm -rf "$ARCHIVE_PATH"
ditto "$APP_PATH" "$ARCHIVE_PATH"

echo ""
echo "=== Empaquetado listo ==="
echo "  Carpeta local: $ARCHIVE_PATH"
echo "  Version:       $VERSION"

if [ "$INSTALL" -eq 1 ]; then
    TARGET="/Applications/Capturador.app"
    echo ""
    echo "Instalando en $TARGET ..."
    rm -rf "$TARGET"
    ditto "$ARCHIVE_PATH" "$TARGET"
    echo ""
    echo "Capturador $VERSION instalado en Aplicaciones."
    echo "Abri Launchpad o Spotlight (Cmd+Espacio) y busca \"Capturador\"."
else
    echo ""
    echo "Para instalar en Aplicaciones:"
    echo "  ./package.sh"
    echo ""
    echo "O arrastra manualmente dist/Capturador-$VERSION.app a /Applications"
fi
