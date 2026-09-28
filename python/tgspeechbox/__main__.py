"""python -m tgspeechbox --ipa "həˈloʊ" --lang en-us --out hello.wav"""
import argparse
import sys

from . import __version__, dsp_version, render


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(prog="python -m tgspeechbox", description=__doc__)
    ap.add_argument("--ipa", required=True, help="IPA to speak (one clause)")
    ap.add_argument("--lang", default="en-us")
    ap.add_argument("--voice", default="", help="a voice profile, e.g. Beth")
    ap.add_argument("--speed", type=float, default=1.0)
    ap.add_argument("--pitch", type=float, default=110.0)
    ap.add_argument("--rate", type=int, default=22050, help="sample rate")
    ap.add_argument("--out", help="write a WAV file here")
    a = ap.parse_args(argv)
    u = render(a.ipa, a.lang, voice=a.voice, speed=a.speed, pitch=a.pitch, sample_rate=a.rate)
    print("tgspeechbox %s (DSP v%d): %d frames, %.2f s" % (__version__, dsp_version(), len(u.frames), u.duration_s))
    if a.out:
        u.write_wav(a.out)
        print("wrote", a.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
