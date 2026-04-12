#!/usr/bin/env bash
set -euo pipefail

REPO="tavakkoliamirmohammad/tash-shell"
INSTALL_DIR="${INSTALL_DIR:-${HOME}/.local/bin}"
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
            ARTIFACT="tash-macos-amd64"
        fi
        ;;
    *)
        echo "Unsupported OS: ${OS}"
        echo "Please build from source: cmake -B build && cmake --build build"
        exit 1
        ;;
esac

mkdir -p "${INSTALL_DIR}"

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
    cd tash-shell-master
    cmake -B build -DBUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release
    cmake --build build

    echo "Installing to ${INSTALL_DIR}..."
    install -m 755 build/tash.out "${INSTALL_DIR}/${BINARY_NAME}"
    rm -rf "${TMPDIR}"
else
    echo "Downloading ${LATEST}..."
    DOWNLOAD_URL="https://github.com/${REPO}/releases/download/${LATEST}/${ARTIFACT}"

    TMPFILE=$(mktemp)
    if curl -sL -o "${TMPFILE}" -w "%{http_code}" "${DOWNLOAD_URL}" | grep -q "^2"; then
        chmod +x "${TMPFILE}"
        echo "Installing to ${INSTALL_DIR}..."
        install -m 755 "${TMPFILE}" "${INSTALL_DIR}/${BINARY_NAME}"
        rm -f "${TMPFILE}"
    else
        echo "Pre-built binary not available for your platform. Building from source..."
        rm -f "${TMPFILE}"

        TMPDIR=$(mktemp -d)
        cd "${TMPDIR}"
        curl -sL "https://github.com/${REPO}/archive/refs/tags/${LATEST}.tar.gz" | tar xz
        cd tash-shell-*
        cmake -B build -DBUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release
        cmake --build build

        echo "Installing to ${INSTALL_DIR}..."
        install -m 755 build/tash.out "${INSTALL_DIR}/${BINARY_NAME}"
        rm -rf "${TMPDIR}"
    fi
fi

# Install Nerd Font for prompt glyphs (Powerline icons)
install_nerd_font() {
    local FONT_NAME="MesloLGS Nerd Font"
    local FONT_VERSION="v3.3.0"
    local FONT_ZIP="Meslo.zip"
    local FONT_URL="https://github.com/ryanoasis/nerd-fonts/releases/download/${FONT_VERSION}/${FONT_ZIP}"

    case "${OS}" in
        Darwin)
            FONT_DIR="${HOME}/Library/Fonts"
            ;;
        Linux)
            FONT_DIR="${HOME}/.local/share/fonts"
            ;;
    esac

    # Skip if already installed
    if ls "${FONT_DIR}"/MesloLGS*NerdFont* &>/dev/null; then
        echo "Nerd Font already installed."
        return 0
    fi

    echo "Installing ${FONT_NAME} for prompt icons..."
    mkdir -p "${FONT_DIR}"

    FONT_TMP=$(mktemp -d)
    if curl -sL -o "${FONT_TMP}/${FONT_ZIP}" "${FONT_URL}"; then
        unzip -qo "${FONT_TMP}/${FONT_ZIP}" -d "${FONT_TMP}/fonts"
        # Install only the MesloLGS variants
        find "${FONT_TMP}/fonts" -name "MesloLGS*NerdFont*.ttf" -exec cp {} "${FONT_DIR}/" \;

        if [ "${OS}" = "Linux" ]; then
            fc-cache -f "${FONT_DIR}" 2>/dev/null || true
        fi

        echo "Installed ${FONT_NAME} to ${FONT_DIR}"
        echo "NOTE: Set your terminal font to \"MesloLGS Nerd Font\" for best results."
    else
        echo "Warning: Could not download Nerd Font. Prompt icons may not display correctly."
        echo "Install manually: https://www.nerdfonts.com/"
    fi

    rm -rf "${FONT_TMP}"
}

install_nerd_font

# Add install dir to PATH in shell profile if not already there
case ":${PATH}:" in
    *":${INSTALL_DIR}:"*) ;;
    *)
        SHELL_NAME="$(basename "${SHELL:-/bin/bash}")"
        case "${SHELL_NAME}" in
            zsh)  PROFILE="${HOME}/.zshrc" ;;
            bash) PROFILE="${HOME}/.bashrc" ;;
            *)    PROFILE="${HOME}/.profile" ;;
        esac

        PATH_LINE="export PATH=\"${INSTALL_DIR}:\$PATH\""
        if ! grep -qF "${INSTALL_DIR}" "${PROFILE}" 2>/dev/null; then
            echo "" >> "${PROFILE}"
            echo "# Added by Tash shell installer" >> "${PROFILE}"
            echo "${PATH_LINE}" >> "${PROFILE}"
            echo "Added ${INSTALL_DIR} to PATH in ${PROFILE}"
        fi

        export PATH="${INSTALL_DIR}:${PATH}"
        ;;
esac

echo ""
echo "Tash shell installed successfully!"
echo "Run 'tash' to start the shell, or restart your shell to pick up PATH changes."
echo ""
echo "IMPORTANT: Set your terminal font to \"MesloLGS Nerd Font\" for prompt icons to display correctly."
echo "  - iTerm2:      Preferences > Profiles > Text > Font"
echo "  - Terminal.app: Preferences > Profiles > Font > Change"
echo "  - Alacritty:    font.normal.family = \"MesloLGS Nerd Font\" in alacritty.toml"
echo "  - Kitty:        font_family MesloLGS Nerd Font in kitty.conf"
