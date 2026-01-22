#!/bin/bash
set -e

REPO="K16858/minisync"
INSTALL_DIR="$HOME/.local/bin"

echo "Installing minisync..."
echo ""

OS=$(uname -s | tr '[:upper:]' '[:lower:]')
ARCH=$(uname -m)

if [ "$ARCH" = "x86_64" ]; then
    ARCH="amd64"
elif [ "$ARCH" = "aarch64" ]; then
    ARCH="arm64"
fi

echo "Detected: $OS-$ARCH"

LATEST_RELEASE=$(curl -s "https://api.github.com/repos/$REPO/releases" | grep "tag_name" | head -n 1 | cut -d '"' -f 4)

if [ -z "$LATEST_RELEASE" ]; then
    echo "Failed to fetch latest release"
    exit 1
fi

echo "Latest version: $LATEST_RELEASE"
echo ""

BASE_URL="https://github.com/$REPO/releases/download/$LATEST_RELEASE"
BINARY_PREFIX="minisync-$LATEST_RELEASE-$OS-$ARCH"

mkdir -p "$INSTALL_DIR"

echo "Downloading binaries..."
curl -L "$BASE_URL/msync-$OS-$ARCH" -o "$INSTALL_DIR/msync"
curl -L "$BASE_URL/msync-server-$OS-$ARCH" -o "$INSTALL_DIR/msync-server"

chmod +x "$INSTALL_DIR/msync"
chmod +x "$INSTALL_DIR/msync-server"

echo "✓ Installed to $INSTALL_DIR"
echo ""

if echo "$PATH" | grep -q "$HOME/.local/bin"; then
    echo "✓ PATH is already configured"
else
    echo "⚠ WARNING: ~/.local/bin is NOT in your PATH"
    echo ""
    echo "Add this line to your shell configuration:"
    if [ -f "$HOME/.bashrc" ]; then
        echo "  echo 'export PATH=\"\$HOME/.local/bin:\$PATH\"' >> ~/.bashrc"
        echo "  source ~/.bashrc"
    elif [ -f "$HOME/.zshrc" ]; then
        echo "  echo 'export PATH=\"\$HOME/.local/bin:\$PATH\"' >> ~/.zshrc"
        echo "  source ~/.zshrc"
    else
        echo "  export PATH=\"\$HOME/.local/bin:\$PATH\""
    fi
fi

echo ""
echo "Installation complete!"
echo ""
echo "Quick start:"
echo "  1. cd /path/to/your/project"
echo "  2. msync init my-project"
echo "  3. msync-server (on one machine)"
echo "  4. msync discover (on another machine)"
