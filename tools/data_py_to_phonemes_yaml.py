"""Convert NV Speech Player data.py to packs/phonemes.yaml.

Usage:
  python data_py_to_phonemes_yaml.py path/to/data.py path/to/phonemes.yaml

This keeps the phoneme entries sparse (missing keys stay missing) so _copyAdjacent works.
"""

import ast
import codecs
import sys
from pathlib import Path


def yaml_quote(s: str) -> str:
    # Always quote keys/values because IPA can include ':' and other YAML syntax.
    s = s.replace("\\", "\\\\").replace("\"", "\\\"")
    return f"\"{s}\""


def format_number(x):
    # Keep ints as ints when possible, else use a compact float.
    if isinstance(x, bool):
        return "true" if x else "false"
    if isinstance(x, (int,)):
        return str(x)
    if isinstance(x, float):
        if x.is_integer():
            return str(int(x))
        # Trim trailing zeros.
        s = f"{x:.10g}"
        return s
    return yaml_quote(str(x))


def main(argv):
    if len(argv) != 3:
        print(__doc__.strip())
        return 2

    data_path = Path(argv[1])
    out_path = Path(argv[2])

    data = ast.literal_eval(codecs.open(data_path, "r", "utf-8").read())

    with out_path.open("w", encoding="utf-8", newline="\n") as f:
        f.write("# NV Speech Player phoneme table\n")
        f.write("# Generated from data.py\n")
        f.write("phonemes:\n")

        for phoneme_key in sorted(data.keys(), key=lambda k: (len(k), k)):
            entry = data[phoneme_key]
            f.write(f"  {yaml_quote(phoneme_key)}:\n")

            # Underscore keys first.
            underscore = [k for k in entry.keys() if k.startswith("_")]
            numeric = [k for k in entry.keys() if not k.startswith("_")]
            for k in sorted(underscore):
                v = entry[k]
                f.write(f"    {k}: {format_number(v)}\n")
            for k in sorted(numeric):
                v = entry[k]
                f.write(f"    {k}: {format_number(v)}\n")

    print(f"Wrote {out_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
