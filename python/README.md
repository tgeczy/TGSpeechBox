# tgspeechbox

TGSpeechBox's formant speech engine from Python: the same DSP and IPA frontend that the NVDA add-on, the SAPI voice, Android, iOS and Linux run. It takes IPA in and gives frames and audio out, along with the traces the engine records along the way.

```python
import tgspeechbox

u = tgspeechbox.render("həˈloʊ wˈɜːld", "en-us")
u.write_wav("hello.wav")

for f in u.frames[:5]:
    print(f.duration_ms, None if f.is_silence else (f.params["cf1"], f.params["cf2"]))
```

Or from the command line: `python -m tgspeechbox --ipa "həˈloʊ" --lang en-us --out hello.wav`

- **`render(ipa, lang, voice=..., speed=..., pitch=...)`** returns an `Utterance`: int16 samples, the frames, the frame trace (which frame each phoneme starts at) and the pass trace (every frontend pass's view of every phoneme).
- **`Frontend(lang)`** turns IPA into frames. It also lists voice profiles, sets a profile, returns its voice source, and runs the text-preparation pass.
- **`Player(sample_rate)`** turns frames into audio, and reads or changes the voice-source settings (`voicing_tone`, `set_voicing_tone`).

## No eSpeak inside

TGSpeechBox turns text into IPA with eSpeak NG. eSpeak NG is GPL; this package is MIT and contains none of it. Pass IPA directly, or phonemize with an eSpeak NG you have: `espeak-ng -q --ipa -v en-us "hello world"`.

## Platforms

A wheel per platform (Windows 64- and 32-bit, macOS universal, Linux x86_64 and aarch64) is attached to each TGSpeechBox release on GitHub. They are ctypes-based, so one wheel serves every Python 3 version. It isn't on PyPI yet; it will be once the wheels have proven themselves.

License: MIT (see LICENSE). https://github.com/tgeczy/TGSpeechBox
