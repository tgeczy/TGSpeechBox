#!/bin/bash
# TGSpeechBox Linux Installer
# Installs to /usr/local by default, or a custom prefix

set -e

PREFIX="${1:-/usr/local}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "Installing TGSpeechBox to $PREFIX..."

# Create directories
mkdir -p "$PREFIX/bin"
mkdir -p "$PREFIX/lib"
mkdir -p "$PREFIX/share/tgspeechbox"

# Copy files
cp "$SCRIPT_DIR/bin/tgsbRender" "$PREFIX/bin/tgsbRender"
cp "$SCRIPT_DIR/lib/"*.so "$PREFIX/lib/"
cp -r "$SCRIPT_DIR/share/tgspeechbox/"* "$PREFIX/share/tgspeechbox/"

# Backward-compat symlinks (keeps existing Speech Dispatcher configs working)
ln -sf tgsbRender "$PREFIX/bin/nvspRender"
ln -sf libtgspeechbox.so "$PREFIX/lib/libspeechPlayer.so"
ln -sf libtgsbFrontend.so "$PREFIX/lib/libnvspFrontend.so"
ln -sfn tgspeechbox "$PREFIX/share/nvspeechplayer"

# Install wrapper script
cp "$SCRIPT_DIR/bin/tgsp" "$PREFIX/bin/tgsp"
chmod +x "$PREFIX/bin/tgsp"
chmod +x "$PREFIX/bin/tgsbRender"

# Backward-compat wrapper symlinks
ln -sf tgsp "$PREFIX/bin/tgsb"
ln -sf tgsp "$PREFIX/bin/nvsp"

echo ""
echo "Installation complete!"
echo ""
echo "Quick test:"
echo "  echo 'həˈloʊ wɜld' | $PREFIX/bin/tgsp --lang en-us | aplay -q -r 16000 -f S16_LE -t raw -"
echo ""
echo "Note: 'nvspRender', 'nvsp', and the old share path are symlinked"
echo "      for backward compatibility. Existing Speech Dispatcher configs"
echo "      will continue to work."
echo ""
echo "For Speech Dispatcher integration, see:"
echo "  $PREFIX/share/tgspeechbox/extras/speech-dispatcher/README.md"
echo ""

# Optionally update library cache if installing to system location
if [ "$PREFIX" = "/usr" ] || [ "$PREFIX" = "/usr/local" ]; then
    if [ -x /sbin/ldconfig ] && [ -w /etc/ld.so.conf.d ]; then
        echo "$PREFIX/lib" > /etc/ld.so.conf.d/tgspeechbox.conf
        # Clean up old config if present
        rm -f /etc/ld.so.conf.d/nvspeechplayer.conf
        /sbin/ldconfig
        echo "Library cache updated."
    else
        echo "Note: You may need to run 'sudo ldconfig' or add $PREFIX/lib to LD_LIBRARY_PATH"
    fi
fi
