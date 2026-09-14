# SAPI 5 installer

`TGSpeechSapi.iss` is the Inno Setup script that builds the Windows
installer for the TGSpeechBox SAPI 5 engine. It produces one installer that
serves both 64-bit and 32-bit SAPI hosts.

## What it packages

Since v3.0 the SAPI engine is a **single statically linked DLL per
architecture** — `TGSpeechSapi.dll` contains the DSP (speechPlayer), the
frontend (nvspFrontend) and eSpeak NG. There are no separate
`speechPlayer.dll`, `nvspFrontend.dll` or `libespeak-ng.dll` files any more.

The script reads everything from a staging folder, `release\` at the
repository root (`..\release` relative to the script):

| Staged file | Installed to |
|---|---|
| `release\x64\TGSpeechSapi.dll` | `{app}\x64` (64-bit Windows only) |
| `release\x86\TGSpeechSapi.dll` | `{app}\x86` |
| `release\espeak-ng-data\*` | `{app}\espeak-ng-data` |
| `release\packs\*` | `{app}\packs` (language packs, phonemes, dictionaries) |
| `release\TGSpeechSapiSettings.exe` | `{app}` (the settings app) |
| `release\notice.txt`, `release\LICENSE-GPL3.txt` | `{app}` |

On install it registers the COM server with `regsvr32` — the 64-bit DLL
through the 64-bit `regsvr32` and the 32-bit DLL through the one in
`SysWOW64` (or the only one, on 32-bit Windows) — and unregisters both on
uninstall.

## Building an installer

1. Build the SAPI engine for both architectures (`-DTGSB_BUILD_SAPI=ON`,
   MinSizeRel) and copy `TGSpeechSapi.dll` from each build into
   `release\x64\` and `release\x86\`.
2. Refresh `release\packs\` from the repository's `packs\` folder so the
   installer ships the current language packs and dictionaries.
   `espeak-ng-data`, the licence and the notice rarely change.
3. Set `AppVersion` in `TGSpeechSapi.iss` to the version being shipped
   (for example `3.10b802`) so users can see they are getting a new engine.
4. Compile with the Inno Setup 6 command-line compiler:

   ```
   "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer\TGSpeechSapi.iss
   ```

   The output is `TGSpeechSapiSetup.exe`; rename it to
   `TGSpeechSapiSetup-vXXX.exe` before attaching it to a release.
