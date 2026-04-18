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
        case "${ARCH}" in
            x86_64|amd64)   ARTIFACT="tash-linux-amd64" ;;
            aarch64|arm64)  ARTIFACT="tash-linux-arm64" ;;
            *)
                echo "Unsupported Linux arch: ${ARCH}"
                echo "Will try to build from source."
                ARTIFACT=""
                ;;
        esac
        ;;
    Darwin)
        if [ "${ARCH}" = "arm64" ]; then
            ARTIFACT="tash-macos-arm64"
        elif [ "${ARCH}" = "x86_64" ]; then
            echo "Note: Intel Macs are no longer a first-class target."
            echo "Installing the arm64 build — it will run under Rosetta 2."
            echo "If Rosetta 2 is not installed, run: softwareupdate --install-rosetta"
            ARTIFACT="tash-macos-arm64"
        else
            echo "Unsupported macOS arch: ${ARCH}"
            echo "Please build from source: cmake -B build && cmake --build build"
            exit 1
        fi
        ;;
    *)
        echo "Unsupported OS: ${OS}"
        echo "Please build from source: cmake -B build && cmake --build build"
        exit 1
        ;;
esac

# Pick sudo invocation for privileged installs (root in containers has
# no sudo binary; a regular user on Alpine may only have `doas`).
SUDO=""
if [ "$(id -u)" -ne 0 ]; then
    if command -v sudo >/dev/null 2>&1; then
        SUDO="sudo"
    elif command -v doas >/dev/null 2>&1; then
        SUDO="doas"
    fi
fi

# Install build-time + runtime deps for the source-build fallback.
# Best-effort: if the package manager isn't recognized we fall through
# and let cmake surface the missing header. The user's distro may name
# things differently; that failure is strictly better than a silent
# degraded build (no AI, no SQLite history) that the user never notices.
install_build_deps() {
    echo "Installing build dependencies (cmake + libcurl + SQLite + nlohmann-json)..."
    if command -v apt-get >/dev/null 2>&1; then
        ${SUDO} apt-get update -y
        ${SUDO} apt-get install -y cmake g++ make \
            libcurl4-openssl-dev libsqlite3-dev nlohmann-json3-dev
    elif command -v dnf >/dev/null 2>&1; then
        ${SUDO} dnf install -y cmake gcc-c++ make \
            libcurl-devel sqlite-devel json-devel
    elif command -v yum >/dev/null 2>&1; then
        ${SUDO} yum install -y cmake gcc-c++ make \
            libcurl-devel sqlite-devel json-devel
    elif command -v apk >/dev/null 2>&1; then
        ${SUDO} apk add --no-cache g++ cmake make linux-headers git \
            curl-dev sqlite-dev nlohmann-json
    elif command -v pacman >/dev/null 2>&1; then
        ${SUDO} pacman -Sy --noconfirm cmake gcc make curl sqlite nlohmann-json
    elif command -v brew >/dev/null 2>&1; then
        brew install cmake curl sqlite nlohmann-json
    else
        echo "Warning: unknown package manager; install cmake + libcurl + SQLite + nlohmann-json manually."
        echo "(nlohmann-json is optional — if absent, the build will fetch it.)"
        echo "See README.md 'Prerequisites' for distro-specific commands."
    fi
}

mkdir -p "${INSTALL_DIR}"

# Channel selection:
#   default        → newest tagged release (stable)
#   TASH_USE_MASTER=1 → rolling master-latest pre-release (bleeding edge)
USE_MASTER="${TASH_USE_MASTER:-0}"

if [ "${USE_MASTER}" = "1" ]; then
    # master-latest is a pre-release, so `releases/latest` skips it.
    # Pin to the known tag name published by .github/workflows/publish-master.yml.
    LATEST="master-latest"
    echo "Using rolling master build (TASH_USE_MASTER=1)"
else
    # Newest tagged release; GitHub's endpoint already skips pre-releases.
    LATEST=$(curl -sL "https://api.github.com/repos/${REPO}/releases/latest" | grep '"tag_name"' | sed -E 's/.*"([^"]+)".*/\1/')
fi

