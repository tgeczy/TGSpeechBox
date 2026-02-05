# -*- coding: utf-8 -*-
"""NV Speech Player - Audio thread management.

This module contains:
- _BgThread: Background thread for text->IPA->frames generation
- _AudioThread: Thread that pulls synthesized audio and feeds WavePlayer
"""

import array
import ctypes
import math
import queue
import threading
import weakref

import config
import nvwave
from logHandler import log
from synthDriverHandler import synthDoneSpeaking, synthIndexReached


class BgThread(threading.Thread):
    """Runs text->IPA->frames generation so speak() doesn't block NVDA."""
    
    def __init__(self, q: "queue.Queue", stopEvent: threading.Event):
        super().__init__(name=f"{self.__class__.__module__}.{self.__class__.__qualname__}")
        self.daemon = True
        self._q = q
        self._stop = stopEvent

    def run(self):
        while not self._stop.is_set():
            try:
                item = self._q.get(timeout=0.2)
            except queue.Empty:
                continue
            try:
                if item is None:
                    return
                func, args, kwargs = item
                func(*args, **kwargs)
            except Exception:
                log.error("nvSpeechPlayer: error in background thread", exc_info=True)
            finally:
                try:
                    self._q.task_done()
                except Exception:
                    # Should be extremely rare; log for diagnosability.
                    log.debug("nvSpeechPlayer: background queue task_done failed", exc_info=True)


