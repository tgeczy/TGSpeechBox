#!/bin/bash
# TGSpeechBox: Speech Dispatcher setup, sourced by install.sh.
#
# Speech Dispatcher finds output modules by itself: every sd_<name> program in
# its module folders becomes the module <name>, set up by <name>.conf in a
# "modules" config folder, and every <name>-generic.conf there becomes a
# generic module.  That only happens while speechd.conf has no AddModule
# lines; one AddModule line and Speech Dispatcher loads only what is listed.
# So TGSpeechBox puts its module and config where Speech Dispatcher looks and
# leaves speechd.conf alone: every other synthesizer stays available, and the
# default synthesizer stays the one you chose.
#
# Earlier installers did add AddModule lines (and could change DefaultModule);
# tgsb_sd_cleanup_conf takes back the ones they added.
#
# Test hooks: TGSB_SD_MODULE_DIR and TGSB_SD_CONFIG_DIR replace the folders
# that would be found; answers to the questions are read from stdin.

# The folder Speech Dispatcher loads sd_* programs from.
tgsb_sd_module_dir() {
    if [ -n "$TGSB_SD_MODULE_DIR" ]; then
        echo "$TGSB_SD_MODULE_DIR"
        return
    fi
    if [ "$(id -u)" = 0 ]; then
        local d
        for d in /usr/lib/speech-dispatcher-modules /usr/libexec/speech-dispatcher-modules \
                 /usr/lib64/speech-dispatcher-modules /usr/lib/*/speech-dispatcher-modules; do
            if [ -d "$d" ]; then
                echo "$d"
                return
            fi
        done
        echo "/usr/lib/speech-dispatcher-modules"
    else
        # Speech Dispatcher's per-user module folder.
        echo "${XDG_DATA_HOME:-$HOME/.local/share}/../libexec/speech-dispatcher-modules"
    fi
}

# The folder holding speechd.conf and modules/.
tgsb_sd_config_dir() {
    if [ -n "$TGSB_SD_CONFIG_DIR" ]; then
        echo "$TGSB_SD_CONFIG_DIR"
    elif [ "$(id -u)" = 0 ]; then
        echo "/etc/speech-dispatcher"
    else
        echo "${XDG_CONFIG_HOME:-$HOME/.config}/speech-dispatcher"
    fi
}

# Install the module where Speech Dispatcher finds it: sd_tgsb plus tgsb.conf
# (the module "tgsb"), or, without the native program, tgsb-generic.conf (the
# generic module "tgsb-generic").  $1 = folder with sd_tgsb, $2 = the extras
# speech-dispatcher folder.
tgsb_sd_install() {
    local bin_dir="$1" extras="$2"
    local module_dir config_dir
    module_dir="$(tgsb_sd_module_dir)"
    config_dir="$(tgsb_sd_config_dir)/modules"
    mkdir -p "$config_dir"

    if [ -f "$bin_dir/sd_tgsb" ] && mkdir -p "$module_dir" 2>/dev/null; then
        cp "$bin_dir/sd_tgsb" "$module_dir/sd_tgsb"
        chmod +x "$module_dir/sd_tgsb"
        # Your own edits to an existing tgsb.conf are kept.
        if [ ! -f "$config_dir/tgsb.conf" ]; then
            if [ -f "$config_dir/tgsb-native.conf" ]; then
                cp "$config_dir/tgsb-native.conf" "$config_dir/tgsb.conf"  # the old name, with any edits
            else
                cp "$extras/tgsb-native.conf" "$config_dir/tgsb.conf"
            fi
        fi
        rm -f "$config_dir/tgsb-native.conf"
        # A generic config left from an older install would be found too and
        # show up as a second TGSpeechBox ("tgsb-generic").
        rm -f "$config_dir/tgsb-generic.conf"
        echo "  Installed the module: $module_dir/sd_tgsb"
        echo "  and its settings:     $config_dir/tgsb.conf"
        TGSB_SD_MODULE_NAME="tgsb"
    else
        cp "$extras/tgsb-generic.conf" "$config_dir/tgsb-generic.conf"
        echo "  Installed the generic module settings: $config_dir/tgsb-generic.conf"
        TGSB_SD_MODULE_NAME="tgsb-generic"
    fi
}

# Take back what earlier TGSpeechBox installers added to speechd.conf: the
# "# --- TGSpeechBox (added by install.sh) ---" block with its AddModule line,
# and the espeak-ng AddModule line they inserted just above it.  A copy of the
# file is kept as speechd.conf.tgsb-backup.  DefaultModule is never touched.
tgsb_sd_cleanup_conf() {
    local conf="$1"
    [ -f "$conf" ] || return 0
    grep -q '^# --- TGSpeechBox (added by install.sh) ---$\|^AddModule "tgsb"' "$conf" || return 0
    cp "$conf" "$conf.tgsb-backup"
    awk '
        { lines[NR] = $0 }
        END {
            for (i = 1; i <= NR; i++) {
                if (lines[i] == "# --- TGSpeechBox (added by install.sh) ---") {
                    drop[i] = 1
                    if (i > 1 && lines[i-1] ~ /^AddModule "espeak-ng" "sd_espeak-ng" "espeak-ng.conf"$/) drop[i-1] = 1
                    if (i > 1 && lines[i-1] == "") drop[i-1] = 1
                    if (i > 2 && lines[i-1] ~ /^AddModule "espeak-ng"/ && lines[i-2] == "") drop[i-2] = 1
                }
                if (lines[i] ~ /^AddModule "tgsb"/) drop[i] = 1
            }
            for (i = 1; i <= NR; i++) if (!drop[i]) print lines[i]
        }' "$conf.tgsb-backup" > "$conf"
    echo "  Removed the AddModule lines an earlier TGSpeechBox installer added to $conf"
    echo "  (the file before: $conf.tgsb-backup)."
}

# If speechd.conf still lists modules itself, Speech Dispatcher loads only
# those and won't find TGSpeechBox.  Say so, and ask; change nothing unasked.
tgsb_sd_check_explicit_list() {
    local conf="$1" name="$2"
    [ -f "$conf" ] || return 0
    local listed
    listed="$(grep '^AddModule ' "$conf")" || return 0

    echo ""
    echo "  $conf lists its synthesizers itself:"
    echo "$listed" | sed 's/^/    /'
    echo "  While it does, Speech Dispatcher loads only those and won't find"
    echo "  TGSpeechBox or any other synthesizer installed later."
    echo "  (If you didn't add these lines, an earlier TGSpeechBox installer may"
    echo "  have turned the espeak-ng one on.)"
    echo ""
    local answer
    read -r -p "  Comment them out, so Speech Dispatcher finds every synthesizer? [y/N] " answer || answer=""
    answer="${answer%$'\r'}"
    case "$answer" in
        [yY]|[yY][eE][sS])
            cp "$conf" "$conf.tgsb-backup"
            sed -i 's/^AddModule /#AddModule /' "$conf"
            echo "  Commented them out (the file before: $conf.tgsb-backup)."
            return 0
            ;;
    esac
    read -r -p "  Add TGSpeechBox to that list instead? [y/N] " answer || answer=""
    answer="${answer%$'\r'}"
    case "$answer" in
        [yY]|[yY][eE][sS])
            cp "$conf" "$conf.tgsb-backup"
            if [ "$name" = "tgsb" ]; then
                echo 'AddModule "tgsb" "sd_tgsb" "tgsb.conf"' >> "$conf"
            else
                echo 'AddModule "tgsb-generic" "sd_generic" "tgsb-generic.conf"' >> "$conf"
            fi
            echo "  Added (the file before: $conf.tgsb-backup)."
            ;;
        *)
            echo "  Left as it is. TGSpeechBox is installed but Speech Dispatcher"
            echo "  won't load it until $conf lets it."
            ;;
    esac
}