# Build from source as a fallback. Installs deps first so users don't
# silently get a binary without AI / SQLite history (CMake auto-disables
# those features when their headers are missing). `channel` picks the
# tarball — "master" for rolling builds, a tag for tagged releases.
build_from_source() {
    local channel="$1"
    install_build_deps

    if ! command -v cmake >/dev/null 2>&1; then
        echo "Error: cmake still not available after install attempt."
        echo "Install cmake manually and re-run this script."
        exit 1
    fi

    TMPDIR=$(mktemp -d)
    cd "${TMPDIR}"
    if [ "${channel}" = "master" ]; then
        curl -sL "https://github.com/${REPO}/archive/refs/heads/master.tar.gz" | tar xz
        cd tash-shell-master
    else
        curl -sL "https://github.com/${REPO}/archive/refs/tags/${channel}.tar.gz" | tar xz
        cd tash-shell-*
    fi
    cmake -B build -DBUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j "$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)"

    echo "Installing to ${INSTALL_DIR}..."
    install -m 755 build/tash.out "${INSTALL_DIR}/${BINARY_NAME}"
    cd - >/dev/null
    rm -rf "${TMPDIR}"
}

if [ -z "${LATEST}" ]; then
    echo "No releases found. Building from source..."
    build_from_source master
elif [ -z "${ARTIFACT}" ]; then
    echo "No prebuilt binary available for ${OS} ${ARCH}. Building from source..."
    if [ "${USE_MASTER}" = "1" ]; then
        build_from_source master
    else
        build_from_source "${LATEST}"
    fi
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
        if [ "${USE_MASTER}" = "1" ]; then
            build_from_source master
        else
            build_from_source "${LATEST}"
        fi
    fi
fi

# Install man page. The repo ships tash.1.in (a template with
# @PROJECT_VERSION@); CMake renders it at build time, so raw master has
# no tash.1 — fetch the template and substitute the version ourselves.
# -fsSL so 4xx actually errors (default -sL writes the 404 body silently).
MAN_DIR="${HOME}/.local/share/man/man1"
mkdir -p "${MAN_DIR}"
if [ "${USE_MASTER}" = "1" ] || [ -z "${LATEST}" ]; then
    MAN_REF="master"
    MAN_VERSION="master-latest"
else
    MAN_REF="${LATEST}"
    MAN_VERSION="${LATEST#v}"
fi
MANPAGE_URL="https://raw.githubusercontent.com/${REPO}/${MAN_REF}/tash.1.in"
MAN_TMP=$(mktemp)
if curl -fsSL -o "${MAN_TMP}" "${MANPAGE_URL}"; then
    sed "s|@PROJECT_VERSION@|${MAN_VERSION}|g" "${MAN_TMP}" > "${MAN_DIR}/tash.1"
    rm -f "${MAN_TMP}"
    echo "Installed man page (man tash)."
else
    rm -f "${MAN_TMP}"
    echo "Warning: Could not download man page from ${MANPAGE_URL}."
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

        MANPATH_LINE="export MANPATH=\"${HOME}/.local/share/man:\$MANPATH\""
        if ! grep -qF ".local/share/man" "${PROFILE}" 2>/dev/null; then
            echo "${MANPATH_LINE}" >> "${PROFILE}"
        fi

        export PATH="${INSTALL_DIR}:${PATH}"
        ;;
esac

echo ""
echo "Tash shell installed successfully!"
echo "Run 'tash' to start the shell, or restart your shell to pick up PATH changes."

# Show which optional features are compiled into the installed binary.
# SQLite history auto-disables when its headers are missing, so this
# line is the user's only hint they didn't get a full-featured build.
# (libcurl is now a hard requirement — builds without it fail at
# configure time, so AI is always compiled in.)
if INSTALLED_INFO=$("${INSTALL_DIR}/${BINARY_NAME}" --version 2>/dev/null); then
    echo ""
    echo "${INSTALLED_INFO}"
    if echo "${INSTALLED_INFO}" | grep -q '\-sqlite-history'; then
        echo ""
        echo "NOTE: SQLite history is not compiled in — falling back to plain ~/.tash_history."
        echo "Install libsqlite3-dev and reinstall to enable history --here / --failed / stats."
    fi
fi

echo ""
echo "IMPORTANT: Set your terminal font to \"MesloLGS Nerd Font\" for prompt icons to display correctly."
echo "  - iTerm2:      Preferences > Profiles > Text > Font"
echo "  - Terminal.app: Preferences > Profiles > Font > Change"
echo "  - Alacritty:    font.normal.family = \"MesloLGS Nerd Font\" in alacritty.toml"
echo "  - Kitty:        font_family MesloLGS Nerd Font in kitty.conf"
