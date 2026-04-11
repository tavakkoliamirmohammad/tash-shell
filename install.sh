#!/usr/bin/env bash
set -euo pipefail

REPO="tavakkoliamirmohammad/UNIX-Command-Line-Interface"
INSTALL_DIR="${INSTALL_DIR:-/usr/local/bin}"
BINARY_NAME="amish"

echo "Installing Amish shell..."

# Detect platform
OS="$(uname -s)"
ARCH="$(uname -m)"

case "${OS}" in
    Linux)
        ARTIFACT="amish-linux-amd64"
        ;;
    Darwin)
        if [ "${ARCH}" = "arm64" ]; then
            ARTIFACT="amish-macos-arm64"
        else
            ARTIFACT="amish-macos-intel"
        fi
        ;;
    *)
        echo "Unsupported OS: ${OS}"
        echo "Please build from source: g++ main.cpp colors.cpp -lreadline -o amish -std=c++11"
        exit 1
        ;;
esac

# Get latest release
LATEST=$(curl -sL "https://api.github.com/repos/${REPO}/releases/latest" | grep '"tag_name"' | sed -E 's/.*"([^"]+)".*/\1/')

if [ -z "${LATEST}" ]; then
    echo "No releases found. Building from source..."

    # Check for dependencies
    if ! command -v g++ &> /dev/null; then
        echo "Error: g++ not found. Please install a C++ compiler."
        exit 1
    fi

    TMPDIR=$(mktemp -d)
    cd "${TMPDIR}"
    curl -sL "https://github.com/${REPO}/archive/refs/heads/master.tar.gz" | tar xz
    cd UNIX-Command-Line-Interface-master
    g++ main.cpp colors.cpp -lreadline -o "${BINARY_NAME}" -std=c++11 -O2

    echo "Installing to ${INSTALL_DIR}..."
    sudo install -m 755 "${BINARY_NAME}" "${INSTALL_DIR}/${BINARY_NAME}"
    rm -rf "${TMPDIR}"
else
    echo "Downloading ${LATEST}..."
    DOWNLOAD_URL="https://github.com/${REPO}/releases/download/${LATEST}/${ARTIFACT}"

    TMPFILE=$(mktemp)
    if curl -sL -o "${TMPFILE}" -w "%{http_code}" "${DOWNLOAD_URL}" | grep -q "^2"; then
        chmod +x "${TMPFILE}"
        echo "Installing to ${INSTALL_DIR}..."
        sudo install -m 755 "${TMPFILE}" "${INSTALL_DIR}/${BINARY_NAME}"
        rm -f "${TMPFILE}"
    else
        echo "Pre-built binary not available for your platform. Building from source..."
        rm -f "${TMPFILE}"

        TMPDIR=$(mktemp -d)
        cd "${TMPDIR}"
        curl -sL "https://github.com/${REPO}/archive/refs/tags/${LATEST}.tar.gz" | tar xz
        cd UNIX-Command-Line-Interface-*
        g++ main.cpp colors.cpp -lreadline -o "${BINARY_NAME}" -std=c++11 -O2

        echo "Installing to ${INSTALL_DIR}..."
        sudo install -m 755 "${BINARY_NAME}" "${INSTALL_DIR}/${BINARY_NAME}"
        rm -rf "${TMPDIR}"
    fi
fi

echo "Amish shell installed successfully!"
echo "Run 'amish' to start the shell."
