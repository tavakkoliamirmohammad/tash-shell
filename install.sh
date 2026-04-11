#!/usr/bin/env bash
set -euo pipefail

REPO="tavakkoliamirmohammad/UNIX-Command-Line-Interface"
INSTALL_DIR="${INSTALL_DIR:-/usr/local/bin}"
BINARY_NAME="tash"

echo "Installing Tash shell..."

# Detect platform
OS="$(uname -s)"
ARCH="$(uname -m)"

case "${OS}" in
    Linux)
        ARTIFACT="tash-linux-amd64"
        ;;
    Darwin)
        if [ "${ARCH}" = "arm64" ]; then
            ARTIFACT="tash-macos-arm64"
        else
            ARTIFACT="tash-macos-arm64"
        fi
        ;;
    *)
        echo "Unsupported OS: ${OS}"
        echo "Please build from source: cmake -B build && cmake --build build"
        exit 1
        ;;
esac

# Get latest release
LATEST=$(curl -sL "https://api.github.com/repos/${REPO}/releases/latest" | grep '"tag_name"' | sed -E 's/.*"([^"]+)".*/\1/')

if [ -z "${LATEST}" ]; then
    echo "No releases found. Building from source..."

    if ! command -v cmake &> /dev/null; then
        echo "Error: cmake not found. Please install cmake."
        exit 1
    fi

    TMPDIR=$(mktemp -d)
    cd "${TMPDIR}"
    curl -sL "https://github.com/${REPO}/archive/refs/heads/master.tar.gz" | tar xz
    cd UNIX-Command-Line-Interface-master
    cmake -B build -DBUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release
    cmake --build build

    echo "Installing to ${INSTALL_DIR}..."
    sudo install -m 755 build/shell.out "${INSTALL_DIR}/${BINARY_NAME}"
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
        cmake -B build -DBUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release
        cmake --build build

        echo "Installing to ${INSTALL_DIR}..."
        sudo install -m 755 build/shell.out "${INSTALL_DIR}/${BINARY_NAME}"
        rm -rf "${TMPDIR}"
    fi
fi

sudo install -m 644 tash.1 /usr/local/share/man/man1/tash.1 2>/dev/null || true

echo "Tash shell installed successfully!"
echo "Run 'tash' to start the shell."
