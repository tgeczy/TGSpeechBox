#!/bin/bash
# TGSpeechBox Linux Installer
# Installs to /usr/local by default, or a custom prefix

set -e

PREFIX="${1:-/usr/local}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo ""
echo "============================================"
echo "  TGSpeechBox Installer"
echo "============================================"
echo ""
echo "Installing to $PREFIX..."

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

# Install tgsb-speak (clause-aware Speech Dispatcher wrapper)
EXTRAS_SD="$PREFIX/share/tgspeechbox/extras/speech-dispatcher"
if [ -f "$EXTRAS_SD/tgsb-speak" ]; then
    cp "$EXTRAS_SD/tgsb-speak" "$PREFIX/bin/tgsb-speak"
    chmod +x "$PREFIX/bin/tgsb-speak"
fi

# Symlink into /usr/bin if installing to /usr/local — systemd services
# (like speech-dispatcher via socket activation) often only have /usr/bin
# in their PATH, not /usr/local/bin.
if [ "$PREFIX" = "/usr/local" ] && [ -d /usr/bin ]; then
    for cmd in tgsbRender tgsp tgsb-speak tgsb nvsp; do
        if [ -f "$PREFIX/bin/$cmd" ] || [ -L "$PREFIX/bin/$cmd" ]; then
            ln -sf "$PREFIX/bin/$cmd" "/usr/bin/$cmd"
        fi
    done
    echo "  Symlinked binaries into /usr/bin (for systemd PATH compatibility)."
fi

echo ""
echo "  Core files installed."

# Optionally update library cache if installing to system location
if [ "$PREFIX" = "/usr" ] || [ "$PREFIX" = "/usr/local" ]; then
    if [ -x /sbin/ldconfig ] && [ -w /etc/ld.so.conf.d ]; then
        echo "$PREFIX/lib" > /etc/ld.so.conf.d/tgspeechbox.conf
        # Clean up old config if present
        rm -f /etc/ld.so.conf.d/nvspeechplayer.conf
        /sbin/ldconfig
        echo "  Library cache updated."
    else
        echo ""
        echo "  Note: You may need to run 'sudo ldconfig' or add $PREFIX/lib to LD_LIBRARY_PATH"
    fi
fi

# ============================================================================
# Dependency check
# ============================================================================

echo ""
echo "--------------------------------------------"
echo "  Checking dependencies"
echo "--------------------------------------------"
echo ""

dep_ok=true

# espeak-ng (required)
if command -v espeak-ng >/dev/null 2>&1; then
    echo "  [OK] espeak-ng"
else
    echo "  [MISSING] espeak-ng — required for text-to-phoneme conversion"
    echo "            Install: sudo apt install espeak-ng  (Debian/Ubuntu)"
    echo "                     sudo dnf install espeak-ng  (Fedora)"
    dep_ok=false
fi

# tgsbRender (just installed)
if command -v tgsbRender >/dev/null 2>&1; then
    echo "  [OK] tgsbRender"
else
    echo "  [WARNING] tgsbRender not found in PATH"
    echo "            This shouldn't happen — check that $PREFIX/bin is in your PATH."
    dep_ok=false
fi

# Audio output (paplay preferred, aplay as fallback)
if command -v paplay >/dev/null 2>&1; then
    echo "  [OK] paplay (PipeWire/PulseAudio audio — preferred)"
elif command -v aplay >/dev/null 2>&1; then
    echo "  [OK] aplay (ALSA audio)"
else
    echo "  [MISSING] No audio player found (need paplay or aplay)"
    echo "            Install: sudo apt install pulseaudio-utils  (for paplay)"
    echo "                     sudo apt install alsa-utils        (for aplay)"
    dep_ok=false
fi

# python3 (optional — no longer required for tgsb-speak)
if command -v python3 >/dev/null 2>&1; then
    echo "  [OK] python3"
fi

echo ""
if [ "$dep_ok" = true ]; then
    echo "  All required dependencies found."
else
    echo "  Some dependencies are missing (see above)."
    echo "  TGSpeechBox may not work until they are installed."
fi

# ============================================================================
# Quick self-test
# ============================================================================

echo ""
echo "--------------------------------------------"
echo "  Running self-test"
echo "--------------------------------------------"
echo ""

if command -v espeak-ng >/dev/null 2>&1 && command -v tgsbRender >/dev/null 2>&1; then
    test_output=$(echo 'həˈloʊ' | tgsbRender --packdir "$PREFIX/share/tgspeechbox" --lang en-us 2>/dev/null | wc -c)
    if [ "$test_output" -gt 1000 ] 2>/dev/null; then
        echo "  [OK] tgsbRender produces audio ($test_output bytes)"
    else
        echo "  [FAIL] tgsbRender produced no audio or too little ($test_output bytes)"
        echo "         This may indicate a glibc incompatibility or missing packs."
        echo "         Try: ldd $PREFIX/bin/tgsbRender"
    fi

    # Test in-process espeak mode (preferred — no pipe chain)
    test_espeak=$(tgsbRender --espeak --text 'hello' --packdir "$PREFIX/share/tgspeechbox" --lang en-us 2>/dev/null | wc -c)
    if [ "$test_espeak" -gt 1000 ] 2>/dev/null; then
        echo "  [OK] In-process espeak works (tgsbRender --espeak: $test_espeak bytes)"
    else
        echo "  [INFO] In-process espeak not available — using pipe fallback"
        # Test pipe chain fallback
        test_pipeline=$(echo 'hello' | espeak-ng -q -v en-us --ipa=1 --stdin 2>/dev/null | tgsbRender --packdir "$PREFIX/share/tgspeechbox" --lang en-us 2>/dev/null | wc -c)
        if [ "$test_pipeline" -gt 1000 ] 2>/dev/null; then
            echo "  [OK] Pipe fallback works (espeak-ng → tgsbRender: $test_pipeline bytes)"
        else
            echo "  [FAIL] Neither in-process nor pipe mode produced audio"
            echo "         Check espeak-ng installation and language data."
        fi
    fi
