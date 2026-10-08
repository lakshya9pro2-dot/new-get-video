#!/usr/bin/env bash
set -euo pipefail

echo "============================================================"
echo " WPE HLS URL Extractor - Dependency & Installation Setup"
echo "============================================================"

# Detect system details
OS_ID=$(grep -E '^ID=' /etc/os-release | cut -d= -f2 | tr -d '"')
VERSION_ID=$(grep -E '^VERSION_ID=' /etc/os-release | cut -d= -f2 | tr -d '"')
ARCH=$(uname -m)

echo "[INFO] Detected OS: ${OS_ID} ${VERSION_ID} (${ARCH})"

# Check if required tools are available
check_cmd() {
    command -v "$1" >/dev/null 2>&1
}

# Install base dev dependencies
echo "[INFO] Checking build dependencies..."
MISSING_PKGS=()

for pkg in cmake g++ gcc pkg-config libsoup-3.0-dev libglib2.0-dev libxkbcommon-dev libegl-dev libepoxy-dev libjpeg62-turbo; do
    if ! dpkg -s "$pkg" >/dev/null 2>&1; then
        MISSING_PKGS+=("$pkg")
    fi
done

if [ ${#MISSING_PKGS[@]} -gt 0 ]; then
    echo "[INFO] Installing required build packages: ${MISSING_PKGS[*]}"
    sudo apt-get update -y
    sudo apt-get install -y "${MISSING_PKGS[@]}" || true
fi

# Check for WPE WebKit 2.0+ and WPE Platform
echo "[INFO] Verifying WPE WebKit installation..."
if ! pkg-config --exists wpe-webkit-2.0 wpe-platform-headless-2.0; then
    echo "[WARN] wpe-webkit-2.0 or wpe-platform-headless-2.0 pkg-config not found in default path."
    echo "[INFO] Checking local /usr/local installation..."
    if [ -f "/usr/local/lib/x86_64-linux-gnu/pkgconfig/wpe-webkit-2.0.pc" ]; then
        export PKG_CONFIG_PATH="/usr/local/lib/x86_64-linux-gnu/pkgconfig:${PKG_CONFIG_PATH:-}"
    fi
fi

if pkg-config --exists wpe-webkit-2.0; then
    WPE_VER=$(pkg-config --modversion wpe-webkit-2.0)
    echo "[OK] Found WPE WebKit version: ${WPE_VER}"
else
    echo "[ERROR] WPE WebKit could not be found. Please ensure libwpewebkit-2.0 is installed."
    exit 1
fi

if pkg-config --exists wpe-platform-headless-2.0; then
    PLAT_VER=$(pkg-config --modversion wpe-platform-headless-2.0)
    echo "[OK] Found WPE Headless Platform version: ${PLAT_VER}"
fi

echo "[INFO] Dependencies verified successfully!"
echo "[INFO] To build the project, run:"
echo "       cmake -S . -B build -DCMAKE_BUILD_TYPE=Release"
echo "       cmake --build build -j1"