class AudioThread(threading.Thread):
    """Pulls synthesized audio from the DLL and feeds nvwave.WavePlayer."""
    
    # Pre-compute cosine fade table for fast lookup (256 entries)
    _FADE_TABLE_SIZE = 256
    _fadeTable = tuple((1.0 - math.cos(i * math.pi / 255)) / 2.0 
                       for i in range(256))
    
    def __init__(self, synth, player, sampleRate: int):
        """Initialize audio thread.
        
        Args:
            synth: Reference to the SynthDriver instance
            player: speechPlayer.SpeechPlayer instance
            sampleRate: Audio sample rate in Hz
        """
        super().__init__(name=f"{self.__class__.__module__}.{self.__class__.__qualname__}")
        self.daemon = True
        self._synthRef = weakref.ref(synth)
        self._player = player
        self._sampleRate = int(sampleRate)

        self._keepAlive = True
        self.isSpeaking = False

        self._wake = threading.Event()
        self._init = threading.Event()

        self._wavePlayer = None
        self._outputDevice = None

        # Avoid log spam for repeated backend failures.
        self._feedErrorLogged = False
        self._idleErrorLogged = False
        self._synthErrorLogged = False

        # Fade-in state: apply envelope to first audio chunk after stop()/idle()
        self._applyFadeIn = False
        # Fade duration in samples (~12ms for smooth transition that covers stop() discontinuity)
        self._fadeInSamples = int(sampleRate * 0.012)
        # Pre-allocated silence buffer (3ms) to prepend before faded audio
        # This gives the audio device time to stabilize before non-zero samples
        self._silencePrefix = bytes(int(sampleRate * 0.003) * 2)

        self.start()
        self._init.wait()

    def _getOutputDevice(self):
        """Get the configured audio output device."""
        try:
            return config.conf["audio"]["outputDevice"]
        except Exception:
            try:
                return config.conf["speech"]["outputDevice"]
            except Exception:
                return None

    def _feed(self, data: bytes, onDone=None) -> None:
        """Feed audio data to the wave player.
        
        Handles API differences across NVDA versions.
        """
        if not self._wavePlayer:
            return
        try:
            self._wavePlayer.feed(data, len(data), onDone=onDone)
        except TypeError:
            try:
                if onDone is None:
                    self._wavePlayer.feed(data)
                else:
                    self._wavePlayer.feed(data, onDone=onDone)
            except Exception:
                if not self._feedErrorLogged:
                    log.error("nvSpeechPlayer: WavePlayer.feed failed", exc_info=True)
                    self._feedErrorLogged = True

    def _applyFadeInEnvelope(self, audioBytes: bytes) -> bytes:
        """Apply fade-in envelope to audio samples. Returns modified bytes.
        
        Uses a modified cosine curve that starts at zero and ramps up.
        The first few samples are forced to zero to mask any click from stop().
        """
        samples = array.array('h')
        samples.frombytes(audioBytes)
        fadeLen = min(self._fadeInSamples, len(samples))
        
        if fadeLen > 0:
            # Force first few samples to absolute zero (mask any click)
            zeroSamples = min(fadeLen // 4, 30)  # ~0.7ms at 44.1kHz
            for i in range(zeroSamples):
                samples[i] = 0
            
            # Apply cosine fade to remaining samples
            tableSize = self._FADE_TABLE_SIZE
            fadeTable = self._fadeTable
            fadeStart = zeroSamples
            fadeRemaining = fadeLen - fadeStart
            
            for i in range(fadeStart, fadeLen):
                # Map sample index to table index (starting from where zeros end)
                progress = (i - fadeStart) / fadeRemaining if fadeRemaining > 0 else 1.0
                tableIdx = int(progress * (tableSize - 1))
                samples[i] = int(samples[i] * fadeTable[tableIdx])
        
        # Prepend silence to let audio device stabilize
        return self._silencePrefix + samples.tobytes()

    def terminate(self):
        """Stop the audio thread and clean up resources."""
        self._keepAlive = False
        self.isSpeaking = False
        self._wake.set()
        
        # Join with timeout - thread should exit quickly since _keepAlive is False
        # and we've set the wake event.
        try:
            self.join(timeout=2.0)
        except TypeError:
            # Very old Python versions without timeout parameter - just join blocking
            # This is rare in practice (Python 3.7+ all support timeout)
            try:
                self.join()
            except (RuntimeError, AttributeError):
                # Thread not started or already dead
                pass
        except (RuntimeError, AttributeError):
            # RuntimeError: cannot join current thread / thread not started
            # AttributeError: shouldn't happen but be defensive
            pass
        except Exception:
            log.debug("nvSpeechPlayer: AudioThread.join raised unexpected error", exc_info=True)
        
        # Stop the wave player
        try:
            if self._wavePlayer:
                self._wavePlayer.stop()
        except (OSError, AttributeError):
            # OSError: audio device error
            # AttributeError: _wavePlayer methods gone
            pass
        except Exception:
            log.debug("nvSpeechPlayer: WavePlayer.stop failed during terminate", exc_info=True)

    def kick(self):
        """Wake up the audio thread to start processing."""
        self._wake.set()

    def run(self):
        """Main audio thread loop."""
        # Initialize WavePlayer
        try:
            self._outputDevice = self._getOutputDevice()
            # Try to create WavePlayer with AudioPurpose.SPEECH for NVDA's built-in
            # audio keepalive and trimming features
            try:
                self._wavePlayer = nvwave.WavePlayer(
                    channels=1,
                    samplesPerSec=self._sampleRate,
                    bitsPerSample=16,
                    outputDevice=self._outputDevice,
                    purpose=nvwave.AudioPurpose.SPEECH,
                )
            except (TypeError, AttributeError):
                # Older NVDA versions don't have purpose parameter or AudioPurpose
                try:
                    self._wavePlayer = nvwave.WavePlayer(
                        channels=1,
                        samplesPerSec=self._sampleRate,
                        bitsPerSample=16,
                        outputDevice=self._outputDevice,
                        buffered=True,
                    )
                except TypeError:
                    self._wavePlayer = nvwave.WavePlayer(
                        channels=1,
                        samplesPerSec=self._sampleRate,
                        bitsPerSample=16,
                        outputDevice=self._outputDevice,
                    )
        except Exception:
            log.error("nvSpeechPlayer: failed to initialize audio output", exc_info=True)
            self._wavePlayer = None
        finally:
            self._init.set()

        # Local references for faster access in tight loop
        player = self._player
        wavePlayer = self._wavePlayer
        wake = self._wake
        synthRef = self._synthRef
        
        while self._keepAlive:
            wake.wait()
            wake.clear()

            lastIndex = None
            isFirstChunk = True

            while self._keepAlive and self.isSpeaking:
                try:
                    data = player.synthesize(8192)
                except Exception:
                    if not self._synthErrorLogged:
                        log.error("nvSpeechPlayer: speechPlayer.synthesize failed", exc_info=True)
                        self._synthErrorLogged = True
                    break

                if data:
                    n = int(getattr(data, "length", 0) or 0)
                    if n <= 0:
                        continue

                    nbytes = n * 2  # 16-bit = 2 bytes per sample
                    audioBytes = ctypes.string_at(ctypes.addressof(data), nbytes)
                    
                    # Apply fade-in to first chunk after stop()/idle() to prevent click
                    if self._applyFadeIn and isFirstChunk:
                        audioBytes = self._applyFadeInEnvelope(audioBytes)
                        self._applyFadeIn = False
                    isFirstChunk = False

                    idx = int(player.getLastIndex())
                    s = synthRef()

                    if idx >= 0:
                        def cb(index=idx, synth=s):
                            if synth:
                                synthIndexReached.notify(synth=synth, index=index)
                        self._feed(audioBytes, onDone=cb)
                    else:
                        self._feed(audioBytes)

                    lastIndex = idx
                    continue

                # No audio was produced - check for index markers
                idx = int(player.getLastIndex())
                if idx >= 0 and idx != lastIndex:
                    s = synthRef()
                    if s:
                        def cb(index=idx, synth=s):
                            if synth:
                                synthIndexReached.notify(synth=synth, index=index)
                        self._feed(b"", onDone=cb)
                    lastIndex = idx
                    continue

                break

            # Stream finished - go idle and prepare for next stream
            try:
                if wavePlayer:
                    wavePlayer.idle()
                    # Next audio feed should have fade-in applied
                    self._applyFadeIn = True
            except Exception:
                if not self._idleErrorLogged:
                    log.debug("nvSpeechPlayer: WavePlayer.idle failed", exc_info=True)
                    self._idleErrorLogged = True

            s = synthRef()
            if s:
                synthDoneSpeaking.notify(synth=s)

            self.isSpeaking = False
