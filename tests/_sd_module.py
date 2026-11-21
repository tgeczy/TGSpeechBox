"""Speech Dispatcher's side of its module protocol, to drive sd_tgsb in tests.

The test plays the server: it starts the module with a configuration file,
speaks the text protocol on its stdin and stdout, and decodes the audio the
module sends back in 705 AUDIO blocks (speech-dispatcher's
src/modules/module_process.c; the module's sd_send_audio).  Linux only: the
module is a Linux program and loads the system's espeak-ng.

    with SdModule(module_path, packs_dir, tmp_path) as sd:
        said = sd.message("SPEAK", "<speak>Hello</speak>")
        said.samples, said.pcm, said.ended

A SPEAK carries SSML, as the server sends it: the text wrapped in <speak>
and its &, < and > written as entities.  CHAR and KEY carry the character
or the key name as it is.
"""
from __future__ import annotations

import os
import subprocess
from dataclasses import dataclass, field


@dataclass
class Said:
    """What the module answered to one message."""
    pcm: bytes = b""            # 16-bit mono, as sent
    sample_rate: int = 0
    events: list = field(default_factory=list)   # the 7xx lines, in order
    refused: str = ""           # a 3xx answer in place of "200 OK SPEAKING"

    @property
    def samples(self) -> int:
        return len(self.pcm) // 2

    @property
    def ended(self) -> str:
        """"END", "STOP", or "" when the message was refused."""
        for line in reversed(self.events):
            if line.startswith("702"):
                return "END"
            if line.startswith("703"):
                return "STOP"
        return ""

    def loud(self, threshold: int = 500) -> bool:
        """True when there is more than silence in it."""
        import array
        a = array.array("h")
        a.frombytes(self.pcm[: len(self.pcm) // 2 * 2])
        return any(s > threshold or s < -threshold for s in a)


def _unescape(data: bytes) -> bytes:
    """The 705 block's escaping: 0x7D, then the byte with bit 5 flipped."""
    out = bytearray()
    it = iter(data)
    for b in it:
        if b == 0x7D:
            out.append(next(it) ^ 0x20)
        else:
            out.append(b)
    return bytes(out)


class SdModule:
    def __init__(self, module_path, packs_dir, work_dir, conf_lines=(), language="en-us"):
        work_dir = str(work_dir)
        conf = os.path.join(work_dir, "tgsb.conf")
        with open(conf, "w", encoding="utf-8") as f:
            f.write('TGSBPackDir "%s"\nTGSBDefaultLanguage "%s"\n' % (packs_dir, language))
            for line in conf_lines:
                f.write(line + "\n")
        env = dict(os.environ)
        env["HOME"] = work_dir          # no per-user configuration of the tester's is read
        env["TGSB_PACKDIR"] = str(packs_dir)
        self.proc = subprocess.Popen([str(module_path), conf], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=subprocess.DEVNULL, env=env)
        self._send("INIT")
        answer = self._line()
        while answer[:3] not in ("299", "399") and answer:
            answer = self._line()
        if not answer.startswith("299"):
            self.close()
            raise RuntimeError("the module did not start: %r" % answer)

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def close(self):
        if self.proc.poll() is None:
            try:
                self._send("QUIT")
                self.proc.wait(timeout=5)
            except Exception:
                self.proc.kill()

    # ---- the wire ----

    def _send(self, text: str) -> None:
        self.proc.stdin.write(text.encode("utf-8") + b"\n")
        self.proc.stdin.flush()

    def _raw_line(self) -> bytes:
        line = self.proc.stdout.readline()
        return line[:-1] if line.endswith(b"\n") else line

    def _line(self) -> str:
        return self._raw_line().decode("utf-8", "replace")

    # ---- the protocol ----

    def set(self, **settings) -> None:
        """A SET block: set(language="pt-PT", rate=20)."""
        self._send("SET")
        assert self._line().startswith("202")
        for key, value in settings.items():
            self._send("%s=%s" % (key, value))
        self._send(".")
        assert self._line().startswith("203")

    def message(self, command: str, text: str) -> Said:
        """SPEAK, CHAR or KEY with `text`, read to the end of its audio."""
        said = Said()
        self._send(command)
        assert self._line().startswith("202"), "the module did not take a %s" % command
        for line in text.split("\n"):
            self._send("." + line if line.startswith(".") else line)
        self._send(".")
        answer = self._line()
        if not answer.startswith("200"):
            said.refused = answer
            return said
        pcm = bytearray()
        while True:
            raw = self._raw_line()
            if not raw and self.proc.poll() is not None:
                raise RuntimeError("the module ended in the middle of a message")
            if raw.startswith(b"705-AUDIO\x00"):
                pcm += _unescape(raw[len(b"705-AUDIO\x00"):])
            elif raw.startswith(b"705-sample_rate="):
                said.sample_rate = int(raw.split(b"=", 1)[1])
            elif raw.startswith(b"705"):
                continue
            else:
                line = raw.decode("utf-8", "replace")
                said.events.append(line)
                if line.startswith("702") or line.startswith("703"):
                    break
        said.pcm = bytes(pcm)
        return said