else
    echo "  Skipped (missing espeak-ng or tgsbRender)."
fi

# ============================================================================
# Speech Dispatcher integration (optional)
# ============================================================================

configure_speech_dispatcher() {
    # Install the module where Speech Dispatcher finds it by itself, and leave
    # speechd.conf alone (see speechd-setup.sh).
    # shellcheck source=speechd-setup.sh
    . "$SCRIPT_DIR/speechd-setup.sh"
    local extras="$PREFIX/share/tgspeechbox/extras/speech-dispatcher"

    echo ""
    tgsb_sd_install "$SCRIPT_DIR/bin" "$extras"

    # Take back what earlier installers added, wherever they added it: the
    # system speechd.conf, or that of the user running sudo.
    local _home="${SUDO_USER:+$(eval echo ~$SUDO_USER)}"
    _home="${_home:-$HOME}"
    local user_sd_conf="$_home/.config/speech-dispatcher/speechd.conf"
    local sd_conf
    sd_conf="$(tgsb_sd_config_dir)/speechd.conf"
    tgsb_sd_cleanup_conf "$sd_conf"
    if [ "$user_sd_conf" != "$sd_conf" ]; then
        tgsb_sd_cleanup_conf "$user_sd_conf"
    fi

    # Speech Dispatcher reads the user's speechd.conf when there is one.
    local effective_conf="$sd_conf"
    if [ -f "$user_sd_conf" ]; then
        effective_conf="$user_sd_conf"
    fi
    tgsb_sd_check_explicit_list "$effective_conf" "$TGSB_SD_MODULE_NAME"

    # Copy the module's settings template to the per-user location (don't overwrite)
    local src_native="$extras/tgsb-native.conf"
    if [ -n "$_home" ] && [ -f "$src_native" ] && [ "$TGSB_SD_MODULE_NAME" = "tgsb" ]; then
        local user_conf_dir="$_home/.config/tgspeechbox"
        local user_conf="$user_conf_dir/sd_tgsb.conf"
        if [ ! -f "$user_conf" ]; then
            mkdir -p "$user_conf_dir"
            cp "$src_native" "$user_conf"
            if [ -n "$SUDO_USER" ]; then
                chown -R "$SUDO_USER:$SUDO_USER" "$user_conf_dir"
            fi
            echo "  Per-user settings template: $user_conf"
            echo "  (Uncomment lines to customize; see the comments in the file)"
        else
            echo "  Per-user settings exist: $user_conf (not overwritten)"
        fi
    fi

    echo ""
    echo "--------------------------------------------"
    echo "  Speech Dispatcher setup complete!"
    echo "--------------------------------------------"
    echo ""
    echo "  TGSpeechBox is the \"$TGSB_SD_MODULE_NAME\" synthesizer. Your default"
    echo "  synthesizer and the others you have are unchanged."
    echo ""
    echo "  To apply:  killall speech-dispatcher"
    echo "  To test:   spd-say -o $TGSB_SD_MODULE_NAME 'Hello from TGSpeechBox'"
    echo "  To use it: Orca Preferences > Speech > Speech Synthesizer"
    echo ""

    return 0
}

# --- Ask the user ---

# Only offer if speech-dispatcher appears to be installed
if command -v spd-say >/dev/null 2>&1 || [ -f /etc/speech-dispatcher/speechd.conf ] || [ -f "$HOME/.config/speech-dispatcher/speechd.conf" ]; then
    echo ""
    echo "--------------------------------------------"
    echo "  Speech Dispatcher integration"
    echo "--------------------------------------------"
    echo ""
    echo "  Speech Dispatcher detected on this system."
    echo "  TGSpeechBox can register as a synthesizer so"
    echo "  screen readers like Orca can use it."
    echo ""
    read -r -p "  Configure Speech Dispatcher? [Y/n] " do_sd
    case "$do_sd" in
        [nN]|[nN][oO])
            echo ""
            echo "  Skipped Speech Dispatcher setup."
            echo "  To configure manually later, see:"
            echo "    $PREFIX/share/tgspeechbox/extras/speech-dispatcher/README.md"
            ;;
        *)
            configure_speech_dispatcher || true
            ;;
    esac
else
    echo ""
    echo "  Speech Dispatcher not detected."
    echo "  To integrate later, see:"
    echo "    $PREFIX/share/tgspeechbox/extras/speech-dispatcher/README.md"
fi

echo ""
echo "============================================"
echo "  Installation complete!"
echo "============================================"
echo ""
echo "  Quick manual test:"
echo "    echo 'hello world' | espeak-ng -q -v en-us --ipa=1 --stdin \\"
echo "      | tgsbRender --packdir $PREFIX/share/tgspeechbox --lang en-us \\"
echo "      | paplay --raw --rate=22050 --channels=1 --format=s16le"
echo ""
echo "  Backward-compat symlinks (nvspRender, nvsp) are installed"
echo "  for existing Speech Dispatcher configs."
echo ""
