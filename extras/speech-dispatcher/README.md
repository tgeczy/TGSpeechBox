# Speech Dispatcher (sd_generic) integration

This folder contains a **starter** configuration for using TGSpeechBox with
Speech Dispatcher on Linux.

It uses Speech Dispatcher's built-in **sd_generic** module (so you don't need to
write/compile a Speech Dispatcher C module). The configuration follows the same
pattern as the Debian `espeak-ng-mbrola-generic` config: a shell pipeline is used
for synthesis and playback.

## How it works

Pipeline:

1. Speech Dispatcher sends text (`$DATA`).
2. `espeak-ng --ipa=1` converts text -> IPA.
3. `nvspRender` (built from this repo) converts IPA -> raw PCM.
4. `aplay` plays raw PCM.

## Build nvspRender

From the repo root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DNVSP_BUILD_TOOLS=ON
cmake --build build -j
```

The executable will be at:

```sh
build/nvspRender
```

## Install packs

`nvspRender` needs a pack directory that contains a `packs/` folder.

For a quick local test, you can point it at the repo root:

```sh
./build/nvspRender --packdir . --lang en-us --rate 0 --pitch 50 --volume 1 < ipa.txt | \
  aplay -q -r 22050 -f S16_LE -t raw -
```

For a more “system” install, a common layout is:

- `/usr/share/nvspeechplayer/packs/...`
- `nvspRender` in `/usr/local/bin/`

## Install the Speech Dispatcher module config

1. Copy `nvsp-generic.conf` into your Speech Dispatcher modules directory.

   Common locations:
   - system: `/etc/speech-dispatcher/modules/`
   - user: `~/.config/speech-dispatcher/modules/` (depends on distro)

2. Edit `nvsp-generic.conf` and set the `--packdir` path to where you installed
   the packs.

3. Enable it in `speechd.conf` by adding a module line similar to:

   ```
   AddModule "nvsp" "sd_generic" "nvsp-generic.conf"
   DefaultModule nvsp
   ```

4. Restart Speech Dispatcher and test:

   ```sh
   spd-say "Hello from NVSP"
   ```

## Notes / limitations

- This is a “good enough” proof-of-concept. It relies on `espeak-ng` to produce
  IPA (text -> phonemes), matching how the NVDA add-on drives this engine.
- Speech Dispatcher stop/pause semantics are handled by `sd_generic` process
  management. The pipeline must stop when Speech Dispatcher sends SIGKILL.
- Quotes in text can be tricky for `sd_generic` pipelines; the config uses
  `GenericStripPunctChars` to avoid breaking the command line.
