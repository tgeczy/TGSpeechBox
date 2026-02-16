# NVDA API Reference (from source docstrings)

Auto-generated from: `C:\Users\Tomi\nvda\source`  
Modules scanned: 33  

---

## `synthDriverHandler.py`

### class `LanguageInfo`(StringParameterInfo)  *(line 38)*

  Holds information for a particular language

### `LanguageInfo.__init__`(self, id)  *(line 41)*

  Given a language ID (locale name) the description is automatically calculated.

### class `VoiceInfo`(StringParameterInfo)  *(line 47)*

  Provides information about a single synthesizer voice.

### `VoiceInfo.__init__`(self, id, displayName, language: Optional[str]=None)  *(line 50)*

  @param language: The ID of the language this voice speaks,
          C{None} if not known or the synth implements language separate from voices.

### class `SynthDriver`(driverHandler.Driver)  *(line 59)*

  Abstract base synthesizer driver.
  Each synthesizer driver should be a separate Python module in the root synthDrivers directory
  containing a SynthDriver class
  which inherits from this base class.
  
  At a minimum, synth drivers must set L{name} and L{description} and override the L{check} method.
  The methods L{speak}, L{cancel} and L{pause} should be overridden as appropriate.
  L{supportedSettings} should be set as appropriate for the settings supported by the synthesiser.
  There are factory functions to create L{autoSettingsUtils.driverSetting.DriverSetting} instances
  for common settings;
  e.g. L{VoiceSetting} and L{RateSetting}.
  Each setting is retrieved and set using attributes named after the setting;
  e.g. the L{voice} attribute is used for the L{voice} setting.
  These will usually be properties.
  L{supportedCommands} should specify what synth commands the synthesizer supports.
  At a minimum, L{IndexCommand} must be supported.
  L{PitchCommand} must also be supported if you want capital pitch change to work;
  support for the pitch setting is not sufficient.
  L{supportedNotifications} should specify what notifications the synthesizer provides.
  Currently, the available notifications are L{synthIndexReached} and L{synthDoneSpeaking}.
  Both of these must be supported.
  @ivar pitch: The current pitch; ranges between 0 and 100.
  @type pitch: int
  @ivar rate: The current rate; ranges between 0 and 100.
  @type rate: int
  @ivar volume: The current volume; ranges between 0 and 100.
  @type volume: int
  @ivar variant: The current variant of the voice.
  @type variant: str
  @ivar availableVariants: The available variants of the voice.
  @type availableVariants: OrderedDict of [L{VoiceInfo} keyed by VoiceInfo's ID
  @ivar inflection: The current inflection; ranges between 0 and 100.
  @type inflection: int

### @classmethod `SynthDriver.LanguageSetting`(cls)  *(line 122)*

  Factory function for creating a language setting.

### @classmethod `SynthDriver.VoiceSetting`(cls)  *(line 134)*

  Factory function for creating voice setting.

### @classmethod `SynthDriver.VariantSetting`(cls)  *(line 146)*

  Factory function for creating variant setting.

### @classmethod `SynthDriver.RateSetting`(cls, minStep=1)  *(line 158)*

  Factory function for creating rate setting.

### @classmethod `SynthDriver.RateBoostSetting`(cls)  *(line 171)*

  Factory function for creating rate boost setting.

### @classmethod `SynthDriver.VolumeSetting`(cls, minStep=1)  *(line 184)*

  Factory function for creating volume setting.

### @classmethod `SynthDriver.PitchSetting`(cls, minStep=1)  *(line 198)*

  Factory function for creating pitch setting.

### @classmethod `SynthDriver.InflectionSetting`(cls, minStep=1)  *(line 211)*

  Factory function for creating inflection setting.

### @classmethod `SynthDriver.UseWasapiSetting`(cls) -> BooleanDriverSetting  *(line 224)*

  Factory function for creating 'Use WASAPI' setting.

### `SynthDriver.speak`(self, speechSequence)  *(line 236)*

  Speaks the given sequence of text and speech commands.
  @param speechSequence: a list of text strings and SynthCommand objects (such as index and parameter changes).
  @type speechSequence: list of string and L{SynthCommand}

### `SynthDriver.cancel`(self)  *(line 244)*

  Silence speech immediately.

### `SynthDriver._get_language`(self) -> Optional[str]  *(line 247)*

### `SynthDriver._set_language`(self, language)  *(line 250)*

### `SynthDriver._get_availableLanguages`(self) -> Set[Optional[str]]  *(line 253)*

### `SynthDriver._get_voice`(self)  *(line 256)*

### `SynthDriver._set_voice`(self, value)  *(line 259)*

### `SynthDriver._getAvailableVoices`(self) -> OrderedDict[str, VoiceInfo]  *(line 262)*

  fetches an ordered dictionary of voices that the synth supports.
  @returns: an OrderedDict of L{VoiceInfo} instances representing the available voices, keyed by ID

### `SynthDriver._get_availableVoices`(self) -> OrderedDict[str, VoiceInfo]  *(line 268)*

### `SynthDriver._get_rate`(self) -> int  *(line 277)*

### `SynthDriver._set_rate`(self, value: int)  *(line 280)*

### `SynthDriver._get_pitch`(self) -> int  *(line 287)*

### `SynthDriver._set_pitch`(self, value: int)  *(line 290)*

### `SynthDriver._get_volume`(self) -> int  *(line 297)*

### `SynthDriver._set_volume`(self, value: int)  *(line 300)*

### `SynthDriver._get_variant`(self)  *(line 303)*

### `SynthDriver._set_variant`(self, value)  *(line 306)*

### `SynthDriver._getAvailableVariants`(self)  *(line 309)*

  fetches an ordered dictionary of variants that the synth supports, keyed by ID
  @returns: an ordered dictionary of L{VoiceInfo} instances representing the available variants
  @rtype: OrderedDict

### `SynthDriver._get_availableVariants`(self)  *(line 316)*

### `SynthDriver._get_inflection`(self)  *(line 321)*

### `SynthDriver._set_inflection`(self, value)  *(line 324)*

### `SynthDriver.pause`(self, switch)  *(line 327)*

  Pause or resume speech output.
  @param switch: C{True} to pause, C{False} to resume (unpause).
  @type switch: bool

### `SynthDriver.languageIsSupported`(self, lang: str | None) -> bool  *(line 334)*

  Determines if the specified language is supported.
  :param lang: A language code or None.
  :return: ``True`` if the language is supported, ``False`` otherwise.

### `SynthDriver.initSettings`(self)  *(line 353)*

### `SynthDriver.loadSettings`(self, onlyChanged=False)  *(line 375)*

### `SynthDriver._get_initialSettingsRingSetting`(self)  *(line 406)*

### `initialize`()  *(line 424)*

### `changeVoice`(synth, voice)  *(line 428)*

### `_getSynthDriver`(name) -> SynthDriver  *(line 441)*

### `getSynthList`() -> List[Tuple[str, str]]  *(line 445)*

### `getSynth`() -> Optional[SynthDriver]  *(line 475)*

### `getSynthInstance`(name, asDefault=False)  *(line 479)*

### `setSynth`(name: Optional[str], isFallback: bool=False)  *(line 493)*

### `findAndSetNextSynth`(currentSynthName: str) -> bool  *(line 539)*

  Returns True if the next synth could be found, False if currentSynthName is the last synth
  in the defaultSynthPriorityList

### `handlePostConfigProfileSwitch`(resetSpeechIfNeeded=True)  *(line 554)*

  Switches synthesizers and or applies new voice settings to the synth due to a config profile switch.
  @var resetSpeechIfNeeded: if true and a new synth will be loaded, speech queues are fully reset first.
  This is what happens by default.
  However, Speech itself may call this with false internally if this is a config profile switch within a
  currently processing speech sequence.
  @type resetSpeechIfNeeded: bool

### `isDebugForSynthDriver`()  *(line 575)*

---

## `synthDrivers/_espeak.py`

### class `espeak_EVENT_id`(Union)  *(line 90)*

### class `espeak_EVENT`(Structure)  *(line 98)*

### class `espeak_VOICE`(Structure)  *(line 111)*

### `espeak_VOICE.__eq__`(self, other)  *(line 124)*

### `espeak_VOICE.__hash__`(self)  *(line 129)*

### `encodeEspeakString`(text)  *(line 138)*

### `decodeEspeakString`(data)  *(line 142)*

### `callback`(wav, numsamples, event)  *(line 150)*

### class `BgThread`(threading.Thread)  *(line 199)*

### `BgThread.__init__`(self)  *(line 200)*

### `BgThread.run`(self)  *(line 204)*

### `_execWhenDone`(func, *args, mustBeAsync=False, **kwargs)  *(line 217)*

### `_speak`(text)  *(line 227)*

### `speak`(text)  *(line 243)*

### `stop`()  *(line 248)*

### `pause`(switch)  *(line 268)*

### `setParameter`(param, value, relative)  *(line 273)*

### `getParameter`(param, current)  *(line 277)*

### `getVoiceList`()  *(line 281)*

### `getCurrentVoice`()  *(line 291)*

### `setVoice`(voice)  *(line 299)*

### `setVoiceByName`(name)  *(line 304)*

### `_setVoiceAndVariant`(voice=None, variant=None)  *(line 308)*

### `setVoiceAndVariant`(voice=None, variant=None)  *(line 327)*

### `_setVoiceByLanguage`(lang)  *(line 331)*

### `setVoiceByLanguage`(lang)  *(line 342)*

### `espeak_errcheck`(res, func, args)  *(line 346)*

### `initialize`(indexCallback=None)  *(line 352)*

  @param indexCallback: A function which is called when eSpeak reaches an index.
          It is called with one argument:
          the number of the index or C{None} when speech stops.

### `terminate`()  *(line 394)*

### `info`()  *(line 408)*

### `getVariantDict`()  *(line 413)*

---

## `synthDrivers/espeak.py`

### class `SynthDriver`(SynthDriver)  *(line 37)*

### @classmethod `SynthDriver.check`(cls)  *(line 211)*

### `SynthDriver.__init__`(self)  *(line 214)*

### `SynthDriver._get_language`(self)  *(line 226)*

### `SynthDriver._processText`(self, text)  *(line 241)*

### `SynthDriver._normalizeLangCommand`(self, command: LangChangeCommand) -> LangChangeCommand  *(line 252)*

  Checks if a LangChangeCommand language is compatible with eSpeak.
  If not, find a default mapping occurs in L{_defaultLangToLocale}.
  Otherwise, finds a language of a different dialect exists (e.g. ru-ru to ru).
  Returns an eSpeak compatible LangChangeCommand.

### `SynthDriver._handleLangChangeCommand`(self, langChangeCommand: LangChangeCommand, langChanged: bool) -> str  *(line 296)*

  Get language xml tags needed to handle a lang change command.
  - if a language change has already been handled for this speech,
  close the open voice tag.
  - if the language is supported by eSpeak, switch to that language.
  - otherwise, switch to the default synthesizer language.

### `SynthDriver.speak`(self, speechSequence: SpeechSequence)  *(line 323)*

### `SynthDriver.cancel`(self)  *(line 383)*

### `SynthDriver.pause`(self, switch)  *(line 386)*

### `SynthDriver._get_rateBoost`(self)  *(line 392)*

### `SynthDriver._set_rateBoost`(self, enable)  *(line 395)*

### `SynthDriver._get_rate`(self)  *(line 402)*

### `SynthDriver._set_rate`(self, rate)  *(line 408)*

### `SynthDriver._get_pitch`(self)  *(line 414)*

### `SynthDriver._set_pitch`(self, pitch)  *(line 418)*

### `SynthDriver._get_inflection`(self)  *(line 422)*

### `SynthDriver._set_inflection`(self, val)  *(line 426)*

### `SynthDriver._get_volume`(self) -> int  *(line 430)*

### `SynthDriver._set_volume`(self, volume: int)  *(line 433)*

### `SynthDriver._getAvailableVoices`(self)  *(line 436)*

### `SynthDriver._get_voice`(self)  *(line 447)*

### `SynthDriver._set_voice`(self, identifier)  *(line 457)*

### `SynthDriver._onIndexReached`(self, index)  *(line 472)*

### `SynthDriver.terminate`(self)  *(line 478)*

### `SynthDriver._get_variant`(self)  *(line 481)*

### `SynthDriver._set_variant`(self, val)  *(line 484)*

### `SynthDriver._getAvailableVariants`(self)  *(line 488)*

---

## `synthDrivers/oneCore.py`

> Synth driver for Windows OneCore voices.

### class `_OcSsmlConverter`(speechXml.SsmlConverter)  *(line 60)*

### `_OcSsmlConverter.__init__`(self, defaultLanguage: str, availableLanguages: Set[str])  *(line 61)*

  Used for newer OneCore installations (OneCore API > 5)
  where supportsProsodyOptions is True.
  This allows for changing rate, volume and pitch after initialization.
  
  @param defaultLanguage: language with locale, installed by OneCore (e.g. 'en_US')
  @param availableLanguages: languages with locale, installed by OneCore (e.g. 'zh_HK', 'en_US')

### `_OcSsmlConverter._convertProsody`(self, command, attr, default, base=None)  *(line 80)*

### `_OcSsmlConverter.convertRateCommand`(self, command)  *(line 92)*

### `_OcSsmlConverter.convertPitchCommand`(self, command)  *(line 95)*

### `_OcSsmlConverter.convertVolumeCommand`(self, command)  *(line 98)*

### `_OcSsmlConverter.convertCharacterModeCommand`(self, command)  *(line 101)*

### `_OcSsmlConverter.convertLangChangeCommand`(self, command: LangChangeCommand) -> Optional[speechXml.SetAttrCommand]  *(line 106)*

### class `_OcPreAPI5SsmlConverter`(_OcSsmlConverter)  *(line 124)*

### `_OcPreAPI5SsmlConverter.__init__`(self, defaultLanguage: str, availableLanguages: Set[str], rate: float, pitch: float, volume: float)  *(line 125)*

  Used for older OneCore installations (OneCore API < 5),
  where supportsProsodyOptions is False.
  This means we must initially set a good default for rate, volume and pitch,
  as this can't be changed after initialization.
  
  @param defaultLanguage: language with locale, installed by OneCore (e.g. 'en_US')
  @param availableLanguages: languages with locale, installed by OneCore (e.g. 'zh_HK', 'en_US')
  @param rate: from 0-100
  @param pitch: from 0-100
  @param volume: from 0-100

### `_OcPreAPI5SsmlConverter.generateBalancerCommands`(self, speechSequence: SpeechSequence) -> Generator[Any, None, None]  *(line 150)*

### `_OcPreAPI5SsmlConverter.convertRateCommand`(self, command)  *(line 162)*

### `_OcPreAPI5SsmlConverter.convertPitchCommand`(self, command)  *(line 165)*

### `_OcPreAPI5SsmlConverter.convertVolumeCommand`(self, command)  *(line 168)*

### class `OneCoreSynthDriver`(SynthDriver)  *(line 172)*

### @classmethod `OneCoreSynthDriver.check`(cls)  *(line 196)*

### `OneCoreSynthDriver._get_supportsProsodyOptions`(self)  *(line 200)*

### `OneCoreSynthDriver._get_supportedSettings`(self)  *(line 204)*

### `OneCoreSynthDriver.__init__`(self)  *(line 219)*

### `OneCoreSynthDriver._maybeInitPlayer`(self, wav)  *(line 254)*

  Initialize audio playback based on the wave header provided by the synthesizer.
  If the sampling rate has not changed, the existing player is used.
  Otherwise, a new one is created with the appropriate parameters.

### `OneCoreSynthDriver.terminate`(self)  *(line 274)*

### `OneCoreSynthDriver.cancel`(self)  *(line 285)*

### `OneCoreSynthDriver.speak`(self, speechSequence: SpeechSequence) -> None  *(line 299)*

### `OneCoreSynthDriver._queueSpeech`(self, item: str) -> None  *(line 319)*

### @classmethod `OneCoreSynthDriver._percentToParam`(cls, percent, min, max)  *(line 326)*

  Overrides SynthDriver._percentToParam to return floating point parameter values.

### `OneCoreSynthDriver._get_pitch`(self)  *(line 330)*

### `OneCoreSynthDriver._set_pitch`(self, pitch)  *(line 336)*

### `OneCoreSynthDriver._get_volume`(self) -> int  *(line 343)*

### `OneCoreSynthDriver._set_volume`(self, volume: int)  *(line 349)*

### `OneCoreSynthDriver._get_rate`(self)  *(line 356)*

### `OneCoreSynthDriver._set_rate`(self, rate)  *(line 363)*

### `OneCoreSynthDriver._get_rateBoost`(self)  *(line 373)*

### `OneCoreSynthDriver._set_rateBoost`(self, enable)  *(line 376)*

### `OneCoreSynthDriver._processQueue`(self)  *(line 385)*

### `OneCoreSynthDriver._handleSpeechFailure`(self)  *(line 432)*

### `OneCoreSynthDriver._callback`(self, bytes, len, markers)  *(line 444)*

### `OneCoreSynthDriver._getVoiceInfoFromOnecoreVoiceString`(self, voiceStr)  *(line 494)*

  Produces an NVDA VoiceInfo object representing the given voice string from Onecore speech.

### `OneCoreSynthDriver._getAvailableVoices`(self)  *(line 503)*

### `OneCoreSynthDriver._isVoiceValid`(self, ID: str) -> bool  *(line 518)*

  Checks that the given voice actually exists and is valid.
  It checks the Registry, and also ensures that its data files actually exist on this machine.
  :param ID: the ID of the requested voice.
  :returns: True if the voice is valid, False otherwise.
  
  OneCore keeps specific registry caches of OneCore for AT applications.
  Installed copies of NVDA have a OneCore cache in:
  `HKEY_CURRENT_USER\Software\Microsoft\Speech_OneCore\Isolated\Ny37kw9G-o42UiJ1z6Qc_sszEKkCNywTlrTOG0QKVB4`.
  The caches contain a subtree which is meant to mirror the path:
  `HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Speech_OneCore\*`.
  
  For example:
  `HKEY_CURRENT_USER\Software\Microsoft\Speech_OneCore\Isolated\Ny37kw9G-o42UiJ1z6Qc_sszEKkCNywTlrTOG0QKVB4\
  HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Speech_OneCore\Voices\Tokens\MSTTS_V110_enUS_MarkM`
  refers to `HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Speech_OneCore\Voices\Tokens\MSTTS_V110_enUS_MarkM`.
  
  Languages which have been used by an installed copy of NVDA,
  but uninstalled from the system are kept in the cache.
  For installed copies of NVDA, OneCore will still attempt to use these languages,
  so we must check if they are valid first.
  For portable copies, the cache is bypassed and `HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Speech_OneCore\`
  is read directly.
  
  For more information, refer to:
  https://github.com/nvaccess/nvda/issues/13732#issuecomment-1149386711

### `OneCoreSynthDriver._get_voice`(self)  *(line 578)*

### `OneCoreSynthDriver._set_voice`(self, id)  *(line 581)*

### `OneCoreSynthDriver._getDefaultVoice`(self, pickAny: bool=True) -> str  *(line 590)*

  Finds the best available voice that can be used as a default.
  It first tries finding a voice with the same language as the user's configured NVDA language
  else one that matches the system language.
  else any voice if pickAny is True.
  Uses the Windows locale (eg en_AU) to provide country information for the voice where possible.
  @returns: the ID of the voice, suitable for passing to self.voice for setting.

### `OneCoreSynthDriver.pause`(self, switch)  *(line 626)*

### class `VoiceUnsupportedError`(RuntimeError)  *(line 635)*

---

## `synthDrivers/sapi5.py`

### class `_SPAudioState`(IntEnum)  *(line 69)*

### class `SpeechVoiceSpeakFlags`(IntEnum)  *(line 77)*

### class `SpeechVoiceEvents`(IntEnum)  *(line 84)*

### class `_SpeakRequest`(NamedTuple)  *(line 91)*

### `__getattr__`(attrName: str) -> Any  *(line 120)*

  Module level `__getattr__` used to preserve backward compatibility.

### class `_SPEventLParamType`(IntEnum)  *(line 131)*

### class `_SPEventEnum`(IntEnum)  *(line 140)*

### class `_SapiEvent`(SPEVENT)  *(line 165)*

  Enhanced version of the SPEVENT structure that supports freeing lParam data automatically.

### `_SapiEvent.clear`(self) -> None  *(line 168)*

  Clear and free related data.

### `_SapiEvent.__del__`(self)  *(line 176)*

### @staticmethod `_SapiEvent.copy`(dst: SPEVENT, src: SPEVENT) -> None  *(line 180)*

### `_SapiEvent.copyTo`(self, dst: SPEVENT) -> None  *(line 199)*

### `_SapiEvent.copyFrom`(self, src: SPEVENT) -> None  *(line 202)*

### `_SapiEvent.getFrom`(self, eventSource: _Pointer[ISpEventSource]) -> bool  *(line 205)*

  Get one event from the event source and store it in this object.
  Return False if there is no event.

### @staticmethod `_SapiEvent.enumerateFrom`(eventSource: _Pointer[ISpEventSource]) -> Generator['_SapiEvent', None, None]  *(line 216)*

  Enumerate all events in the event source.

### `_SapiEvent.getString`(self) -> str  *(line 224)*

  Get the string parameter stored in lParam.

### class `SynthDriverAudioStream`(COMObject)  *(line 231)*

  Implements ISpAudio, ISpEventSource, and ISpEventSink.
  ISpAudio extends IStream which is used to stream in audio data,
  and also has `SetFormat` to tell the audio object what wave format is preferred.
  Should be set as the audio output via `ISpAudio::SetOutput`.
  ISpEventSource and ISpEventSink are also required for `SetOutput` to work,
  although we only need to pass the event from the sink to the source,
  and leave most functions unimplemented.

### `SynthDriverAudioStream.__init__`(self, synthRef: weakref.ReferenceType['SynthDriver'])  *(line 244)*

### `SynthDriverAudioStream.ISequentialStream_RemoteWrite`(self, this: int, pv: _Pointer[c_ubyte], cb: int, pcbWritten: _Pointer[c_ulong]) -> int  *(line 252)*

  This is called when SAPI wants to write (output) a wave data chunk.
  
  :param pv: A pointer to the first wave data byte.
  :param cb: The number of bytes to write.
  :param pcbWritten: A pointer to a variable where the actual number of bytes written will be stored.
          Can be null.
  :returns: HRESULT code.

### `SynthDriverAudioStream.IStream_RemoteSeek`(self, this: int, dlibMove: _LARGE_INTEGER, dwOrigin: int, plibNewPosition: _Pointer[_ULARGE_INTEGER]) -> int  *(line 292)*

  This is called when SAPI wants to get the current stream position.
  Seeking to another position is not supported.
  
  :param dlibMove: The displacement to be added to the location indicated by the dwOrigin parameter.
          Only 0 is supported.
  :param dwOrigin: The origin for the displacement specified in dlibMove.
          Only 1 (STREAM_SEEK_CUR) is supported.
  :param plibNewPosition: A pointer to a ULARGE_INTEGER where the current stream position will be stored.
          Can be null.
  :returns: HRESULT code.

### `SynthDriverAudioStream.IStream_Commit`(self, grfCommitFlags: int)  *(line 317)*

  This is called when MSSP wants to flush the written data.
  Does nothing.

### `SynthDriverAudioStream.ISpStreamFormat_GetFormat`(self, pguidFormatId: _Pointer[GUID]) -> _Pointer[WAVEFORMATEX]  *(line 322)*

  This is called when SAPI wants to get the current wave format.
  
  :param pguidFormatId: Receives the current format GUID.
          Should be SPDFID_WaveFormatEx for WAVEFORMATEX formats.
          This parameter is incorrectly marked as "in" by comtypes,
          but is actually an out parameter.
  :returns: Pointer to a WAVEFORMATEX structure that is allocated by CoTaskMemAlloc.

### `SynthDriverAudioStream.ISpAudio_SetState`(self, NewState: SPAUDIOSTATE, ullReserved: int) -> None  *(line 339)*

  This is called when the audio state changes, for example, when the audio stream is paused or closed.

### `SynthDriverAudioStream.ISpAudio_SetFormat`(self, rguidFmtId: _Pointer[GUID], pWaveFormatEx: _Pointer[WAVEFORMATEX])  *(line 343)*

  This is called when SAPI wants to tell us what wave format we should use.
  We can get the best format for the specific voice here.
  
  :param rguidFmtId: Format GUID. Should be SPDFID_WaveFormatEx.
  :param pWaveFormatEx: Pointer to a WAVEFORMATEX structure.
          We should copy the data to our own structure to keep the format data.

### @staticmethod `SynthDriverAudioStream._writeDefaultFormat`(wfx: WAVEFORMATEX) -> None  *(line 365)*

  Put the default format into wfx. The default format is 48kHz 16-bit stereo.

### `SynthDriverAudioStream.ISpAudio_GetDefaultFormat`(self) -> tuple[GUID, _Pointer[WAVEFORMATEX]]  *(line 375)*

  Returns the default format that is guaranteed to work on this audio object.
  
  :returns: A tuple of a GUID, which should always be SPDFID_WaveFormatEx,
          and a pointer to a WAVEFORMATEX structure, allocated by CoTaskMemAlloc.

### `SynthDriverAudioStream.ISpAudio_EventHandle`(self) -> int  *(line 386)*

### `SynthDriverAudioStream.ISpNotifySource_SetNotifySink`(self, pNotifySink: _Pointer[ISpNotifySink]) -> None  *(line 389)*

  SAPI will pass in an ISpNotifySink pointer to be notified of events.
  We just need to pass the events we have received back to the sink.

### `SynthDriverAudioStream.ISpNotifySource_GetNotifyEventHandle`(self) -> int  *(line 394)*

### `SynthDriverAudioStream.ISpEventSource_SetInterest`(self, ullEventInterest: int, ullQueuedInterest: int) -> None  *(line 397)*

  SAPI uses this to tell us the types of events it is interested in.
  We just ignore this and assume that it's interested in everything.
  
  :param ullEventInterest: Types of events that should cause ISpNotifySink::Notify() to be called.
  :param ullQueuedInterest: Types of events than should be stored in the event queue
          and can be retrieved later with ISpEventSource::GetEvents().

### `SynthDriverAudioStream.ISpEventSource_GetEvents`(self, this: int, ulCount: int, pEventArray: _Pointer[SPEVENT], pulFetched: _Pointer[c_ulong]) -> None  *(line 406)*

  Send the events that was passed in via AddEvents back to the event sink.
  Events that has been retrieved will be removed.
  
  :param ulCount: The maximum number of events pEventArray can hold.
  :param pEventArray: Pointer to an array of SPEVENT structures
          that is used to receive the event data.
  :param pulFetched: Used to store the actual number of events fetched.
          This pointer can be NULL when ulCount is 1.

### `SynthDriverAudioStream.ISpEventSink_AddEvents`(self, pEventArray: _Pointer[SPEVENT], ulCount: int) -> None  *(line 427)*

  SAPI will send all events to our ISpAudio implementation,
  such as StartStream events and Bookmark events.
  To let the ISpVoice client get notified as well, we should store those events,
  then pass the events to the ISpNotifySink we got earlier.
  
  :param pEventArray: Pointer to an array of SPEVENT structures.
  :param ulCount: Number of events.

### class `SapiSink`(COMObject)  *(line 445)*

  Implements ISpNotifySink to handle SAPI event notifications.
  Should be passed to ISpNotifySource::SetNotifySink().
  Notifications will be sent on the original thread,
  instead of being routed to the main thread.

### `SapiSink.__init__`(self, synthRef: weakref.ReferenceType['SynthDriver'])  *(line 455)*

### `SapiSink.ISpNotifySink_Notify`(self)  *(line 458)*

  This is called when there's a new event notification.
  Queued events will be retrieved.

### `SapiSink.StartStream`(self, streamNum: int, pos: int)  *(line 480)*

### `SapiSink.Bookmark`(self, streamNum: int, pos: int, bookmark: str, bookmarkId: int)  *(line 489)*

### `SapiSink.EndStream`(self, streamNum: int, pos: int)  *(line 501)*

### `SapiSink.onIndexReached`(self, streamNum: int, index: int)  *(line 518)*

### class `SynthDriver`(SynthDriver)  *(line 532)*

### @classmethod `SynthDriver.check`(cls)  *(line 560)*

### `SynthDriver.__init__`(self, _defaultVoiceToken=None)  *(line 573)*

  @param _defaultVoiceToken: an optional sapi voice token which should be used as the default voice (only useful for subclasses)
  @type _defaultVoiceToken: ISpeechObjectToken

### `SynthDriver._stopThread`(self) -> None  *(line 599)*

  Stops the WASAPI speak thread (if it's running) and waits for the thread to quit.

### `SynthDriver.terminate`(self)  *(line 612)*

### `SynthDriver._getAvailableVoices`(self)  *(line 619)*

### `SynthDriver._getVoiceTokens`(self)  *(line 637)*

  Provides a collection of sapi5 voice tokens. Can be overridden by subclasses if tokens should be looked for in some other registry location.

### `SynthDriver._get_rate`(self)  *(line 641)*

### `SynthDriver._get_rateBoost`(self)  *(line 644)*

### `SynthDriver._get_pitch`(self)  *(line 647)*

### `SynthDriver._get_volume`(self) -> int  *(line 650)*

### `SynthDriver._get_voice`(self)  *(line 653)*

### `SynthDriver._get_useWasapi`(self) -> bool  *(line 656)*

### `SynthDriver._get_lastIndex`(self)  *(line 659)*

### @classmethod `SynthDriver._percentToParam`(cls, percent, min, max) -> float  *(line 667)*

  Overrides SynthDriver._percentToParam to return floating point parameter values.

### `SynthDriver._percentToRate`(self, percent)  *(line 671)*

### `SynthDriver._set_rate`(self, rate)  *(line 674)*

### `SynthDriver._set_rateBoost`(self, enable: bool)  *(line 689)*

### `SynthDriver._set_pitch`(self, value)  *(line 696)*

### `SynthDriver._set_volume`(self, value)  *(line 700)*

### `SynthDriver._initWasapiAudio`(self)  *(line 704)*

### `SynthDriver._initLegacyAudio`(self)  *(line 723)*

### `SynthDriver._initTts`(self, voice: str | None=None)  *(line 734)*

### `SynthDriver._set_voice`(self, value)  *(line 763)*

### `SynthDriver._set_useWasapi`(self, value: bool)  *(line 779)*

### `SynthDriver._percentToPitch`(self, percent)  *(line 785)*

### `SynthDriver._convertPhoneme`(self, ipa)  *(line 793)*

### `SynthDriver._requestsAvailable`(self) -> bool  *(line 813)*

### `SynthDriver._requestCompleted`(self) -> bool  *(line 816)*

### `SynthDriver._onEndStream`(self) -> None  *(line 819)*

  Common handling when a speech stream ends.

### `SynthDriver._speakThread`(self)  *(line 833)*

  Thread that processes speech when WASAPI is enabled.

### `SynthDriver.speak`(self, speechSequence)  *(line 884)*

### `SynthDriver._speak_legacy`(self, text: str, flags: int) -> int  *(line 1001)*

  Legacy way of calling SpVoice.Speak that uses a temporary audio ducker.

### `SynthDriver.cancel`(self)  *(line 1043)*

### `SynthDriver.pause`(self, switch: bool)  *(line 1063)*

### `SynthDriver.__getattr__`(self, attrName: str) -> Any  *(line 1087)*

  This is used to reserve backward compatibility.

---

## `speech/__init__.py`

### `initialize`()  *(line 157)*

  Loads and sets the synth driver configured in nvda.ini.
  Initializes the state of speech and initializes the sayAllHandler

### `terminate`()  *(line 174)*

---

## `speech/commands.py`

> Commands that can be embedded in a speech sequence for changing synth parameters, playing sounds or running
>  other callbacks.

### class `SpeechCommand`(object)  *(line 43)*

  The base class for objects that can be inserted between strings of text to perform actions,
  change voice parameters, etc.
  
  Note: Some of these commands are processed by NVDA and are not directly passed to synth drivers.
  synth drivers will only receive commands derived from L{SynthCommand}.

### class `_CancellableSpeechCommand`(SpeechCommand)  *(line 52)*

  A command that allows cancelling the utterance that contains it.
  Support currently experimental and may be subject to change.

### `_CancellableSpeechCommand.__init__`(self, reportDevInfo=False)  *(line 58)*

  @param reportDevInfo: If true, developer info is reported for repr implementation.

### `_CancellableSpeechCommand._checkIfValid`(self)  *(line 70)*

### `_CancellableSpeechCommand._getDevInfo`(self)  *(line 74)*

### `_CancellableSpeechCommand._checkIfCancelled`(self)  *(line 77)*

### @property `_CancellableSpeechCommand.isCancelled`(self)  *(line 85)*

### `_CancellableSpeechCommand.cancelUtterance`(self)  *(line 88)*

### `_CancellableSpeechCommand._getFormattedDevInfo`(self)  *(line 91)*

### `_CancellableSpeechCommand.__repr__`(self)  *(line 103)*

### class `SynthCommand`(SpeechCommand)  *(line 112)*

  Commands that can be passed to synth drivers.

### class `IndexCommand`(SynthCommand)  *(line 116)*

  Marks this point in the speech with an index.
  When speech reaches this index, the synthesizer notifies NVDA,
  thus allowing NVDA to perform actions at specific points in the speech;
  e.g. synchronizing the cursor, beeping or playing a sound.
  Callers should not use this directly.
  Instead, use one of the subclasses of L{BaseCallbackCommand}.
  NVDA handles the indexing and dispatches callbacks as appropriate.

### `IndexCommand.__init__`(self, index)  *(line 126)*

  @param index: the value of this index
  @type index: integer

### `IndexCommand.__repr__`(self)  *(line 135)*

### `IndexCommand.__eq__`(self, __o: object) -> bool  *(line 138)*

### class `SynthParamCommand`(SynthCommand)  *(line 146)*

  A synth command which changes a parameter for subsequent speech.

### class `CharacterModeCommand`(SynthParamCommand)  *(line 156)*

  Turns character mode on and off for speech synths.

### `CharacterModeCommand.__init__`(self, state)  *(line 159)*

  @param state: if true character mode is on, if false its turned off.
  @type state: boolean

### `CharacterModeCommand.__repr__`(self)  *(line 169)*

### `CharacterModeCommand.__eq__`(self, __o: object) -> bool  *(line 172)*

### class `LangChangeCommand`(SynthParamCommand)  *(line 180)*

  A command to switch the language within speech.

### `LangChangeCommand.__init__`(self, lang: str | None)  *(line 183)*

  :param lang: The language to switch to: If None then the NVDA locale will be used.

### `LangChangeCommand.__repr__`(self)  *(line 190)*

### `LangChangeCommand.__eq__`(self, __o: object) -> bool  *(line 193)*

### class `BreakCommand`(SynthCommand)  *(line 203)*

  Insert a break between words.

### `BreakCommand.__init__`(self, time: int=0)  *(line 206)*

  @param time: The duration of the pause to be inserted in milliseconds.

### `BreakCommand.__repr__`(self)  *(line 213)*

### `BreakCommand.__eq__`(self, __o: object) -> bool  *(line 216)*

### class `EndUtteranceCommand`(SpeechCommand)  *(line 224)*

  End the current utterance at this point in the speech.
  Any text after this will be sent to the synthesizer as a separate utterance.

### `EndUtteranceCommand.__repr__`(self)  *(line 229)*

### class `SuppressUnicodeNormalizationCommand`(SpeechCommand)  *(line 233)*

  Suppresses Unicode normalization at a point in a speech sequence.
  For any text after this, Unicode normalization will be suppressed when state is True.
  When state is False, original behavior of normalization will be restored.
  This command is a no-op when normalization is disabled.

### `SuppressUnicodeNormalizationCommand.__init__`(self, state: bool=True)  *(line 242)*

  :param state: Suppress normalization if True, don't suppress when False

### `SuppressUnicodeNormalizationCommand.__repr__`(self)  *(line 248)*

### class `BaseProsodyCommand`(SynthParamCommand)  *(line 252)*

  Base class for commands which change voice prosody; i.e. pitch, rate, etc.
  The change to the setting is specified using either an offset or a multiplier, but not both.
  The L{offset} and L{multiplier} properties convert between the two if necessary.
  To return to the default value, specify neither.
  This base class should not be instantiated directly.

### `BaseProsodyCommand.__init__`(self, offset=0, multiplier=1)  *(line 263)*

  Constructor.
  Either of C{offset} or C{multiplier} may be specified, but not both.
  @param offset: The amount by which to increase/decrease the user configured setting;
          e.g. 30 increases by 30, -10 decreases by 10, 0 returns to the configured setting.
  @type offset: int
  @param multiplier: The number by which to multiply the user configured setting;
          e.g. 0.5 is half, 1 returns to the configured setting.
  @param multiplier: int/float

### @property `BaseProsodyCommand.defaultValue`(self)  *(line 280)*

  The default value for the setting as configured by the user.

### @property `BaseProsodyCommand.multiplier`(self)  *(line 287)*

  The number by which to multiply the default value.

### @property `BaseProsodyCommand.offset`(self)  *(line 301)*

  The amount by which to increase/decrease the default value.

### @property `BaseProsodyCommand.newValue`(self)  *(line 315)*

  The new absolute value after the offset or multiplier is applied to the default value.

### `BaseProsodyCommand.__repr__`(self)  *(line 326)*

### `BaseProsodyCommand.__eq__`(self, __o: object) -> bool  *(line 338)*

### `BaseProsodyCommand.__ne__`(self, __o) -> bool  *(line 345)*

### class `PitchCommand`(BaseProsodyCommand)  *(line 353)*

  Change the pitch of the voice.

### class `VolumeCommand`(BaseProsodyCommand)  *(line 359)*

  Change the volume of the voice.

### class `RateCommand`(BaseProsodyCommand)  *(line 365)*

  Change the rate of the voice.

### class `PhonemeCommand`(SynthCommand)  *(line 371)*

  Insert a specific pronunciation.
  This command accepts Unicode International Phonetic Alphabet (IPA) characters.
  Note that this is not well supported by synthesizers.

### `PhonemeCommand.__init__`(self, ipa, text=None)  *(line 377)*

  @param ipa: Unicode IPA characters.
  @type ipa: str
  @param text: Text to speak if the synthesizer does not support
          some or all of the specified IPA characters,
          C{None} to ignore this command instead.
  @type text: str

### `PhonemeCommand.__repr__`(self)  *(line 389)*

### `PhonemeCommand.__eq__`(self, __o: object) -> bool  *(line 395)*

### class `BaseCallbackCommand`(SpeechCommand)  *(line 403)*

  Base class for commands which cause a function to be called when speech reaches them.
  This class should not be instantiated directly.
  It is designed to be subclassed to provide specific functionality;
  e.g. L{BeepCommand}.
  To supply a generic function to run, use L{CallbackCommand}.
  This command is never passed to synth drivers.

### `BaseCallbackCommand.run`(self)  *(line 413)*

  Code to run when speech reaches this command.
  This method is executed in NVDA's main thread,
  therefore must return as soon as practically possible,
  otherwise it will block production of further speech and or other functionality in NVDA.

### class `CallbackCommand`(BaseCallbackCommand)  *(line 421)*

  Call a function when speech reaches this point.
  Note that  the provided function is executed in NVDA's main thread,
          therefore must return as soon as practically possible,
          otherwise it will block production of further speech and or other functionality in NVDA.

### `CallbackCommand.__init__`(self, callback, name: Optional[str]=None)  *(line 429)*

### `CallbackCommand.run`(self, *args, **kwargs)  *(line 433)*

### `CallbackCommand.__repr__`(self)  *(line 436)*

### class `BeepCommand`(BaseCallbackCommand)  *(line 442)*

  Produce a beep.

### `BeepCommand.__init__`(self, hz, length, left=50, right=50)  *(line 445)*

### `BeepCommand.run`(self)  *(line 451)*

### `BeepCommand.__repr__`(self)  *(line 462)*

### class `WaveFileCommand`(BaseCallbackCommand)  *(line 471)*

  Play a wave file.

### `WaveFileCommand.__init__`(self, fileName)  *(line 474)*

### `WaveFileCommand.run`(self)  *(line 477)*

### `WaveFileCommand.__repr__`(self)  *(line 482)*

### class `ConfigProfileTriggerCommand`(SpeechCommand)  *(line 486)*

  Applies (or stops applying) a configuration profile trigger to subsequent speech.

### `ConfigProfileTriggerCommand.__init__`(self, trigger, enter=True)  *(line 489)*

  @param trigger: The configuration profile trigger.
  @type trigger: L{config.ProfileTrigger}
  @param enter: C{True} to apply the trigger, C{False} to stop applying it.
  @type enter: bool

---

## `speech/extensions.py`

> Extension points for speech.

---

## `speech/manager.py`

### `_shouldCancelExpiredFocusEvents`()  *(line 38)*

### `_shouldDoSpeechManagerLogging`()  *(line 43)*

### `_speechManagerDebug`(msg, *args, **kwargs) -> None  *(line 47)*

  Log 'msg % args' with severity 'DEBUG' if speech manager logging is enabled.
  'SpeechManager-' is prefixed to all messages to make searching the log easier.

### `_speechManagerUnitTest`(msg, *args, **kwargs) -> None  *(line 61)*

  Log 'msg % args' with severity 'DEBUG' if .
  'SpeechManUnitTest-' is prefixed to all messages to make searching the log easier.
  When

### class `ParamChangeTracker`(object)  *(line 87)*

  Keeps track of commands which change parameters from their defaults.
  This is useful when an utterance needs to be split.
  As you are processing a sequence,
  you update the tracker with a parameter change using the L{update} method.
  When you split the utterance, you use the L{getChanged} method to get
  the parameters which have been changed from their defaults.

### `ParamChangeTracker.__init__`(self)  *(line 96)*

### `ParamChangeTracker.update`(self, command)  *(line 99)*

  Update the tracker with a parameter change.
  @param command: The parameter change command.
  @type command: L{SynthParamCommand}

### `ParamChangeTracker.getChanged`(self)  *(line 111)*

  Get the commands for the parameters which have been changed from their defaults.
  @return: List of parameter change commands.
  @type: list of L{SynthParamCommand}

### class `_ManagerPriorityQueue`(object)  *(line 119)*

  A speech queue for a specific priority.
  This is intended for internal use by L{_SpeechManager} only.
  Each priority has a separate queue.
  It holds the pending speech sequences to be spoken,
  as well as other information necessary to restore state when this queue
  is preempted by a higher priority queue.

### `_ManagerPriorityQueue.__init__`(self, priority: Spri)  *(line 128)*

### class `SpeechManager`(object)  *(line 140)*

  Manages queuing of speech utterances, calling callbacks at desired points in the speech, profile switching, prioritization, etc.
  This is intended for internal use only.
  It is used by higher level functions such as L{speak}.
  
  The high level flow of control is as follows:
  1. A speech sequence is queued with L{speak}, which in turn calls L{_queueSpeechSequence}.
  2. L{_processSpeechSequence} is called to normalize, process and split the input sequence.
          It converts callbacks to indexes.
          All indexing is assigned and managed by this class.
          It maps any indexes to their corresponding callbacks.
          It splits the sequence at indexes so we easily know what has completed speaking.
          If there are end utterance commands, the sequence is split at that point.
          We ensure there is an index at the end of all utterances so we know when they've finished speaking.
          We ensure any config profile trigger commands are preceded by an utterance end.
          Parameter changes are re-applied after utterance breaks.
          We ensure any entered profile triggers are exited at the very end.
  3. L{_queueSpeechSequence} places these processed sequences in the queue
          for the priority specified by the caller in step 1.
          There is a separate queue for each priority.
  4. L{_pushNextSpeech} is called to begin pushing speech.
          It looks for the highest priority queue with pending speech.
          Because there's no other speech queued, that'll be the queue we just touched.
  5. If the input begins with a profile switch, it is applied immediately.
  6. L{_buildNextUtterance} is called to build a full utterance and it is sent to the synth.
  7. For every index reached, L{_handleIndex} is called.
          The completed sequence is removed from L{_pendingSequences}.
          If there is an associated callback, it is run.
          If the index marks the end of an utterance, L{_pushNextSpeech} is called to push more speech.
  8. If there is another utterance before a profile switch, it is built and sent as per steps 6 and 7.
  9. In L{_pushNextSpeech}, if a profile switch is next, we wait for the synth to finish speaking before pushing more.
          This is because we don't want to start speaking too early with a different synth.
          L{_handleDoneSpeaking} is called when the synth finishes speaking.
          It pushes more speech, which includes applying the profile switch.
  10. The flow then repeats from step 6 onwards until there are no more pending sequences.
  11. If another sequence is queued via L{speak} during speech,
          it is processed and queued as per steps 2 and 3.
  12. If this is the first utterance at priority now, speech is interrupted
          and L{_pushNextSpeech} is called.
          Otherwise, L{_pushNextSpeech} is called when the current utterance completes
          as per step 7.
  13. When L{_pushNextSpeech} is next called, it looks for the highest priority queue with pending speech.
          If that priority is different to the priority of the utterance just spoken,
          any relevant profile switches are applied to restore the state for this queue.
  14. If a lower priority utterance was interrupted in the middle,
          L{_buildNextUtterance} applies any parameter changes that applied before the interruption.
  15. The flow then repeats from step 6 onwards until there are no more pending sequences.
  
  Note:
  All of this activity is (and must be) synchronized and serialized on the main thread.

### `SpeechManager.__init__`(self)  *(line 196)*

### `SpeechManager._generateIndexes`(self) -> typing.Generator[_IndexT, None, None]  *(line 206)*

  Generator of index numbers.
  We don't want to reuse index numbers too quickly,
  as there can be race conditions when cancelling speech which might result
  in an index from a previous utterance being treated as belonging to the current utterance.
  However, we don't want the counter increasing indefinitely,
  as some synths might not be able to handle huge numbers.
  Therefore, we use a counter which starts at 1, counts up to L{MAX_INDEX},
  wraps back to 1 and continues cycling thus.
  This maximum is arbitrary, but
  it's small enough that any synth should be able to handle it
  and large enough that previous indexes won't reasonably get reused
  in the same or previous utterance.

### `SpeechManager._reset`(self)  *(line 224)*

### `SpeechManager._synthStillSpeaking`(self) -> bool  *(line 240)*

### `SpeechManager._hasNoMoreSpeech`(self)  *(line 243)*

### `SpeechManager.speak`(self, speechSequence: SpeechSequence, priority: Spri)  *(line 246)*

### `SpeechManager._queueSpeechSequence`(self, inSeq: SpeechSequence, priority: Spri) -> bool  *(line 273)*

  @return: Whether to interrupt speech.

### `SpeechManager._ensureEndUtterance`(self, seq: SpeechSequence, outSeqs, paramsToReplay, paramTracker)  *(line 297)*

  We split at EndUtteranceCommands so the ends of utterances are easily found.
  This function ensures the given sequence ends with an EndUtterance command,
  Ensures that the sequence also includes an index command at the end,
  It places the complete sequence in outSeqs,
  It clears the given sequence list ready to build a new one,
  And clears the paramsToReplay list
  and refills it with any params that need to be repeated if a new sequence is going to be built.

### `SpeechManager._processSpeechSequence`(self, inSeq: SpeechSequence)  *(line 328)*

### `SpeechManager._pushNextSpeech`(self, doneSpeaking: bool)  *(line 386)*

### `SpeechManager._getNextPriority`(self)  *(line 442)*

  Get the highest priority queue containing pending speech.

### `SpeechManager._buildNextUtterance`(self)  *(line 452)*

  Since an utterance might be split over several sequences,
  build a complete utterance to pass to the synth.

### `SpeechManager._checkForCancellations`(self, utterance: SpeechSequence) -> bool  *(line 485)*

  Checks utterance to ensure it is not cancelled (via a _CancellableSpeechCommand).
  Because synthesizers do not expect CancellableSpeechCommands, they are removed from the utterance.
  :arg utterance: The utterance to check for cancellations. Modified in place, CancellableSpeechCommands are
  removed.
  :return True if sequence is still valid, else False

### @classmethod `SpeechManager._isIndexABeforeIndexB`(cls, indexA: _IndexT, indexB: _IndexT) -> bool  *(line 520)*

  Was indexB created before indexB
  Because indexes wrap after MAX_INDEX, custom logic is needed to compare relative positions.
  The boundary for considering a wrapped value as before another value is based on the distance
  between the indexes. If the distance is greater than half the available index space it is no longer
  before.
  @return True if indexA was created before indexB, else False

### @classmethod `SpeechManager._isIndexAAfterIndexB`(cls, indexA: _IndexT, indexB: _IndexT) -> bool  *(line 540)*

### `SpeechManager._getMostRecentlyCancelledUtterance`(self) -> Optional[_IndexT]  *(line 543)*

### `SpeechManager.removeCancelledSpeechCommands`(self)  *(line 565)*

### `SpeechManager._doRemoveCancelledSpeechCommands`(self)  *(line 569)*

### `SpeechManager._getUtteranceIndex`(self, utterance: SpeechSequence)  *(line 586)*

### `SpeechManager._onSynthIndexReached`(self, synth=None, index=None)  *(line 594)*

### `SpeechManager._removeCompletedFromQueue`(self, index: int) -> Tuple[bool, bool]  *(line 603)*

  Removes completed speech sequences from the queue.
  @param index: The index just reached indicating a completed sequence.
  @return: Tuple of (valid, endOfUtterance),
          where valid indicates whether the index was valid and
          endOfUtterance indicates whether this sequence was the end of the current utterance.
  @rtype: (bool, bool)

### `SpeechManager._handleIndex`(self, index: int)  *(line 674)*

### `SpeechManager._onSynthDoneSpeaking`(self, synth: Optional[synthDriverHandler.SynthDriver]=None)  *(line 721)*

### `SpeechManager._handleDoneSpeaking`(self)  *(line 728)*

### `SpeechManager._switchProfile`(self)  *(line 736)*

### `SpeechManager._exitProfileTriggers`(self, triggers)  *(line 756)*

### `SpeechManager._restoreProfileTriggers`(self, triggers)  *(line 764)*

### `SpeechManager.cancel`(self)  *(line 772)*

---

## `speech/priorities.py`

> Speech priority enumeration.

### class `SpeechPriority`(IntEnum)  *(line 12)*

  Facilitates the ability to prioritize speech.
  Note: This enum has its counterpart in the NVDAController RPC interface (nvdaController.idl).
  Additions to this enum should also be reflected in nvdaController.idl.

---

## `speech/sayAll.py`

### class `CURSOR`(IntEnum)  *(line 38)*

### `initialize`(speakFunc: Callable[[SpeechSequence], None], speakObject: 'speakObject', getTextInfoSpeech: 'getTextInfoSpeech', SpeakTextInfoState: 'SpeakTextInfoState')  *(line 47)*

### class `_SayAllHandler`  *(line 63)*

### `_SayAllHandler.__init__`(self, speechWithoutPausesInstance: SpeechWithoutPauses, speakObject: 'speakObject', getTextInfoSpeech: 'getTextInfoSpeech', SpeakTextInfoState: 'SpeakTextInfoState')  *(line 64)*

### `_SayAllHandler.stop`(self)  *(line 80)*

  Stops any active objects reader and resets the SayAllHandler's SpeechWithoutPauses instance

### `_SayAllHandler.isRunning`(self)  *(line 89)*

  Determine whether say all is currently running.
  @return: C{True} if say all is currently running, C{False} if not.
  @rtype: bool

### `_SayAllHandler.readObjects`(self, obj: 'NVDAObjects.NVDAObject', startedFromScript: bool | None=False)  *(line 96)*

  Start or restarts the object reader.
  :param obj: the object to be read
  :param startedFromScript: whether the current say all action was initially started from a script; use None to keep
          the last value unmodified, e.g. when the say all action is resumed during skim reading.

### `_SayAllHandler.readText`(self, cursor: CURSOR, startPos: Optional[textInfos.TextInfo]=None, nextLineFunc: Optional[Callable[[textInfos.TextInfo], textInfos.TextInfo]]=None, shouldUpdateCaret: bool=True, startedFromScript: bool | None=False) -> None  *(line 108)*

  Start or restarts the reader
  :param cursor: the type of cursor used for say all
  :param startPos: start position (only used for table say all)
  :param nextLineFunc: function called to read the next line (only used for table say all)
  :param shouldUpdateCaret: whether the caret should be updated during say all (only used for table say all)
  :param startedFromScript: whether the current say all action was initially started from a script; use None to keep
          the last value unmodified, e.g. when the say all action is resumed during skim reading.

### class `_Reader`(garbageHandler.TrackedObject)  *(line 143)*

  Base class for readers in say all.

### `_Reader.__init__`(self, handler: _SayAllHandler)  *(line 146)*

### `_Reader.next`(self)  *(line 151)*

### `_Reader.stop`(self)  *(line 154)*

  Stops the reader.

### `_Reader.__del__`(self)  *(line 158)*

### class `_ObjectsReader`(_Reader)  *(line 162)*

  Manages continuous reading of objects.

### `_ObjectsReader.__init__`(self, handler: _SayAllHandler, root: 'NVDAObjects.NVDAObject')  *(line 165)*

### `_ObjectsReader.walk`(self, obj: 'NVDAObjects.NVDAObject')  *(line 170)*

### `_ObjectsReader.next`(self)  *(line 178)*

### `_ObjectsReader.stop`(self)  *(line 201)*

### class `_TextReader`(_Reader)  *(line 208)*

  Manages continuous reading of text.
  This is intended for internal use only.
  
  The high level flow of control is as follows:
  1. The constructor sets things up.
  2. L{nextLine} is called to read the first line.
  3. When it speaks a line, L{nextLine} request that L{lineReached} be called
          when we start speaking this line, providing the position and state at this point.
  4. When we start speaking a line, L{lineReached} is called
          and moves the cursor to that line.
  5. L{lineReached} calls L{nextLine}.
  6. If there are more lines, L{nextLine} works as per steps 3 and 4.
  7. Otherwise, if the object doesn't support page turns, we're finished.
  8. If the object does support page turns,
          we request that L{turnPage} be called when speech is finished.
  9. L{turnPage} tries to turn the page.
  10. If there are no more pages, we're finished.
  11. If there is another page, L{turnPage} calls L{nextLine}.

### `_TextReader.__init__`(self, handler: _SayAllHandler)  *(line 231)*

### `_TextReader.getInitialTextInfo`(self) -> textInfos.TextInfo  *(line 243)*

### `_TextReader.updateCaret`(self, updater: textInfos.TextInfo) -> None  *(line 246)*

### `_TextReader.shouldReadInitialPosition`(self) -> bool  *(line 248)*

### `_TextReader.nextLineImpl`(self) -> bool  *(line 251)*

  Advances cursor to the next reading chunk (e.g. paragraph).
  @return: C{True} if advanced successfully, C{False} otherwise.

### `_TextReader.collapseLineImpl`(self) -> bool  *(line 274)*

  Collapses to the end of this line, ready to read the next.
  @return: C{True} if collapsed successfully, C{False} otherwise.

### `_TextReader.next`(self)  *(line 289)*

### `_TextReader.nextLine`(self)  *(line 292)*

### `_TextReader.lineReached`(self, obj, bookmark, state)  *(line 363)*

### `_TextReader.turnPage`(self)  *(line 374)*

### `_TextReader.finish`(self)  *(line 385)*

### `_TextReader.stop`(self)  *(line 400)*

### class `_CaretTextReader`(_TextReader)  *(line 409)*

### `_CaretTextReader.getInitialTextInfo`(self) -> textInfos.TextInfo  *(line 410)*

### `_CaretTextReader.updateCaret`(self, updater: textInfos.TextInfo) -> None  *(line 416)*

### class `_ReviewTextReader`(_TextReader)  *(line 422)*

### `_ReviewTextReader.getInitialTextInfo`(self) -> textInfos.TextInfo  *(line 423)*

### `_ReviewTextReader.updateCaret`(self, updater: textInfos.TextInfo) -> None  *(line 426)*

### class `_TableTextReader`(_CaretTextReader)  *(line 430)*

### `_TableTextReader.__init__`(self, handler: _SayAllHandler, startPos: Optional[textInfos.TextInfo]=None, nextLineFunc: Optional[Callable[[textInfos.TextInfo], textInfos.TextInfo]]=None, shouldUpdateCaret: bool=True)  *(line 431)*

### `_TableTextReader.getInitialTextInfo`(self) -> textInfos.TextInfo  *(line 443)*

### `_TableTextReader.nextLineImpl`(self) -> bool  *(line 446)*

### `_TableTextReader.collapseLineImpl`(self) -> bool  *(line 454)*

### `_TableTextReader.shouldReadInitialPosition`(self) -> bool  *(line 457)*

### `_TableTextReader.updateCaret`(self, updater: textInfos.TextInfo) -> None  *(line 460)*

### class `SayAllProfileTrigger`(config.ProfileTrigger)  *(line 465)*

  A configuration profile trigger for when say all is in progress.

---

## `speech/types.py`

> Types used by speech package.
> Kept here so they can be re-used without having to worry about circular imports.

### `_isDebugForSpeech`() -> bool  *(line 31)*

  Check if debug logging for speech is enabled.

### class `GeneratorWithReturn`(Iterable)  *(line 36)*

  Helper class, used with generator functions to access the 'return' value after there are no more values
  to iterate over.

### `GeneratorWithReturn.__init__`(self, gen: Iterable, defaultReturnValue=None)  *(line 41)*

### `GeneratorWithReturn.__iter__`(self)  *(line 46)*

### `_flattenNestedSequences`(nestedSequences: Union[Iterable[SpeechSequence], GeneratorWithReturn]) -> Generator[SequenceItemT, Any, Optional[bool]]  *(line 51)*

  Turns [[a,b,c],[d,e]] into [a,b,c,d,e]

### `logBadSequenceTypes`(sequence: SpeechIterable, raiseExceptionOnError=False) -> bool  *(line 61)*

  Check if the provided sequence is valid, otherwise log an error (only if speech is
  checked in the "log categories" setting of the advanced settings panel.
  @param sequence: the sequence to check
  @param raiseExceptionOnError: if True, and exception is raised. Useful to help track down the introduction
          of erroneous speechSequence data.
  @return: True if the sequence is valid.

---

## `speech/speech.py`

> High-level functions to speak information.

### class `SpeechMode`(DisplayStringIntEnum)  *(line 96)*

### @property `SpeechMode._displayStringLabels`(self) -> dict[Self, str]  *(line 103)*

### class `SpeechState`  *(line 118)*

### `getState`()  *(line 140)*

### `setSpeechMode`(newMode: SpeechMode)  *(line 144)*

### `_setLastSpeechString`(speechSequence: SpeechSequence, symbolLevel: characterProcessing.SymbolLevel | None, priority: Spri)  *(line 148)*

### `initialize`()  *(line 166)*

### `isBlank`(text)  *(line 184)*

  Determine whether text should be reported as blank.
  @param text: The text in question.
  @type text: str
  @return: C{True} if the text is blank, C{False} if not.
  @rtype: bool

### `processText`(locale: str, text: str, symbolLevel: characterProcessing.SymbolLevel, normalize: bool=False) -> str  *(line 197)*

  Processes text for symbol pronunciation, speech dictionaries and Unicode normalization.
  :param locale: The language the given text is in, passed for symbol pronunciation.
  :param text: The text to process.
  :param symbolLevel: The verbosity level used for symbol pronunciation.
  :param normalize: Whether to apply Unicode normalization to the text
          after it has been processed for symbol pronunciation and speech dictionaries.
  :returns: The processed text

### `cancelSpeech`()  *(line 222)*

  Interupts the synthesizer from currently speaking

### `pauseSpeech`(switch)  *(line 241)*

### `_getSpeakMessageSpeech`(text: str) -> SpeechSequence  *(line 248)*

  Gets the speech sequence for a given message.
  @param text: the message to speak

### `speakMessage`(text: str, priority: Optional[Spri]=None) -> None  *(line 264)*

  Speaks a given message.
  @param text: the message to speak
  @param priority: The speech priority.

### `_getSpeakSsmlSpeech`(ssml: str, markCallback: 'MarkCallbackT | None'=None, _prefixSpeechCommand: SpeechCommand | None=None) -> SpeechSequence  *(line 277)*

  Gets the speech sequence for given SSML.
  :param ssml: The SSML data to speak
  :param markCallback: An optional callback called for every mark command in the SSML.
  :param _prefixSpeechCommand: A SpeechCommand to append before the sequence.

### `speakSsml`(ssml: str, markCallback: 'MarkCallbackT | None'=None, symbolLevel: characterProcessing.SymbolLevel | None=None, _prefixSpeechCommand: SpeechCommand | None=None, priority: Spri | None=None) -> None  *(line 299)*

  Speaks a given speech sequence provided as ssml.
  :param ssml: The SSML data to speak.
  :param markCallback: An optional callback called for every mark command in the SSML.
  :param symbolLevel: The symbol verbosity level.
  :param _prefixSpeechCommand: A SpeechCommand to append before the sequence.
  :param priority: The speech priority.

### `getCurrentLanguage`() -> str  *(line 318)*

### `spellTextInfo`(info: textInfos.TextInfo, useCharacterDescriptions: bool=False, priority: Optional[Spri]=None) -> None  *(line 333)*

  Spells the text from the given TextInfo, honouring any LangChangeCommand objects it finds if autoLanguageSwitching is enabled.

### `speakSpelling`(text: str, locale: Optional[str]=None, useCharacterDescriptions: bool=False, priority: Optional[Spri]=None) -> None  *(line 355)*

### `_getSpellingSpeechAddCharMode`(seq: Generator[SequenceItemT, None, None]) -> Generator[SequenceItemT, None, None]  *(line 372)*

  Inserts CharacterMode commands in a speech sequence generator to ensure any single character
  is spelled by the synthesizer.
  @param seq: The speech sequence to be spelt.

### `_getSpellingCharAddCapNotification`(speakCharAs: str, sayCapForCapitals: bool, capPitchChange: int, beepForCapitals: bool, reportNormalized: bool=False) -> Generator[SequenceItemT, None, None]  *(line 392)*

  This function produces a speech sequence containing a character to be spelt as well as commands
  to indicate that this character is uppercase and/or normalized, if applicable.
  :param speakCharAs: The character as it will be spoken by the synthesizer.
  :param sayCapForCapitals: indicates if 'cap' should be reported along with the currently spelled character.
  :param capPitchChange: pitch offset to apply while spelling the currently spelled character.
  :param beepForCapitals: indicates if a cap notification beep should be produced while spelling the currently
  spelled character.
  :param reportNormalized: Indicates if 'normalized' should be reported
  along with the currently spelled character.

### `_getSpellingSpeechWithoutCharMode`(text: str, locale: str, useCharacterDescriptions: bool, sayCapForCapitals: bool, capPitchChange: int, beepForCapitals: bool, fallbackToCharIfNoDescription: bool=True, unicodeNormalization: bool=False, reportNormalizedForCharacterNavigation: bool=False) -> Generator[SequenceItemT, None, None]  *(line 440)*

  Processes text when spelling by character.
  This doesn't take care of character mode (Option "Use spelling functionality").
  :param text: The text to speak.
          This is usually one character or a string containing a decomposite character (or glyph),
          however it can also be a word or line of text spoken by a spell command.
  :param locale: The locale used to generate character descriptions, if applicable.
  :param useCharacterDescriptions: Whether or not to use character descriptions,
          e.g. speak "a" as "alpha".
  :param sayCapForCapitals: Indicates if 'cap' should be reported
          along with the currently spelled character.
  :param capPitchChange: Pitch offset to apply while spelling the currently spelled character.
  :param beepForCapitals: Indicates if a cap notification beep should be produced
          while spelling the currently spelled character.
  :param fallbackToCharIfNoDescription: Only applies if useCharacterDescriptions is True.
          If fallbackToCharIfNoDescription is True, and no character description is found,
          the character itself will be announced. Otherwise, nothing will be spoken.
  :param unicodeNormalization: Whether to use Unicode normalization for the given text.
  :param reportNormalizedForCharacterNavigation: When unicodeNormalization is true, indicates if 'normalized'
          should be reported along with the currently spelled character.
  :returns: A speech sequence generator.

### `getSingleCharDescriptionDelayMS`() -> int  *(line 549)*

  @returns: 1 second, a default delay for delayed character descriptions.
  In the future, this should fetch its value from a user defined NVDA idle time.
  Blocked by: https://github.com/nvaccess/nvda/issues/13915

### `getSingleCharDescription`(text: str, locale: Optional[str]=None) -> Generator[SequenceItemT, None, None]  *(line 558)*

  Returns a speech sequence:
  a pause, the length determined by getSingleCharDescriptionDelayMS,
  followed by the character description.

### `getSpellingSpeech`(text: str, locale: Optional[str]=None, useCharacterDescriptions: bool=False) -> Generator[SequenceItemT, None, None]  *(line 597)*

### `getCharDescListFromText`(text, locale)  *(line 634)*

  This method prepares a list, which contains character and its description for all characters the text is made up of, by checking the presence of character descriptions in characterDescriptions.dic of that locale for all possible combination of consecutive characters in the text.
  This is done to take care of conjunct characters present in several languages such as Hindi, Urdu, etc.

### `speakObjectProperties`(obj: 'NVDAObjects.NVDAObject', reason: OutputReason=OutputReason.QUERY, _prefixSpeechCommand: Optional[SpeechCommand]=None, priority: Optional[Spri]=None, **allowedProperties)  *(line 658)*

### `getObjectPropertiesSpeech`(obj: 'NVDAObjects.NVDAObject', reason: OutputReason=OutputReason.QUERY, _prefixSpeechCommand: Optional[SpeechCommand]=None, **allowedProperties) -> SpeechSequence  *(line 678)*

### `_getPlaceholderSpeechIfTextEmpty`(obj, reason: OutputReason) -> Tuple[bool, SpeechSequence]  *(line 816)*

  Attempt to get speech for placeholder attribute if text for 'obj' is empty. Don't report the placeholder
   value unless the text is empty, because it is confusing to hear the current value (presumably typed by the
   user) *and* the placeholder. The placeholder should "disappear" once the user types a value.
  :return: `(True, SpeechSequence)` if text for obj was considered empty and we attempted to get speech for the
          placeholder value. `(False, [])` if text for obj was not considered empty.

### `speakObject`(obj, reason: OutputReason=OutputReason.QUERY, _prefixSpeechCommand: Optional[SpeechCommand]=None, priority: Optional[Spri]=None)  *(line 832)*

### `getObjectSpeech`(obj: 'NVDAObjects.NVDAObject', reason: OutputReason=OutputReason.QUERY, _prefixSpeechCommand: Optional[SpeechCommand]=None) -> SpeechSequence  *(line 847)*

### `_objectSpeech_calculateAllowedProps`(reason: OutputReason, shouldReportTextContent: bool, objRole: controlTypes.Role) -> dict[str, bool]  *(line 919)*

### `speakText`(text: str, reason: OutputReason=OutputReason.MESSAGE, symbolLevel: characterProcessing.SymbolLevel | None=None, priority: Spri | None=None)  *(line 1007)*

  Speaks some text.
  @param text: The text to speak.
  @param reason: Unused
  @param symbolLevel: The symbol verbosity level; C{None} (default) to use the user's configuration.
  @param priority: The speech priority.

### `splitTextIndentation`(text)  *(line 1027)*

  Splits indentation from the rest of the text.
  @param text: The text to split.
  @type text: str
  @return: Tuple of indentation and content.
  @rtype: (str, str)

### `getIndentToneDuration`() -> int  *(line 1042)*

### `getIndentationSpeech`(indentation: str, formatConfig: Dict[str, bool]) -> SpeechSequence  *(line 1046)*

  Retrieves the indentation speech sequence for a given string of indentation.
  @param indentation: The string of indentation.
  @param formatConfig: The configuration to use.

### `speak`(speechSequence: SpeechSequence, symbolLevel: characterProcessing.SymbolLevel | None=None, priority: Spri=Spri.NORMAL)  *(line 1108)*

  Speaks a sequence of text and speech commands
  @param speechSequence: the sequence of text and L{SpeechCommand} objects to speak
  @param symbolLevel: The symbol verbosity level; C{None} (default) to use the user's configuration.
  @param priority: The speech priority.

### `speakPreselectedText`(text: str, priority: Optional[Spri]=None)  *(line 1212)*

  Helper method to announce that a newly focused control already has
  text selected. This method is in contrast with L{speakTextSelected}.
  The method will speak the word "selected" with the provided text appended.
  The announcement order is different from L{speakTextSelected} in order to
  inform a user that the newly focused control has content that is selected,
  which they may unintentionally overwrite.
  
  @remarks: Implemented using L{getPreselectedTextSpeech}

### `getPreselectedTextSpeech`(text: str) -> SpeechSequence  *(line 1230)*

  Helper method to get the speech sequence to announce a newly focused control already has
  text selected.
  This method will speak the word "selected" with the provided text appended.
  The announcement order is different from L{speakTextSelected} in order to
  inform a user that the newly focused control has content that is selected,
  which they may unintentionally overwrite.
  
  @remarks: Implemented using L{_getSelectionMessageSpeech}, which allows for
          creating a speech sequence with an arbitrary attached message.

### `speakTextSelected`(text: str, priority: Optional[Spri]=None)  *(line 1252)*

  Helper method to announce that the user has caused text to be selected.
  This method is in contrast with L{speakPreselectedText}.
  The method will speak the provided text with the word "selected" appended.
  
  @remarks: Implemented using L{speakSelectionMessage}, which allows for
          speaking text with an arbitrary attached message.

### `speakSelectionMessage`(message: str, text: str, priority: Optional[Spri]=None)  *(line 1269)*

### `_getSelectionMessageSpeech`(message: str, text: str) -> SpeechSequence  *(line 1282)*

### `speakSelectionChange`(oldInfo: textInfos.TextInfo, newInfo: textInfos.TextInfo, speakSelected: bool=True, speakUnselected: bool=True, generalize: bool=False, priority: Optional[Spri]=None)  *(line 1298)*

  Speaks a change in selection, either selected or unselected text.
  @param oldInfo: a TextInfo instance representing what the selection was before
  @param newInfo: a TextInfo instance representing what the selection is now
  @param generalize: if True, then this function knows that the text may have changed between the creation of the oldInfo and newInfo objects, meaning that changes need to be spoken more generally, rather than speaking the specific text, as the bounds may be all wrong.
  @param priority: The speech priority.

### `_suppressSpeakTypedCharacters`(number: int)  *(line 1378)*

  Suppress speaking of typed characters.
  This should be used when sending a string of characters to the system
  and those characters should not be spoken individually as if the user were typing them.
  @param number: The number of characters to suppress.

### `isFocusEditable`() -> bool  *(line 1396)*

  Check if the currently focused object is editable.
  :return: ``True`` if the focused object is editable, ``False`` otherwise.

### `speakTypedCharacters`(ch: str)  *(line 1407)*

### class `SpeakTextInfoState`(object)  *(line 1452)*

  Caches the state of speakTextInfo such as the current controlField stack, current formatfield and indentation.

### `SpeakTextInfoState.__init__`(self, obj)  *(line 1462)*

### `SpeakTextInfoState.updateObj`(self)  *(line 1473)*

### `SpeakTextInfoState.copy`(self)  *(line 1478)*

### `_extendSpeechSequence_addMathForTextInfo`(speechSequence: SpeechSequence, info: textInfos.TextInfo, field: textInfos.Field) -> None  *(line 1482)*

### `speakTextInfo`(info: textInfos.TextInfo, useCache: Union[bool, SpeakTextInfoState]=True, formatConfig: Dict[str, bool]=None, unit: Optional[str]=None, reason: OutputReason=OutputReason.QUERY, _prefixSpeechCommand: Optional[SpeechCommand]=None, onlyInitialFields: bool=False, suppressBlanks: bool=False, priority: Optional[Spri]=None) -> bool  *(line 1497)*

### `getTextInfoSpeech`(info: textInfos.TextInfo, useCache: Union[bool, SpeakTextInfoState]=True, formatConfig: dict[str, bool | int] | None=None, unit: Optional[str]=None, reason: OutputReason=OutputReason.QUERY, _prefixSpeechCommand: Optional[SpeechCommand]=None, onlyInitialFields: bool=False, suppressBlanks: bool=False) -> Generator[SpeechSequence, None, bool]  *(line 1528)*

### `_isControlEndFieldCommand`(command: Union[str, textInfos.FieldCommand])  *(line 1920)*

### `_getTextInfoSpeech_considerSpelling`(unit: Optional[textInfos.TextInfo], onlyInitialFields: bool, textWithFields: textInfos.TextInfo.TextWithFieldsT, reason: OutputReason, speechSequence: SpeechSequence, language: str) -> Generator[SpeechSequence, None, None]  *(line 1924)*

### `_getTextInfoSpeech_updateCache`(useCache: Union[bool, SpeakTextInfoState], speakTextInfoState: SpeakTextInfoState, newControlFieldStack: List[textInfos.ControlField], formatFieldAttributesCache: textInfos.Field)  *(line 1957)*

### `getPropertiesSpeech`(reason: OutputReason=OutputReason.QUERY, **propertyValues) -> SpeechSequence  *(line 1972)*

### `_rowAndColumnCountText`(rowCount: int, columnCount: int) -> Optional[str]  *(line 2174)*

### `_rowCountText`(count: int) -> str  *(line 2194)*

### `_columnCountText`(count: int) -> str  *(line 2204)*

### `_shouldSpeakContentFirst`(reason: OutputReason, role: int, presCat: str, attrs: textInfos.ControlField, tableID: Any, states: Iterable[int]) -> bool  *(line 2214)*

  Determines whether or not to speak the content before the controlField information.
  Helper function for getControlFieldSpeech.

### `getControlFieldSpeech`(attrs: textInfos.ControlField, ancestorAttrs: List[textInfos.Field], fieldType: str, formatConfig: Optional[Dict[str, bool]]=None, extraDetail: bool=False, reason: Optional[OutputReason]=None) -> SpeechSequence  *(line 2249)*

### `getFormatFieldSpeech`(attrs: textInfos.Field, attrsCache: Optional[textInfos.Field]=None, formatConfig: Optional[Dict[str, bool]]=None, reason: Optional[OutputReason]=None, unit: Optional[str]=None, extraDetail: bool=False, initialFormat: bool=False) -> SpeechSequence  *(line 2581)*

### `getTableInfoSpeech`(tableInfo: Optional[Dict[str, Any]], oldTableInfo: Optional[Dict[str, Any]], extraDetail: bool=False) -> SpeechSequence  *(line 3082)*

### `clearTypedWordBuffer`() -> None  *(line 3129)*

  Forgets any word currently being built up with typed characters for speaking.
  This should be called when the user's context changes such that they could no longer
  complete the word (such as a focus change or choosing to move the caret).

---

## `speech/speechWithoutPauses.py`

### `_yieldIfNonEmpty`(seq: SpeechSequence)  *(line 28)*

  Helper method to yield the sequence if it is not None or empty.

### class `SpeechWithoutPauses`  *(line 34)*

### `SpeechWithoutPauses.__init__`(self, speakFunc: Callable[[SpeechSequence], None])  *(line 41)*

  :param speakFunc: Function used by L{speakWithoutPauses} to speak. This will likely be speech.speak.

### `SpeechWithoutPauses.reset`(self)  *(line 51)*

### `SpeechWithoutPauses.speakWithoutPauses`(self, speechSequence: Optional[SpeechSequence], detectBreaks: bool=True) -> bool  *(line 54)*

  Speaks the speech sequences given over multiple calls,
  only sending to the synth at acceptable phrase or sentence boundaries,
  or when given None for the speech sequence.
  @return: C{True} if something was actually spoken,
          C{False} if only buffering occurred.

### `SpeechWithoutPauses.getSpeechWithoutPauses`(self, speechSequence: Optional[SpeechSequence], detectBreaks: bool=True) -> Generator[SpeechSequence, None, bool]  *(line 76)*

  Generate speech sequences over multiple calls,
  only returning a speech sequence at acceptable phrase or sentence boundaries,
  or when given None for the speech sequence.
  @return: The speech sequence that can be spoken without pauses. The 'return' for this generator function,
  is a bool which indicates whether this sequence should be considered valid speech. Use
  L{GeneratorWithReturn} to retain the return value. A generator is used because the previous
  implementation had several calls to speech, this approach replicates that.

### `SpeechWithoutPauses._detectBreaksAndGetSpeech`(self, speechSequence: SpeechSequence) -> Generator[SpeechSequence, None, bool]  *(line 108)*

### `SpeechWithoutPauses._flushPendingSpeech`(self) -> SpeechSequence  *(line 134)*

  @return: may be empty sequence

### `SpeechWithoutPauses._getSpeech`(self, speechSequence: SpeechSequence) -> SpeechSequence  *(line 143)*

  @return: May be an empty sequence

---

## `nvwave.py`

> Provides a simple Python interface to playing audio using the Windows Audio Session API (WASAPI), as well as other useful utilities.

### `_isDebugForNvWave`()  *(line 71)*

### class `AudioPurpose`(Enum)  *(line 75)*

  The purpose of a particular stream of audio.

### `playWaveFile`(fileName: str, asynchronous: bool=True, isSpeechWaveFileCommand: bool=False)  *(line 82)*

  plays a specified wave file.
  
  :param fileName: the path to the wave file, usually absolute.
  :param asynchronous: whether the wave file should be played asynchronously
          If ``False``, the calling thread is blocked until the wave has finished playing.
  :param isSpeechWaveFileCommand: whether this wave is played as part of a speech sequence.

### `_cleanup`()  *(line 167)*

### `isInError`() -> bool  *(line 173)*

### class `WavePlayer`(garbageHandler.TrackedObject)  *(line 180)*

  Synchronously play a stream of audio using WASAPI.
  To use, construct an instance and feed it waveform audio using L{feed}.
  Keeps device open until it is either not available, or WavePlayer is explicitly closed / deleted.
  Will attempt to use the preferred device, if not will fallback to the default device.

### `WavePlayer.__init__`(self, channels: int, samplesPerSec: int, bitsPerSample: int, outputDevice: str=DEFAULT_DEVICE_KEY, wantDucking: bool=True, purpose: AudioPurpose=AudioPurpose.SPEECH)  *(line 206)*

  Constructor.
  @param channels: The number of channels of audio; e.g. 2 for stereo, 1 for mono.
  @param samplesPerSec: Samples per second (hz).
  @param bitsPerSample: The number of bits per sample.
  @param outputDevice: The name of the audio output device to use, defaults to WasapiWavePlayer.DEFAULT_DEVICE_KEY
  @param wantDucking: if true then background audio will be ducked on Windows 8 and higher
  @param purpose: The purpose of this audio.
  @note: If C{outputDevice} is a name and no such device exists, the default device will be used.
  @raise WindowsError: If there was an error opening the audio output device.

### `WavePlayer._callback`(cppPlayer, feedId)  *(line 271)*

### `WavePlayer.__del__`(self)  *(line 277)*

### `WavePlayer.open`(self)  *(line 295)*

  Open the output device.
  This will be called automatically when required.
  It is not an error if the output device is already open.

### `WavePlayer.close`(self)  *(line 311)*

  Close the output device.

### `WavePlayer.feed`(self, data: typing.Union[bytes, c_void_p], size: typing.Optional[int]=None, onDone: typing.Optional[typing.Callable]=None) -> None  *(line 315)*

  Feed a chunk of audio data to be played.
  This will block until there is sufficient space in the buffer.
  However, it will return well before the audio is finished playing.
  This allows for uninterrupted playback as long as a new chunk is fed before
  the previous chunk has finished playing.
  @param data: Waveform audio in the format specified when this instance was constructed.
  @param size: The size of the data in bytes if data is a ctypes pointer.
          If data is a Python bytes object, size should be None.
  @param onDone: Function to call when this chunk has finished playing.
  @raise WindowsError: If there was an error initially opening the device.

### `WavePlayer.sync`(self)  *(line 371)*

  Synchronise with playback.
  This method blocks until the previously fed chunk of audio has finished playing.

### `WavePlayer.idle`(self)  *(line 377)*

  Indicate that this player is now idle; i.e. the current continuous segment  of audio is complete.

### `WavePlayer.stop`(self)  *(line 385)*

  Stop playback.

### `WavePlayer.pause`(self, switch: bool)  *(line 397)*

  Pause or unpause playback.
  @param switch: C{True} to pause playback, C{False} to unpause.

### `WavePlayer.setVolume`(self, *, all: Optional[float]=None, left: Optional[float]=None, right: Optional[float]=None)  *(line 417)*

  Set the volume of one or more channels in this stream.
  Levels must be specified as a number between 0 and 1.
  @param all: The level to set for all channels.
  @param left: The level to set for the left channel.
  @param right: The level to set for the right channel.

### `WavePlayer.enableTrimmingLeadingSilence`(self, enable: bool) -> None  *(line 446)*

  Enable or disable automatic leading silence removal.
  This is by default enabled for speech audio, and disabled for non-speech audio.

### `WavePlayer.startTrimmingLeadingSilence`(self, start: bool=True) -> None  *(line 453)*

  Start or stop trimming the leading silence from the next audio chunk.

### `WavePlayer._setVolumeFromConfig`(self)  *(line 457)*

### @classmethod `WavePlayer._scheduleIdleCheck`(cls)  *(line 470)*

### @classmethod `WavePlayer._idleCheck`(cls)  *(line 485)*

  Check whether there are open audio streams that should be considered
  idle. If there are any, stop them. If there are open streams that
  aren't idle yet, schedule another check.
  This is necessary because failing to stop streams can prevent sleep on some
  systems.
  We do this in a single, class-wide check rather than separately for each
  instance to avoid continually resetting a timer for each call to feed().
  Resetting timers from another thread involves queuing to the main thread.
  Doing that for every chunk of audio would not be very efficient.
  Doing this with a class-wide check means that some checks might not take any
  action and some streams might be stopped a little after the timeout elapses,
  but this isn't problematic for our purposes.

### `WavePlayer._onPreSpeak`(self, speechSequence: SpeechSequence)  *(line 528)*

### `initialize`()  *(line 543)*

### `terminate`() -> None  *(line 562)*

### `playErrorSound`() -> None  *(line 568)*

---

## `audio/soundSplit.py`

### class `SoundSplitState`(DisplayStringIntEnum)  *(line 25)*

### @property `SoundSplitState._displayStringLabels`(self) -> dict[IntEnum, str]  *(line 36)*

### `SoundSplitState.getAppVolume`(self) -> VolumeTupleT  *(line 59)*

### `SoundSplitState.getNVDAVolume`(self) -> VolumeTupleT  *(line 74)*

### `initialize`() -> None  *(line 94)*

### `terminate`()  *(line 106)*

### class `_AudioSessionNotificationWrapper`(AudioSessionNotification)  *(line 114)*

### `_AudioSessionNotificationWrapper.on_session_created`(self, new_session: AudioSession)  *(line 117)*

### `_applyToAllAudioSessions`(callback: AudioSessionNotification, applyToFuture: bool=True) -> None  *(line 127)*

  Executes provided callback function on all active audio sessions.
  Additionally, if applyToFuture is True, then it will register a notification with audio session manager,
  which will execute the same callback for all future sessions as they are created.
  That notification will be active until next invokation of this function,
  or until _unregisterCallback() is called.

### `_unregisterCallback`() -> None  *(line 151)*

### class `_VolumeSetter`(AudioSessionNotification)  *(line 159)*

### `_VolumeSetter.on_session_created`(self, new_session: AudioSession)  *(line 166)*

### `_setSoundSplitState`(state: SoundSplitState, initial: bool=False) -> dict  *(line 184)*

### `_toggleSoundSplitState`() -> None  *(line 203)*

### class `_VolumeRestorer`(AudioSessionEvents)  *(line 227)*

### `_VolumeRestorer.on_state_changed`(self, new_state: str, new_state_id: int)  *(line 231)*

### `_VolumeRestorer.restoreVolume`(self)  *(line 236)*

### `_VolumeRestorer.unregister`(self)  *(line 252)*

---

## `languageHandler.py`

> Language and localization support.
> This module assists in NVDA going global through language services
> such as converting Windows locale ID's to friendly names and presenting available languages.

### class `LOCALE`(enum.IntEnum)  *(line 69)*

### `isNormalizedWin32Locale`(localeName: str) -> bool  *(line 82)*

  Checks if the given locale is in a form which can be used by Win32 locale functions such as
  `GetLocaleInfoEx`. See `normalizeLocaleForWin32` for more comments.

### `normalizeLocaleForWin32`(localeName: str) -> str  *(line 94)*

  Converts given locale to a form which can be used by Win32 locale functions such as
  `GetLocaleInfoEx` unless locale is normalized already.
  Uses hyphen as a language/country separator taking care not to replace underscores used
  as a separator between country name and alternate order specifiers.
  For example locales using alternate sorts see:
  https://docs.microsoft.com/en-us/openspecs/windows_protocols/ms-lcid/e6a54e86-9660-44fa-a005-d00da97722f2
  While NVDA does not support locales requiring multiple sorting orders users may still have their Windows
  set to such locale and if all underscores were replaced unconditionally
  we would be unable to generate Python locale from their default UI language.

### `localeNameToWindowsLCID`(localeName: str) -> int  *(line 110)*

  Retrieves the Windows locale identifier (LCID) for the given locale name
  @param localeName: a string of 2letterLanguage_2letterCountry
  or just language (2letterLanguage or 3letterLanguage)
  @returns: a Windows LCID or L{LCID_NONE} if it could not be retrieved.

### `windowsLCIDToLocaleName`(lcid: int) -> str | None  *(line 128)*

  Gets a normalized locale from a Windows LCID.
  
  NVDA should avoid relying on LCIDs in future, as they have been deprecated by MS:
  https://docs.microsoft.com/en-us/globalization/locale/locale-names

### `getLanguageDescription`(language: str) -> weakref.ReferenceType | None  *(line 153)*

  Finds out the description (localized full name) of a given local name

### `englishLanguageNameFromNVDALocale`(localeName: str) -> str | None  *(line 182)*

  Returns either English name of the given language  using `GetLocaleInfoEx` or None
  if the given locale is not known to Windows.

### `englishCountryNameFromNVDALocale`(localeName: str) -> str | None  *(line 215)*

  Returns either English name of the given country using GetLocaleInfoEx or None
  if the given locale is not known to Windows.

### `ansiCodePageFromNVDALocale`(localeName: str) -> str | None  *(line 234)*

  Returns either ANSI code page for a given locale using GetLocaleInfoEx or None
  if the given locale is not known to Windows.

### `listNVDALocales`() -> list[str]  *(line 259)*

### `getAvailableLanguages`(presentational: bool=False) -> list[tuple[str, str]]  *(line 277)*

  generates a list of locale names, plus their full localized language and country names.
  @param presentational: whether this is meant to be shown alphabetically by language description

### `isLanguageForced`() -> bool  *(line 303)*

  Returns `True` if language is provided from the command line - `False` otherwise.

### `getWindowsLanguage`()  *(line 308)*

  Fetches the locale name of the user's configured language in Windows.

### `_createGettextTranslation`(localeName: str) -> tuple[None | gettext.GNUTranslations | gettext.NullTranslations, str | None]  *(line 321)*

### `setLanguage`(lang: str) -> None  *(line 334)*

  Sets the following using `lang` such as "en", "ru_RU", or "es-ES". Use "Windows" to use the system locale
   - the windows locale for the thread (fallback to system locale)
   - the translation service (fallback to English)
   - Current NVDA language (match the translation service)
   - the python locale for the thread (match the translation service, fallback to system default)

### `localeStringFromLocaleCode`(localeCode: str) -> str  *(line 368)*

  Given an NVDA locale such as 'en' or or a Windows locale such as 'pl_PL'
  creates a locale representation in a standard form for Win32
  which can be safely passed to Python's `setlocale`.
  The required format is:
  'englishLanguageName_englishCountryName.localeANSICodePage'
  Raises exception if the given locale is not known to Windows.

### `_setPythonLocale`(localeString: str) -> bool  *(line 385)*

  Sets Python locale to a specified one.
  Returns `True` if succesfull `False` if locale cannot be set or retrieved.

### `setLocale`(localeName: str) -> None  *(line 401)*

  Set python's locale using a `localeName` such as "en", "ru_RU", or "es-ES".
  Will fallback on current NVDA language if it cannot be set and finally fallback to the system locale.
  Passing NVDA locales straight to python `locale.setlocale` does now work since it tries to normalize the
  parameter using `locale.normalize` which results in locales unknown to Windows (Python issue 37945).
  For example executing: `locale.setlocale(locale.LC_ALL, "pl")`
  results in locale being set to `('pl_PL', 'ISO8859-2')`
  which is meaningless to Windows,

### `getLanguage`() -> str  *(line 448)*

### `normalizeLanguage`(lang: str) -> str | None  *(line 452)*

  Normalizes a  language-dialect string  in to a standard form we can deal with.
  Converts  any dash to underline, and makes sure that language is lowercase and dialect is upercase.

### `useImperialMeasurements`() -> bool  *(line 468)*

  Whether or not measurements should be reported as imperial, rather than metric.

### `stripLocaleFromLangCode`(langWithOptionalLocale: str) -> str  *(line 479)*

  Get the lang code eg "en" for "en-au" or "chr" for "chr-US-Qaaa-x-west".
  @param langWithOptionalLocale: may already be language only, or include locale specifier
  (e.g. "en" or "en-au").
  @return The language only part, before the first dash.

---

## `speech/languageHandling.py`

### `getSpeechSequenceWithLangs`(speechSequence: SpeechSequence) -> SpeechSequence  *(line 15)*

  Get a speech sequence with the language description for each non default language of the read text.
  
  :param speechSequence: The original speech sequence.
  :return: A speech sequence containing descriptions for each non default language, indicating if the language is not supported by the current synthesizer.

### `shouldSwitchVoice`() -> bool  *(line 55)*

  Determines if the current synthesizer should switch to the voice corresponding to the language of the text been read.

### `shouldMakeLangChangeCommand`() -> bool  *(line 60)*

  Determines if NVDA should get the language of the text been read.

### `shouldReportNotSupported`() -> bool  *(line 65)*

  Determines if NVDA should report if the language is not supported by the synthesizer.

### `getLangToReport`(lang: str) -> str  *(line 73)*

  Gets the language to report by NVDA, according to speech settings.
  
  :param lang: A language code corresponding to the text been read.
  :return: A language code corresponding to the language to be reported.

---

## `driverHandler.py`

> Handler for driver functionality that is global to synthesizers and braille displays.

### class `Driver`(AutoSettings)  *(line 12)*

  Abstract base class for drivers, such as speech synthesizer and braille display drivers.
  Abstract subclasses such as L{braille.BrailleDisplayDriver} should set L{_configSection}.
  
  At a minimum, drivers must set L{name} and L{description} and override the L{check} method.
  
  L{supportedSettings} should be set as appropriate for the settings supported by the driver.
  Each setting is retrieved and set using attributes named after the setting;
  e.g. the L{dotFirmness} attribute is used for the L{dotFirmness} setting.
  These will usually be properties.

### `Driver.__init__`(self)  *(line 35)*

  Initialize this driver.
  This method can also set default settings for the driver.
  @raise Exception: If an error occurs.
  @postcondition: This driver can be used.

### `Driver.terminate`(self)  *(line 43)*

  Save settings and terminate this driver.
  This should be used for any required clean up.
  @precondition: L{initialize} has been called.
  @postcondition: This driver can no longer be used.

### `Driver.__repr__`(self)  *(line 52)*

### @classmethod `Driver.check`(cls)  *(line 56)*

  Determine whether this driver is available.
  The driver will be excluded from the list of available drivers if this method returns C{False}.
  For example, if a speech synthesizer requires installation and it is not installed, C{False} should be returned.
  @return: C{True} if this driver is available, C{False} if not.
  @rtype: bool

### @classmethod `Driver.getId`(cls) -> str  *(line 67)*

### @classmethod `Driver.getDisplayName`(cls) -> str  *(line 71)*

### @classmethod `Driver._getConfigSection`(cls) -> str  *(line 75)*

---

## `autoSettingsUtils/autoSettings.py`

> autoSettings for add-ons

### class `AutoSettings`(AutoPropertyObject)  *(line 25)*

  An AutoSettings instance is used to simplify the load/save of user config for NVDA extensions
  (Synth drivers, braille drivers, vision providers) and make it possible to automatically provide a
  standard GUI for these settings.
  Derived classes must implement:
  - getId
  - getDisplayName
  - _get_supportedSettings

### `AutoSettings.__init__`(self)  *(line 35)*

  Perform any initialisation
  @note: registers with the config save action extension point

### `AutoSettings.__del__`(self)  *(line 42)*

### `AutoSettings._registerConfigSaveAction`(self)  *(line 45)*

  Overrideable pre_configSave registration

### `AutoSettings._unregisterConfigSaveAction`(self)  *(line 50)*

  Overrideable pre_configSave de-registration

### @classmethod `AutoSettings.getId`(cls) -> str  *(line 56)*

  @return: Application friendly name, should be globally unique, however since this is used in the config file
  human readable is also beneficial.

### @classmethod `AutoSettings.getDisplayName`(cls) -> str  *(line 65)*

  @return: The translated name for this collection of settings. This is for use in the GUI to represent the
  group of these settings.

### @classmethod `AutoSettings._getConfigSection`(cls) -> str  *(line 74)*

  @return: The section of the config that these settings belong in.

### @classmethod `AutoSettings._initSpecificSettings`(cls, clsOrInst: Any, settings: SupportedSettingType) -> None  *(line 81)*

### `AutoSettings.initSettings`(self)  *(line 105)*

  Initializes the configuration for this AutoSettings instance.
  This method is called when initializing the AutoSettings instance.

### `AutoSettings._get_supportedSettings`(self) -> SupportedSettingType  *(line 117)*

  The settings supported by the AutoSettings instance. Abstract.

### `AutoSettings.isSupported`(self, settingID) -> bool  *(line 121)*

  Checks whether given setting is supported by the AutoSettings instance.

### @classmethod `AutoSettings._getConfigSpecForSettings`(cls, settings: SupportedSettingType) -> dict[str, str]  *(line 129)*

### `AutoSettings.getConfigSpec`(self)  *(line 141)*

### @classmethod `AutoSettings._saveSpecificSettings`(cls, clsOrInst: Any, settings: SupportedSettingType) -> None  *(line 145)*

  Save values for settings to config.
  The values from the attributes of `clsOrInst` that match the `id` of each setting are saved to config.
  @param clsOrInst: Destination for the values.
  @param settings: The settings to load.

### `AutoSettings.saveSettings`(self)  *(line 173)*

  Saves the current settings for the AutoSettings instance to the configuration.
  This method is also executed when the AutoSettings instance is loaded for the first time,
  in order to populate the configuration with the initial settings..

### @classmethod `AutoSettings._loadSpecificSettings`(cls, clsOrInst: Any, settings: SupportedSettingType, onlyChanged: bool=False) -> None  *(line 182)*

  Load settings from config, set them on `clsOrInst`.
  @param clsOrInst: Destination for the values.
  @param settings: The settings to load.
  @param onlyChanged: When True, only settings that no longer match the config are set.
  @note: attributes are set on clsOrInst using setattr.
          The id of each setting in `settings` is used as the attribute name.

### `AutoSettings.loadSettings`(self, onlyChanged: bool=False)  *(line 221)*

  Loads settings for this AutoSettings instance from the configuration.
  This method assumes that the instance has attributes o/properties
  corresponding with the name of every setting in L{supportedSettings}.
  @param onlyChanged: When loading settings, only apply those for which
          the value in the configuration differs from the current value.

### @classmethod `AutoSettings._paramToPercent`(cls, current: int, min: int, max: int) -> int  *(line 232)*

  Convert a raw parameter value to a percentage given the current, minimum and maximum raw values.
  @param current: The current value.
  @param min: The minimum value.
  @param max: The maximum value.

### @classmethod `AutoSettings._percentToParam`(cls, percent: int, min: int, max: int) -> int  *(line 241)*

  Convert a percentage to a raw parameter value given the current percentage and the minimum and maximum
  raw parameter values.
  @param percent: The current percentage.
  @param min: The minimum raw parameter value.
  @param max: The maximum raw parameter value.

---

## `autoSettingsUtils/driverSetting.py`

> Classes used to represent settings for Drivers and other AutoSettings instances
> 
> Naming of these classes is historical, kept for backwards compatibility purposes.

### class `DriverSetting`(AutoPropertyObject)  *(line 16)*

  As a base class, represents a setting to be shown in GUI and saved to config.
  
  GUI representation is a string selection GUI control, a wx.Choice control.
  
  Used for synthesizer or braille display setting such as voice, variant or dot firmness as
  well as for settings in Vision Providers

### `DriverSetting._get_configSpec`(self)  *(line 35)*

  Returns the configuration specification of this particular setting for config file validator.
  @rtype: str

### `DriverSetting.__init__`(self, id: str, displayNameWithAccelerator: str, availableInSettingsRing: bool=False, defaultVal: object=None, displayName: Optional[str]=None, useConfig: bool=True)  *(line 41)*

  @param id: internal identifier of the setting
          If this starts with a `_`, it will not be shown in the settings GUI.
  @param displayNameWithAccelerator: the localized string shown in voice or braille settings dialog
  @param availableInSettingsRing: Will this option be available in a settings ring?
  @param defaultVal: Specifies the default value for a driver setting.
  @param displayName: the localized string used in synth settings ring or
          None to use displayNameWithAccelerator
  @param useConfig: Whether the value of this option is loaded from and saved to NVDA's configuration.
          Set this to C{False} if the driver deals with loading and saving.

### class `NumericDriverSetting`(DriverSetting)  *(line 72)*

  Represents a numeric driver setting such as rate, volume, pitch or dot firmness.
  GUI representation is a slider control.

### `NumericDriverSetting._get_configSpec`(self)  *(line 79)*

### `NumericDriverSetting.__init__`(self, id, displayNameWithAccelerator, availableInSettingsRing=False, defaultVal: int=50, minVal: int=0, maxVal: int=100, minStep: int=1, normalStep: int=5, largeStep: int=10, displayName: Optional[str]=None, useConfig: bool=True)  *(line 86)*

  @param defaultVal: Specifies the default value for a numeric driver setting.
  @param minVal: Specifies the minimum valid value for a numeric driver setting.
  @param maxVal: Specifies the maximum valid value for a numeric driver setting.
  @param minStep: Specifies the minimum step between valid values for each numeric setting.
          For example, if L{minStep} is set to 10, setting values can only be multiples of 10; 10, 20, 30, etc.
  @param normalStep: Specifies the step between values that a user will normally prefer.
          This is used in the settings ring.
  @param largeStep: Specifies the step between values if a large adjustment is desired.
          This is used for pageUp/pageDown on sliders in the Voice Settings dialog.
  @note: If necessary, the step values will be normalised so that L{minStep} <= L{normalStep} <= L{largeStep}.

### class `BooleanDriverSetting`(DriverSetting)  *(line 127)*

  Represents a boolean driver setting such as rate boost or automatic time sync.
  GUI representation is a wx.Checkbox

### `BooleanDriverSetting.__init__`(self, id: str, displayNameWithAccelerator: str, availableInSettingsRing: bool=False, displayName: Optional[str]=None, defaultVal: bool=False, useConfig: bool=True)  *(line 134)*

  @param defaultVal: Specifies the default value for a boolean driver setting.

### `BooleanDriverSetting._get_configSpec`(self)  *(line 155)*

---

## `config/__init__.py`

> Manages NVDA configuration.
> The heart of NVDA's configuration is Configuration Manager, which records current options, profile information and functions to load, save, and switch amongst configuration profiles.
> In addition, this module provides three actions: profile switch notifier, an action to be performed when NVDA saves settings, and action to be performed when NVDA is asked to reload configuration from disk or reset settings to factory defaults.
> For the latter two actions, one can perform actions prior to and/or after they take place.

### `__getattr__`(attrName: str) -> Any  *(line 78)*

  Module level `__getattr__` used to preserve backward compatibility.

### `initialize`()  *(line 108)*

### `saveOnExit`()  *(line 113)*

  Save the configuration if configured to save on exit.
  This should only be called if NVDA is about to exit.
  Errors are ignored.

### `isInstalledCopy`() -> bool  *(line 125)*

  Checks to see if this running copy of NVDA is installed on the system

### `getInstalledUserConfigPath`() -> Optional[str]  *(line 169)*

### `getUserDefaultConfigPath`(useInstalledPathIfExists=False)  *(line 193)*

  Get the default path for the user configuration directory.
  This is the default path and doesn't reflect overriding from the command line,
  which includes temporary copies.
  Most callers will want the C{NVDAState.WritePaths.configDir variable} instead.

### `getScratchpadDir`(ensureExists: bool=False) -> str  *(line 224)*

  Returns the path where custom appModules, globalPlugins and drivers can be placed while being developed.

### `initConfigPath`(configPath: Optional[str]=None) -> None  *(line 237)*

  Creates the current configuration path if it doesn't exist. Also makes sure that various sub directories also exist.
  @param configPath: an optional path which should be used instead (only useful when being called from outside of NVDA)

### `getStartAfterLogon`() -> bool  *(line 268)*

  Not to be confused with getStartOnLogonScreen.
  
  Checks if NVDA is set to start after a logon.
  Checks related easeOfAccess current user registry keys.

### `setStartAfterLogon`(enable: bool) -> None  *(line 277)*

  Not to be confused with setStartOnLogonScreen.
  
  Toggle if NVDA automatically starts after a logon.
  Sets easeOfAccess related registry keys.

### `getStartOnLogonScreen`() -> bool  *(line 291)*

  Not to be confused with getStartAfterLogon.
  
  Checks if NVDA is set to start on the logon screen.
  
  Checks related easeOfAccess local machine registry keys.

### `_setStartOnLogonScreen`(enable: bool) -> None  *(line 301)*

### `setSystemConfigToCurrentConfig`()  *(line 305)*

### `_setSystemConfig`(fromPath, *, prefix=sys.prefix)  *(line 321)*

### `setStartOnLogonScreen`(enable: bool) -> None  *(line 354)*

  Not to be confused with setStartAfterLogon.
  
  Toggle whether NVDA starts on the logon screen automatically.
  On failure to set, retries with escalated permissions.
  
  Raises a RuntimeError on failure.

### `_transformSpec`(spec: ConfigObj)  *(line 387)*

  To make the spec less verbose, transform the spec:
  - Add default="default" to all featureFlag items. This is required so that the key can be read,
  even if it is missing from the config.

### class `ConfigManager`(object)  *(line 403)*

  Manages and provides access to configuration.
  In addition to the base configuration, there can be multiple active configuration profiles.
  Settings in more recently activated profiles take precedence,
  with the base configuration being consulted last.
  This allows a profile to override settings in profiles activated earlier and the base configuration.
  A profile need only include a subset of the available settings.
  Changed settings are written to the most recently activated profile.

### `ConfigManager.__init__`(self)  *(line 428)*

### `ConfigManager._handleProfileSwitch`(self, shouldNotify=True)  *(line 453)*

### `ConfigManager._initBaseConf`(self, factoryDefaults=False)  *(line 467)*

### `ConfigManager._loadConfig`(self, fn, fileError=False)  *(line 510)*

### `ConfigManager.__getitem__`(self, key)  *(line 541)*

### `ConfigManager.__contains__`(self, key)  *(line 547)*

### `ConfigManager.get`(self, key, default=None)  *(line 550)*

### `ConfigManager.__setitem__`(self, key, val)  *(line 553)*

### `ConfigManager.dict`(self)  *(line 556)*

### `ConfigManager.listProfiles`(self)  *(line 559)*

### `ConfigManager._getProfileFn`(self, name: str) -> str  *(line 570)*

### `ConfigManager._getProfile`(self, name, load=True)  *(line 573)*

### `ConfigManager.getProfile`(self, name)  *(line 589)*

  Get a profile given its name.
  This is useful for checking whether a profile has been manually activated or triggered.
  @param name: The name of the profile.
  @type name: str
  @return: The profile object.
  @raise KeyError: If the profile is not loaded.

### `ConfigManager.manualActivateProfile`(self, name)  *(line 599)*

  Manually activate a profile.
  Only one profile can be manually active at a time.
  If another profile was manually activated, deactivate it first.
  If C{name} is C{None}, a profile will not be activated.
  @param name: The name of the profile or C{None} for no profile.
  @type name: str

### `ConfigManager._markWriteProfileDirty`(self)  *(line 618)*

### `ConfigManager._writeProfileToFile`(self, filename, profile)  *(line 624)*

### `ConfigManager.save`(self)  *(line 628)*

  Save all modified profiles and the base configuration to disk.

### `ConfigManager.reset`(self, factoryDefaults=False)  *(line 650)*

  Reset the configuration to saved settings or factory defaults.
  @param factoryDefaults: C{True} to reset to factory defaults, C{False} to reset to saved configuration.
  @type factoryDefaults: bool

### `ConfigManager.createProfile`(self, name)  *(line 664)*

  Create a profile.
  @param name: The name of the profile to create.
  @type name: str
  @raise ValueError: If a profile with this name already exists.

### `ConfigManager.deleteProfile`(self, name)  *(line 686)*

  Delete a profile.
  @param name: The name of the profile to delete.
  @type name: str
  @raise LookupError: If the profile doesn't exist.

### `ConfigManager.renameProfile`(self, oldName, newName)  *(line 740)*

  Rename a profile.
  @param oldName: The current name of the profile.
  @type oldName: str
  @param newName: The new name for the profile.
  @type newName: str
  @raise LookupError: If the profile doesn't exist.
  @raise ValueError: If a profile with the new name already exists.

### `ConfigManager._triggerProfileEnter`(self, trigger)  *(line 794)*

  Called by L{ProfileTrigger.enter}}}.

### `ConfigManager._triggerProfileExit`(self, trigger)  *(line 817)*

  Called by L{ProfileTrigger.exit}}}.

### `ConfigManager.atomicProfileSwitch`(self)  *(line 844)*

  Indicate that multiple profile switches should be treated as one.
  This is useful when multiple triggers may be exited/entered at once;
  e.g. when switching applications.
  While multiple switches aren't harmful, they might take longer;
  e.g. unnecessarily switching speech synthesizers or braille displays.
  This is a context manager to be used with the C{with} statement.

### `ConfigManager.suspendProfileTriggers`(self)  *(line 861)*

  Suspend handling of profile triggers.
  Any triggers that currently apply will continue to apply.
  Subsequent enters or exits will not apply until triggers are resumed.
  @see: L{resumeTriggers}

### `ConfigManager.resumeProfileTriggers`(self)  *(line 871)*

  Resume handling of profile triggers after previous suspension.
  Any trigger enters or exits that occurred while triggers were suspended will be applied.
  Trigger handling will then return to normal.
  @see: L{suspendTriggers}

### `ConfigManager.disableProfileTriggers`(self)  *(line 885)*

  Temporarily disable all profile triggers.
  Any triggered profiles will be deactivated and subsequent triggers will not apply.
  Call L{enableTriggers} to re-enable triggers.

### `ConfigManager.enableProfileTriggers`(self)  *(line 902)*

  Re-enable profile triggers after they were previously disabled.

### `ConfigManager._loadProfileTriggers`(self)  *(line 906)*

### `ConfigManager.saveProfileTriggers`(self)  *(line 923)*

  Save profile trigger information to disk.
  This should be called whenever L{profilesToTriggers} is modified.

### `ConfigManager._getSpecFromKeyPath`(self, keyPath)  *(line 934)*

### `ConfigManager._getConfigValidation`(self, spec)  *(line 943)*

  returns a tuple with the spec for the config spec:
  ("type", [], {}, "default value") EG:
  - (u'boolean', [], {}, u'false')
  - (u'integer', [], {'max': u'255', 'min': u'1'}, u'192')
  - (u'option', [u'changedContext', u'fill', u'scroll'], {}, u'changedContext')

### `ConfigManager.getConfigValidation`(self, keyPath)  *(line 952)*

  Get a config validation details
  This can be used to get a L{ConfigValidationData} containing the type, default, options list, or
  other validation parameters (min, max, etc) for a config key.
  @param keyPath: a sequence of the identifiers leading to the config key. EG ("braille", "messageTimeout")
  @return ConfigValidationData

### class `ConfigValidationData`(object)  *(line 968)*

### `ConfigValidationData.__init__`(self, validationFuncName)  *(line 971)*

### class `AggregatedSection`  *(line 984)*

  A view of a section of configuration which aggregates settings from all active profiles.

### `AggregatedSection.__init__`(self, manager: ConfigManager, path: Tuple[str], spec: ConfigObj, profiles: List[ConfigObj])  *(line 989)*

### @staticmethod `AggregatedSection._isSection`(val: Any) -> bool  *(line 1004)*

  Checks if a given value or spec is a section of a config profile.

### `AggregatedSection.__getitem__`(self, key: aggregatedSection._cacheKeyT, checkValidity: bool=True)  *(line 1008)*

### `AggregatedSection.__contains__`(self, key)  *(line 1072)*

### `AggregatedSection.get`(self, key, default=None)  *(line 1079)*

### `AggregatedSection.isSet`(self, key)  *(line 1085)*

  Check whether a given key has been explicitly set.
  This is sometimes useful because it can return C{False} even if there is a default for the key.
  @return: C{True} if the key has been explicitly set, C{False} if not.
  @rtype: bool

### `AggregatedSection._cacheLeaf`(self, key, spec, val)  *(line 1098)*

### `AggregatedSection.__iter__`(self)  *(line 1105)*

### `AggregatedSection.items`(self)  *(line 1122)*

### `AggregatedSection.copy`(self)  *(line 1130)*

### `AggregatedSection.dict`(self)  *(line 1133)*

  Return a deepcopy of self as a dictionary.
  Adapted from L{configobj.Section.dict}.

### `AggregatedSection.__setitem__`(self, key: aggregatedSection._cacheKeyT, val: aggregatedSection._cacheValueT)  *(line 1150)*

### `AggregatedSection._linkDeprecatedValues`(self, key: aggregatedSection._cacheKeyT, val: aggregatedSection._cacheValueT)  *(line 1209)*

  Link deprecated config keys and values to their replacements.
  
  :arg key: The configuration key to link to its new or old counterpart.
  :arg val: The value associated with the configuration key.
  
  Example of how to link values:
  
  >>> match self.path:
  >>>     ...
  >>>     case ("path", "segments"):
  >>>             ...
  >>>             match key:
  >>>                     case "newKey":
  >>>                             # Do something to alias the new path/key to the old path/key for backwards compatibility.
  >>>                     case "oldKey":
  >>>                             # Do something to alias the old path/key to the new path/key for forwards compatibility.
  >>>                     case _:
  >>>                             # We don't care about other keys in this section.
  >>>                             return
  >>>     case _:
  >>>             # We don't care about other sections.
  >>>             return
  >>> ...

### `AggregatedSection._getUpdateSection`(self)  *(line 1244)*

### @property `AggregatedSection.spec`(self)  *(line 1265)*

### `AggregatedSection.spec`(self, val)  *(line 1269)*

### class `ProfileTrigger`(object)  *(line 1276)*

  A trigger for automatic activation/deactivation of a configuration profile.
  The user can associate a profile with a trigger.
  When the trigger applies, the associated profile is activated.
  When the trigger no longer applies, the profile is deactivated.
  L{spec} is a string used to search for this trigger and must be implemented.
  To signal that this trigger applies, call L{enter}.
  To signal that it no longer applies, call L{exit}.
  Alternatively, you can use this object as a context manager via the with statement;
  i.e. this trigger will apply only inside the with block.

### `ProfileTrigger.spec`(self)  *(line 1296)*

  The trigger specification.
  This is a string used to search for this trigger in the user's configuration.
  @rtype: str

### @property `ProfileTrigger.hasProfile`(self)  *(line 1304)*

  Whether this trigger has an associated profile.
  @rtype: bool

### `ProfileTrigger.enter`(self)  *(line 1310)*

  Signal that this trigger applies.
  The associated profile (if any) will be activated.

### `ProfileTrigger.exit`(self)  *(line 1329)*

  Signal that this trigger no longer applies.
  The associated profile (if any) will be deactivated.

### `ProfileTrigger.__exit__`(self, excType, excVal, traceback)  *(line 1343)*

### class `AllowUiaInChromium`(Enum)  *(line 1347)*

### @staticmethod `AllowUiaInChromium.getConfig`() -> 'AllowUiaInChromium'  *(line 1354)*

### class `AllowUiaInMSWord`(Enum)  *(line 1361)*

### @staticmethod `AllowUiaInMSWord.getConfig`() -> 'AllowUiaInMSWord'  *(line 1368)*

---

## `config/configFlags.py`

> Flags used to define the possible values for an option in the configuration.
> Use Flag.MEMBER.value to set a new value or compare with an option in the config;
> use Flag.MEMBER.displayString in the UI for a translatable description of this member.
> 
> When creating new parameter options, consider using F{FeatureFlag} which explicitely defines
> the default value.

### class `NVDAKey`(DisplayStringIntFlag)  *(line 29)*

  IntFlag enumeration containing the possible config values for "Select NVDA Modifier Keys" option in
  keyboard settings.
  
  Use NVDAKey.MEMBER.value to compare with the config;
  the config stores a bitwise combination of one or more of these values.
  use NVDAKey.MEMBER.displayString in the UI for a translatable description of this member.

### @property `NVDAKey._displayStringLabels`(self)  *(line 43)*

### class `TypingEcho`(DisplayStringIntEnum)  *(line 55)*

  Enumeration containing the possible config values for typing echo (characters and words).
  
  Use TypingEcho.MEMBER.value to compare with the config;
  use TypingEcho.MEMBER.displayString in the UI for a translatable description of this member.

### @property `TypingEcho._displayStringLabels`(self)  *(line 67)*

### class `ShowMessages`(DisplayStringIntEnum)  *(line 79)*

  Enumeration containing the possible config values for "Show messages" option in braille settings.
  
  Use ShowMessages.MEMBER.value to compare with the config;
  use ShowMessages.MEMBER.displayString in the UI for a translatable description of this member.

### @property `ShowMessages._displayStringLabels`(self)  *(line 91)*

### class `TetherTo`(DisplayStringStrEnum)  *(line 106)*

  Enumeration containing the possible config values for "Tether to" option in braille settings.
  
  Use TetherTo.MEMBER.value to compare with the config;
  use TetherTo.MEMBER.displayString in the UI for a translatable description of this member.

### @property `TetherTo._displayStringLabels`(self)  *(line 118)*

### class `BrailleMode`(DisplayStringStrEnum)  *(line 132)*

  Enumeration containing the possible config values for "Braille mode" option in braille settings.
  Use BrailleMode.MEMBER.value to compare with the config;
  use BrailleMode.MEMBER.displayString in the UI for a translatable description of this member.

### @property `BrailleMode._displayStringLabels`(self) -> dict['BrailleMode', str]  *(line 142)*

### class `ReportLineIndentation`(DisplayStringIntEnum)  *(line 152)*

  Enumeration containing the possible config values to report line indent.
  
  Use ReportLineIndentation.MEMBER.value to compare with the config;
  use ReportLineIndentation.MEMBER.displayString in the UI for a translatable description of this member.

### @property `ReportLineIndentation._displayStringLabels`(self)  *(line 165)*

### class `ReportSpellingErrors`(DisplayStringIntFlag)  *(line 185)*

  IntFlag enumeration containing the possible config values to report spelling errors while reading.
  
  Use ReportSpellingErrors.MEMBER.value to compare with the config;
  the config stores a bitwise combination of zero, one or more of these values.
  Use ReportSpellingErrors.MEMBER.displayString in the UI for a translatable description of this member.

### @property `ReportSpellingErrors._displayStringLabels`(self) -> dict['ReportSpellingErrors', str]  *(line 200)*

### class `ReportTableHeaders`(DisplayStringIntEnum)  *(line 221)*

  Enumeration containing the possible config values to report table headers.
  
  Use ReportTableHeaders.MEMBER.value to compare with the config;
  use ReportTableHeaders.MEMBER.displayString in the UI for a translatable description of this member.

### @property `ReportTableHeaders._displayStringLabels`(self)  *(line 234)*

### class `ReportCellBorders`(DisplayStringIntEnum)  *(line 252)*

  Enumeration containing the possible config values to report cell borders.
  
  Use ReportCellBorders.MEMBER.value to compare with the config;
  use ReportCellBorders.MEMBER.displayString in the UI for a translatable description of this member.

### @property `ReportCellBorders._displayStringLabels`(self)  *(line 264)*

### class `AddonsAutomaticUpdate`(DisplayStringStrEnum)  *(line 278)*

### @property `AddonsAutomaticUpdate._displayStringLabels`(self)  *(line 284)*

### class `OutputMode`(DisplayStringIntFlag)  *(line 297)*

  Enumeration for ways to output information, such as formatting.
  Use OutputMode.MEMBER.value to compare with the config;
  use OutputMode.MEMBER.displayString in the UI for a translatable description of this member.

### @property `OutputMode._displayStringLabels`(self)  *(line 309)*

### class `ParagraphStartMarker`(DisplayStringStrEnum)  *(line 322)*

### @property `ParagraphStartMarker._displayStringLabels`(self)  *(line 328)*

### class `ReportNotSupportedLanguage`(DisplayStringStrEnum)  *(line 341)*

### @property `ReportNotSupportedLanguage._displayStringLabels`(self) -> dict['ReportNotSupportedLanguage', str]  *(line 347)*

### class `RemoteConnectionMode`(DisplayStringIntEnum)  *(line 359)*

  Enumeration containing the possible remote connection modes (roles for connected clients).
  
  Use RemoteConnectionMode.MEMBER.value to compare with the config;
  use RemoteConnectionMode.MEMBER.displayString in the UI for a translatable description of this member.
  
  Note: This datatype has been chosen as it may be desireable to implement further roles in future.
  For instance, an "observer" role, which is neither controlling or controlled, but which allows the user to listen to the other computers in the channel.

### @property `RemoteConnectionMode._displayStringLabels`(self)  *(line 373)*

### `RemoteConnectionMode.toConnectionMode`(self) -> '_remoteClient.connectionInfo.ConnectionMode'  *(line 381)*

### class `RemoteServerType`(DisplayStringFlag)  *(line 392)*

  Enumeration containing the possible types of Remote relay server.
  
  Use RemoteServerType.MEMBER.value to compare with the config;
  use RemoteServerType.MEMBER.displayString in the UI for a translatable description of this member.

### @property `RemoteServerType._displayStringLabels`(self)  *(line 403)*

### class `LoggingLevel`(DisplayStringIntEnum)  *(line 412)*

  Enumeration containing the possible logging levels.
  
  Use LoggingLevel.MEMBER.value to compare with the config;
  use LoggingLevel.MEMBER.displayString in the UI for a translatable description of this member.

### @property `LoggingLevel._displayStringLabels`(self) -> dict[int, str]  *(line 426)*

---

## `api.py`

> General functions for NVDA
> Functions should mostly refer to getting an object (NVDAObject) or a position (TextInfo).

### `getFocusObject`() -> NVDAObjects.NVDAObject  *(line 38)*

  Gets the current object with focus.
  @returns: the object with focus

### `getForegroundObject`() -> NVDAObjects.NVDAObject  *(line 46)*

  Gets the current foreground object.
  This (cached) object is the (effective) top-level "window" (hwnd).
  EG a Dialog rather than the focused control within the dialog.
  The cache is updated as queued events are processed, as such there will be a delay between the winEvent
  and this function matching. However, within NVDA this should be used in order to be in sync with other
  functions such as "getFocusAncestors".
  @returns: the current foreground object

### `setForegroundObject`(obj: NVDAObjects.NVDAObject) -> bool  *(line 58)*

  Stores the given object as the current foreground object.
  Note: does not cause the operating system to change the foreground window,
          but simply allows NVDA to keep track of what the foreground window is.
          Alternative names for this function may have been:
          - setLastForegroundWindow
          - setLastForegroundEventObject
  @param obj: the object that will be stored as the current foreground object

### `setFocusObject`(obj: NVDAObjects.NVDAObject) -> bool  *(line 79)*

  Stores an object as the current focus object.
  Note: this does not physically change the window with focus in the operating system,
  but allows NVDA to keep track of the correct object.
  Before overriding the last object,
  this function calls event_loseFocus on the object to notify it that it is losing focus.
  @param obj: the object that will be stored as the focus object

### `getFocusDifferenceLevel`()  *(line 196)*

### `getFocusAncestors`()  *(line 200)*

  An array of NVDAObjects that are all parents of the object which currently has focus

### `getMouseObject`()  *(line 205)*

  Returns the object that is directly under the mouse

### `setMouseObject`(obj: NVDAObjects.NVDAObject) -> bool  *(line 210)*

  Tells NVDA to remember the given object as the object that is directly under the mouse

### `getDesktopObject`() -> NVDAObjects.NVDAObject  *(line 221)*

  Get the desktop object

### `setDesktopObject`(obj: NVDAObjects.NVDAObject) -> None  *(line 226)*

  Tells NVDA to remember the given object as the desktop object.
  We cannot prevent setting this when objectBelowLockScreenAndWindowsIsLocked is True,
  as NVDA needs to set the desktopObject on start, and NVDA may start from the lockscreen.

### `getReviewPosition`() -> textInfos.TextInfo  *(line 234)*

  Retrieves the current TextInfo instance representing the user's review position.
  If it is not set, it uses navigator object to create a TextInfo.

### `setReviewPosition`(reviewPosition: textInfos.TextInfo, clearNavigatorObject: bool=True, isCaret: bool=False, isMouse: bool=False) -> bool  *(line 246)*

  Sets a TextInfo instance as the review position.
  @param clearNavigatorObject: if True, It sets the current navigator object to C{None}.
          In that case, the next time the navigator object is asked for it fetches it from the review position.
  @param isCaret: Whether the review position is changed due to caret following.
  @param isMouse: Whether the review position is changed due to mouse following.

### `getNavigatorObject`() -> NVDAObjects.NVDAObject  *(line 293)*

  Gets the current navigator object.
  Navigator objects can be used to navigate around the operating system (with the numpad),
  without moving the focus.
  If the navigator object is not set, it fetches and sets it from the review position.
  @returns: the current navigator object

### `setNavigatorObject`(obj: NVDAObjects.NVDAObject, isFocus: bool=False) -> bool  *(line 316)*

  Sets an object to be the current navigator object.
  Navigator objects can be used to navigate around the operating system (with the numpad),
  without moving the focus.
  It also sets the current review position to None so that next time the review position is asked for,
  it is created from the navigator object.
  @param obj: the object that will be set as the current navigator object
  @param isFocus: true if the navigator object was set due to a focus change.

### `isTypingProtected`()  *(line 356)*

  Checks to see if key echo should be suppressed because the focus is currently on an object that has its protected state set.
  @returns: True if it should be suppressed, False otherwise.
  @rtype: boolean

### `createStateList`(states)  *(line 368)*

  Breaks down the given integer in to a list of numbers that are 2 to the power of their position.

### `moveMouseToNVDAObject`(obj)  *(line 373)*

  Moves the mouse to the given NVDA object's position

### `processPendingEvents`(processEventQueue=True)  *(line 380)*

### `copyToClip`(text: str, notify: Optional[bool]=False) -> bool  *(line 398)*

  Copies the given text to the windows clipboard.
  @returns: True if it succeeds, False otherwise.
  @param text: the text which will be copied to the clipboard
  @param notify: whether to emit a confirmation message

### `getClipData`()  *(line 426)*

  Receives text from the windows clipboard.
  @returns: Clipboard text
  @rtype: string

### `getStatusBar`() -> Optional[NVDAObjects.NVDAObject]  *(line 437)*

  Obtain the status bar for the current foreground object.
  @return: The status bar object or C{None} if no status bar was found.

### `getStatusBarText`(obj)  *(line 462)*

  Get the text from a status bar.
  This includes the name of the status bar and the names and values of all of its children.
  @param obj: The status bar.
  @type obj: L{NVDAObjects.NVDAObject}
  @return: The status bar text.
  @rtype: str

### `filterFileName`(name)  *(line 485)*

  Replaces invalid characters in a given string to make a windows compatible file name.
  @param name: The file name to filter.
  @type name: str
  @returns: The filtered file name.
  @rtype: str

### `isNVDAObject`(obj: Any) -> bool  *(line 498)*

  Returns whether the supplied object is a L{NVDAObjects.NVDAObject}

### `isFakeNVDAObject`(obj: Any) -> bool  *(line 512)*

  Returns whether the supplied object is a fake :class:`NVDAObjects.NVDAObject`.

### `isCursorManager`(obj: Any) -> bool  *(line 517)*

  Returns whether the supplied object is a L{cursorManager.CursorManager}

### `isTreeInterceptor`(obj: Any) -> bool  *(line 522)*

  Returns whether the supplied object is a L{treeInterceptorHandler.TreeInterceptor}

### `isObjectInActiveTreeInterceptor`(obj: NVDAObjects.NVDAObject) -> bool  *(line 527)*

  Returns whether the supplied L{NVDAObjects.NVDAObject} is
  in an active L{treeInterceptorHandler.TreeInterceptor},
  i.e. a tree interceptor that is not in pass through mode.

### `getCaretPosition`() -> 'textInfos.TextInfo'  *(line 539)*

  Gets a text info at the position of the caret.

### `getCaretObject`() -> 'documentBase.TextContainerObject'  *(line 547)*

  Gets the object which contains the caret.
  This is normally the NVDAObject with focus, unless it has a browse mode tree interceptor to return instead.
  @return: The object containing the caret.
  @note: Note: this may not be the NVDA Object closest to the caret, EG an edit text box may have focus,
  and contain multiple NVDAObjects closer to the caret position, consider instead:
          ti = getCaretPosition()
          ti.expand(textInfos.UNIT_CHARACTER)
          closestObj = ti.NVDAObjectAtStart

---

## `baseObject.py`

> Contains the base classes that many of NVDA's classes such as NVDAObjects, virtualBuffers, appModules, synthDrivers inherit from. These base classes provide such things as auto properties, and methods and properties for scripting and key binding.

### class `Getter`(object)  *(line 24)*

### `Getter.__init__`(self, fget, abstract=False)  *(line 25)*

### `Getter.__get__`(self, instance: Union[Any, None, 'AutoPropertyObject'], owner) -> Union[GetterReturnT, 'Getter']  *(line 30)*

### `Getter.setter`(self, func)  *(line 41)*

### `Getter.deleter`(self, func)  *(line 44)*

### class `CachingGetter`(Getter)  *(line 48)*

### `CachingGetter.__get__`(self, instance: Union[Any, None, 'AutoPropertyObject'], owner) -> Union[GetterReturnT, 'CachingGetter']  *(line 49)*

### class `AutoPropertyType`(ABCMeta)  *(line 62)*

### `AutoPropertyType.__init__`(**kwargs: Any)  *(line 63)*

### class `AutoPropertyObject`(garbageHandler.TrackedObject)  *(line 123)*

  A class that dynamically supports properties, by looking up _get_*, _set_*, and _del_* methods at runtime.
  _get_x will make property x with a getter (you can get its value).
  _set_x will make a property x with a setter (you can set its value).
  _del_x will make a property x with a deleter that is executed when deleting its value.
  If there is a _get_x but no _set_x then setting x will override the property completely.
  Properties can also be cached for the duration of one core pump cycle.
  This is useful if the same property is likely to be fetched multiple times in one cycle.
  For example, several NVDAObject properties are fetched by both braille and speech.
  Setting _cache_x to C{True} specifies that x should be cached.
  Setting it to C{False} specifies that it should not be cached.
  If _cache_x is not set, L{cachePropertiesByDefault} is used.
  Properties can also be made abstract.
  Setting _abstract_x to C{True} specifies that x should be abstract.
  Setting it to C{False} specifies that it should not be abstract.

### `AutoPropertyObject.__new__`(cls, *args, **kwargs)  *(line 150)*

### `AutoPropertyObject._getPropertyViaCache`(self, getterMethod: Optional[GetterMethodT]=None) -> GetterReturnT  *(line 158)*

### `AutoPropertyObject.invalidateCache`(self)  *(line 171)*

### @classmethod `AutoPropertyObject.invalidateCaches`(cls)  *(line 175)*

  Invalidate the caches for all current instances.

### class `ScriptableType`(AutoPropertyType)  *(line 183)*

  A metaclass used for collecting and caching gestures on a ScriptableObject

### `ScriptableType.__new__`(**kwargs: Any)  *(line 186)*

### class `ScriptableObject`(AutoPropertyObject)  *(line 208)*

  A class that implements NVDA's scripting interface.
  Input gestures are bound to scripts such that the script will be executed when the appropriate input gesture is received.
  Scripts are methods named with a prefix of C{script_}; e.g. C{script_foo}.
  They accept an L{inputCore.InputGesture} as their single argument.
  Gesture bindings can be specified on the class by creating a C{__gestures} dict which maps gesture identifiers to script names.
  They can also be bound on an instance using the L{bindGesture} method.
  @cvar scriptCategory: If present, a translatable string displayed to the user
          as the category for scripts in this class;
          e.g. in the Input Gestures dialog.
          This can be overridden for individual scripts
          by setting a C{category} attribute on the script method.
  @type scriptCategory: str

### `ScriptableObject.__init__`(self)  *(line 223)*

### `ScriptableObject.bindGesture`(self, gestureIdentifier, scriptName)  *(line 241)*

  Bind an input gesture to a script.
  @param gestureIdentifier: The identifier of the input gesture.
  @type gestureIdentifier: str
  @param scriptName: The name of the script, which is the name of the method excluding the C{script_} prefix.
  @type scriptName: str
  @raise LookupError: If there is no script with the provided name.

### `ScriptableObject.removeGestureBinding`(self, gestureIdentifier)  *(line 265)*

  Removes the binding for the given gesture identifier if a binding exists.
  @param gestureIdentifier: The identifier of the input gesture.
  @type gestureIdentifier: str
  @raise LookupError: If there is no binding for this gesture

### `ScriptableObject.clearGestureBindings`(self)  *(line 277)*

  Remove all input gesture bindings from this object.

### `ScriptableObject.bindGestures`(self, gestureMap)  *(line 281)*

  Bind or unbind multiple input gestures.
  This is a convenience method which simply calls L{bindGesture} for each gesture and script pair, logging any errors.
  For the case where script is None, L{removeGestureBinding} is called instead.
  @param gestureMap: A mapping of gesture identifiers to script names.
  @type gestureMap: dict of str to str

### `ScriptableObject.getScript`(self, gesture)  *(line 300)*

  Retrieve the script bound to a given gesture.
  @param gesture: The input gesture in question.
  @type gesture: L{inputCore.InputGesture}
  @return: The script function or C{None} if none was found.
  @rtype: script function

---

## `extensionPoints/__init__.py`

> Framework to enable extensibility at specific points in the code.
> This allows interested parties to register to be notified when some action occurs
> or to modify a specific kind of data.
> For example, you might wish to notify about a configuration profile switch
> or allow modification of spoken messages before they are passed to the synthesizer.
> See the L{Action}, L{Filter}, L{Decider} and L{AccumulatingDecider} classes.

### class `Action`(HandlerRegistrar[Callable[..., None]])  *(line 27)*

  Allows interested parties to register to be notified when some action occurs.
  For example, this might be used to notify that the configuration profile has been switched.
  
  First, an Action is created:
  
  >>> somethingHappened = extensionPoints.Action()
  
  Interested parties then register to be notified about this action, see
  L{register} docstring for details of the type of handlers that can be
  registered:
  
  >>> def onSomethingHappened(someArg=None):
          ...     print(someArg)
  ...
  >>> somethingHappened.register(onSomethingHappened)
  
  When the action is performed, register handlers are notified, see L{util.callWithSupportedKwargs}
  for how args passed to notify are mapped to the handler:
  
  >>> somethingHappened.notify(someArg=42)

### `Action.notify`(self, **kwargs)  *(line 50)*

  Notify all registered handlers that the action has occurred.
  @param kwargs: Arguments to pass to the handlers.

### `Action.notifyOnce`(self, **kwargs)  *(line 60)*

  Notify all registered handlers that the action has occurred.
  Unregister handlers after calling.
  @param kwargs: Arguments to pass to the handlers.

### class `Filter`(HandlerRegistrar[Union[Callable[..., FilterValueT], Callable[[FilterValueT], FilterValueT]]], Generic[FilterValueT])  *(line 77)*

  Allows interested parties to register to modify a specific kind of data.
  For example, this might be used to allow modification of spoken messages before they are passed to the synthesizer.
  
  First, a Filter is created:
  
  >>> import extensionPoints
  >>> messageFilter = extensionPoints.Filter[str]()
  
  Interested parties then register to filter the data, see
  L{register} docstring for details of the type of handlers that can be
  registered:
  
  >>> def filterMessage(message: str, someArg=None) -> str:
  ...     return message + " which has been filtered."
  ...
  >>> messageFilter.register(filterMessage)
  
  When filtering is desired, all registered handlers are called to filter the data, see L{util.callWithSupportedKwargs}
  for how args passed to apply are mapped to the handler:
  
  >>> messageFilter.apply("This is a message", someArg=42)
  'This is a message which has been filtered'

### `Filter.apply`(self, value: FilterValueT, **kwargs) -> FilterValueT  *(line 105)*

  Pass a value to be filtered through all registered handlers.
  The value is passed to the first handler
  and the return value from that handler is passed to the next handler.
  This process continues for all handlers until the final handler.
  The return value from the final handler is returned to the caller.
  @param value: The value to be filtered.
  @param kwargs: Arguments to pass to the handlers.
  @return: The filtered value.

### class `Decider`(HandlerRegistrar[Callable[..., bool]])  *(line 123)*

  Allows interested parties to participate in deciding whether something
  should be done.
  For example, input gestures are normally executed,
  but this might be used to prevent their execution
  under specific circumstances such as when controlling a remote system.
  
  First, a Decider is created:
  
  >>> doSomething = extensionPoints.Decider()
  
  Interested parties then register to participate in the decision, see
  L{register} docstring for details of the type of handlers that can be
  registered:
  
  >>> def shouldDoSomething(someArg=None):
  ...     return False
  ...
  >>> doSomething.register(shouldDoSomething)
  
  When the decision is to be made, registered handlers are called until
  a handler returns False, see L{util.callWithSupportedKwargs}
  for how args passed to decide are mapped to the handler:
  
  >>> doSomething.decide(someArg=42)
  False
  
  If there are no handlers or all handlers return True,
  the return value is True.

### `Decider.decide`(self, **kwargs)  *(line 154)*

  Call handlers to make a decision.
  If a handler returns False, processing stops
  and False is returned.
  If there are no handlers or all handlers return True, True is returned.
  @param kwargs: Arguments to pass to the handlers.
  @return: The decision.
  @rtype: bool

### class `AccumulatingDecider`(HandlerRegistrar[Callable[..., bool]])  *(line 174)*

  Allows interested parties to participate in deciding whether something
  should be done.
  In contrast with L{Decider} all handlers are executed and then results are returned.
  For example, normally user should be warned about all command line parameters
  which are unknown to NVDA, but this extension point can be used to pass each unknown parameter
  to all add-ons since one of them may want to process some command line arguments.
  
  First, an AccumulatingDecider is created with a default decision  :
  
  >>> doSomething = AccumulatingDecider(defaultDecision=True)
  
  Interested parties then register to participate in the decision, see
  L{register} docstring for details of the type of handlers that can be
  registered:
  
  >>> def shouldDoSomething(someArg=None):
  ...     return False
  ...
  >>> doSomething.register(shouldDoSomething)
  
  When the decision is to be made registered handlers are called and their return values are collected,
  see L{util.callWithSupportedKwargs}
  for how args passed to decide are mapped to the handler:
  
  >>> doSomething.decide(someArg=42)
  False
  
  If there are no handlers or all handlers return defaultDecision,
  the return value is the value of the default decision.

### `AccumulatingDecider.__init__`(self, defaultDecision: bool) -> None  *(line 206)*

### `AccumulatingDecider.decide`(self, **kwargs) -> bool  *(line 210)*

  Call handlers to make a decision.
  Results returned from all handlers are collected
  and if at least one handler returns value different than the one specifed as default it is returned.
  If there are no handlers or all handlers return the default value, the default value is returned.
  @param kwargs: Arguments to pass to the handlers.
  @return: The decision.

### class `Chain`(HandlerRegistrar[Callable[..., Iterable[ChainValueTypeT]]], Generic[ChainValueTypeT])  *(line 233)*

  Allows creating a chain of registered handlers.
  The handlers should return an iterable, e.g. they are usually generator functions,
  but returning a list is also supported.
  
  First, a Chain is created:
  
  >>> chainOfNumbers = extensionPoints.Chain[int]()
  
  Interested parties then register to be iterated.
  See L{register} docstring for details of the type of handlers that can be
  registered:
  
  >>> def yieldSomeNumbers(someArg=None) -> Generator[int, None, None]:
          ...     yield 1
          ...     yield 2
          ...     yield 3
  ...
  >>> def yieldMoreNumbers(someArg=42) -> Generator[int, None, None]:
          ...     yield 4
          ...     yield 5
          ...     yield 6
  ...
  >>> chainOfNumbers.register(yieldSomeNumbers)
  >>> chainOfNumbers.register(yieldMoreNumbers)
  
  When the chain is being iterated, it yields all entries generated by the registered handlers,
  see L{util.callWithSupportedKwargs} for how args passed to iter are mapped to the handler:
  
  >>> chainOfNumbers.iter(someArg=42)

### `Chain.iter`(self, **kwargs) -> Generator[ChainValueTypeT, None, None]  *(line 265)*

  Returns a generator yielding all values generated by the registered handlers.
  @param kwargs: Arguments to pass to the handlers.

---

## `extensionPoints/util.py`

> Utilities used withing the extension points framework. Generally it is expected that the class in __init__.py are
> used, however for more advanced requirements these utilities can be used directly.

### class `AnnotatableWeakref`(weakref.ref, Generic[HandlerT])  *(line 33)*

  A weakref.ref which allows annotation with custom attributes.

### class `BoundMethodWeakref`(Generic[HandlerT])  *(line 39)*

  Weakly references a bound instance method.
  Instance methods are bound dynamically each time they are fetched.
  weakref.ref on a bound instance method doesn't work because
  as soon as you drop the reference, the method object dies.
  Instead, this class holds weak references to both the instance and the function,
  which can then be used to bind an instance method.
  To get the actual method, you call an instance as you would a weakref.ref.

### `BoundMethodWeakref.__init__`(self, target: HandlerT, onDelete: Optional[Callable[[BoundMethodWeakref], None]]=None)  *(line 51)*

### `BoundMethodWeakref.__call__`(self) -> Optional[HandlerT]  *(line 68)*

### `_getHandlerKey`(handler: Callable) -> HandlerKeyT  *(line 78)*

  Get a key which identifies a handler function.
  This is needed because we store weak references, not the actual functions.
  We store the key on the weak reference.
  When the handler dies, we can use the key on the weak reference to remove the handler.

### class `HandlerRegistrar`(Generic[HandlerT])  *(line 90)*

  Base class to Facilitate registration and unregistration of handler functions.
  The handlers are stored using weak references and are automatically unregistered
  if the handler dies.
  Both normal functions, instance methods and lambdas are supported. Ensure to keep lambdas alive by maintaining a
  reference to them.
  The handlers are maintained in the order they were registered
  so that they can be called in a deterministic order across runs.
  This class doesn't provide any functionality to actually call the handlers.
  If you want to implement an extension point,
  you probably want the L{Action} or L{Filter} subclasses instead.

### `HandlerRegistrar.__init__`(self, *, _deprecationMessage: str | None=None)  *(line 103)*

  Initialise the handler registrar.
  
  :param _deprecationMessage: Optional deprecation message to be logged when :method:`register` is called on the handler.

### `HandlerRegistrar.register`(self, handler: HandlerT)  *(line 117)*

  You can register functions, bound instance methods, class methods, static methods or lambdas.
  However, the callable must be kept alive by your code otherwise it will be de-registered.
  This is due to the use of weak references.
  This is especially relevant when using lambdas.

### `HandlerRegistrar.moveToEnd`(self, handler: HandlerT, last: bool=False) -> bool  *(line 141)*

  Move a registered handler to the start or end of the collection with registered handlers.
  This can be used to modify the order in which handlers are called.
  @param last: Whether to move the handler to the end.
          If C{False} (default), the handler is moved to the start.
  @returns: Whether the handler was found.

### `HandlerRegistrar.unregister`(self, handler: Union[AnnotatableWeakref[HandlerT], BoundMethodWeakref[HandlerT], HandlerT])  *(line 158)*

### @property `HandlerRegistrar.handlers`(self) -> Generator[HandlerT, None, None]  *(line 173)*

  Generator of registered handler functions.
  This should be used when you want to call the handlers.

### `callWithSupportedKwargs`(func, *args, **kwargs)  *(line 184)*

  Call a function with only the keyword arguments it supports.
  For example, if myFunc is defined as:
  C{def myFunc(a=None, b=None):}
  and you call:
  C{callWithSupportedKwargs(myFunc, a=1, b=2, c=3)}
  Instead of raising a TypeError, myFunc will simply be called like this:
  C{myFunc(a=1, b=2)}
  
  C{callWithSupportedKwargs} does support positional arguments (C{*args}).
  Unfortunately, positional args can not be matched on name (keyword)
  to the names of the params in the handler.
  Therefore, usage is strongly discouraged due to the
  risk of parameter order differences causing bugs.
  
  @param func: can be any callable that is not an unbound method. EG:
          - Bound instance methods
          - class methods
          - static methods
          - functions
          - lambdas
          - partials
  
          The arguments for the supplied callable, C{func}, do not need to have default values, and can take C{**kwargs} to
          capture all arguments.
          See C{tests/unit/test_extensionPoints.py:TestCallWithSupportedKwargs} for examples.
  
          An exception is raised if:
                  - the number of positional arguments given can not be received by C{func}.
                  - parameters required (parameters declared with no default value) by C{func} are not supplied.

---

## `winVersion.py`

> A module used to record Windows versions.
> It is also used to define feature checks such as
> making sure NVDA can run on a minimum supported version of Windows.
> 
> When working on this file, consider moving to winAPI.

### `_getRunningVersionNameFromWinReg`() -> str  *(line 52)*

  Returns the Windows release name defined in Windows Registry.
  This is applicable on Windows 10 Version 1511 (build 10586) and later.

### class `WinVersion`(object)  *(line 76)*

  Represents a Windows release.
  Includes version major, minor, build, service pack information, machine architecture,
  as well as tools such as checking for specific Windows 10 releases.

### `WinVersion.__init__`(self, major: int=0, minor: int=0, build: int=0, revision: int=0, releaseName: str | None=None, servicePack: str='', productType: str='', processorArchitecture: str='')  *(line 83)*

### `WinVersion._getWindowsReleaseName`(self) -> str  *(line 106)*

  Returns the public release name for a given Windows release based on major, minor, and build.
  This also includes feature update release name.
  This is useful if release names are not defined when constructing this class.
  On server systems, unless noted otherwise, client release names will be returned.
  For example, 'Windows 11 24H2' will be returned on Server 2025 systems.

### `WinVersion.__repr__`(self)  *(line 128)*

### `WinVersion.__eq__`(self, other)  *(line 139)*

### `WinVersion.__ge__`(self, other)  *(line 142)*

### `getWinVer`() -> WinVersion  *(line 170)*

  Returns a record of current Windows version NVDA is running on.

### `isSupportedOS`() -> bool  *(line 214)*

### `isUwpOcrAvailable`() -> bool  *(line 222)*

### `__getattr__`(attrName: str) -> Any  *(line 226)*

  Module level `__getattr__` used to preserve backward compatibility.

---

## `globalVars.py`

> global variables module
> 
> This module is scheduled for deprecation.
> Do not continue to add variables to this module.
> 
> To retain backwards compatibility, variables should not be removed
> from globalVars.
> Instead, encapsulate variables in setters and getters in
> other modules.
> 
> When NVDA core is no longer dependent on globalVars,
> a deprecation warning should be added to this module which
> warns developers when importing anything from this module.
> 
> Once a warning is in place, after some time it may become appropriate to delete this module.

### class `DefaultAppArgs`(argparse.Namespace)  *(line 38)*

---

## `gui/settingsDialogs.py`

### class `SettingsDialog`(DpiScalingHelperMixinWithoutInit, gui.contextHelp.ContextHelpMixin, wx.Dialog)  *(line 114)*

  A settings dialog.
  A settings dialog consists of one or more settings controls and OK and Cancel buttons and an optional Apply button.
  Action may be taken in response to the OK, Cancel or Apply buttons.
  
  To use this dialog:
          * Set L{title} to the title of the dialog.
          * Override L{makeSettings} to populate a given sizer with the settings controls.
          * Optionally, override L{postInit} to perform actions after the dialog is created, such as setting the focus. Be
                  aware that L{postInit} is also called by L{onApply}.
          * Optionally, extend one or more of L{onOk}, L{onCancel} or L{onApply} to perform actions in response to the
                  OK, Cancel or Apply buttons, respectively.
  
  @ivar title: The title of the dialog.
  @type title: str

### class `SettingsDialog.MultiInstanceError`(RuntimeError)  *(line 136)*

### class `SettingsDialog.MultiInstanceErrorWithDialog`(MultiInstanceError)  *(line 139)*

### `SettingsDialog.MultiInstanceErrorWithDialog.__init__`(self, dialog: 'SettingsDialog', *args: object) -> None  *(line 142)*

### class `SettingsDialog.DialogState`(IntEnum)  *(line 146)*

### `SettingsDialog.__new__`(cls, *args, **kwargs)  *(line 156)*

### `SettingsDialog._setInstanceDestroyedState`(self)  *(line 185)*

### `SettingsDialog.__init__`(self, parent: wx.Window, resizeable: bool=False, hasApplyButton: bool=False, settingsSizerOrientation: int=wx.VERTICAL, multiInstanceAllowed: bool=False, buttons: Set[int]={wx.OK, wx.CANCEL})  *(line 207)*

  @param parent: The parent for this dialog; C{None} for no parent.
  @param resizeable: True if the settings dialog should be resizable by the user, only set this if
          you have tested that the components resize correctly.
  @param hasApplyButton: C{True} to add an apply button to the dialog; defaults to C{False} for backwards compatibility.
          Deprecated, use buttons instead.
  @param settingsSizerOrientation: Either wx.VERTICAL or wx.HORIZONTAL. This controls the orientation of the
          sizer that is passed into L{makeSettings}. The default is wx.VERTICAL.
  @param multiInstanceAllowed: Whether multiple instances of SettingsDialog may exist.
          Note that still only one instance of a particular SettingsDialog subclass may exist at one time.
  @param buttons: Buttons to add to the settings dialog.
          Should be a subset of {wx.OK, wx.CANCEL, wx.APPLY, wx.CLOSE}.

### `SettingsDialog._enterActivatesOk_ctrlSActivatesApply`(self, evt)  *(line 284)*

  Listens for keyboard input and triggers ok button on enter and triggers apply button when control + S is
  pressed. Cancel behavior is built into wx.
  Pressing enter will also close the dialog when a list has focus
  (e.g. the list of symbols in the symbol pronunciation dialog).
  Without this custom handler, enter would propagate to the list control (wx ticket #3725).

### `SettingsDialog.makeSettings`(self, sizer)  *(line 299)*

  Populate the dialog with settings controls.
  Subclasses must override this method.
  @param sizer: The sizer to which to add the settings controls.
  @type sizer: wx.Sizer

### `SettingsDialog.postInit`(self)  *(line 307)*

  Called after the dialog has been created.
  For example, this might be used to set focus to the desired control.
  Sub-classes may override this method.

### `SettingsDialog.onOk`(self, evt: wx.CommandEvent)  *(line 313)*

  Take action in response to the OK button being pressed.
  Sub-classes may extend this method.
  This base method should always be called to clean up the dialog.

### `SettingsDialog.onCancel`(self, evt: wx.CommandEvent)  *(line 321)*

  Take action in response to the Cancel button being pressed.
  Sub-classes may extend this method.
  This base method should always be called to clean up the dialog.

### `SettingsDialog.onClose`(self, evt: wx.CommandEvent)  *(line 329)*

  Take action in response to the Close button being pressed.
  Sub-classes may extend this method.
  This base method should always be called to clean up the dialog.

### `SettingsDialog.onApply`(self, evt: wx.CommandEvent)  *(line 337)*

  Take action in response to the Apply button being pressed.
  Sub-classes may extend or override this method.
  This base method should be called to run the postInit method.

### `SettingsDialog._onWindowDestroy`(self, evt: wx.WindowDestroyEvent)  *(line 345)*

### class `SettingsPanel`(DpiScalingHelperMixinWithoutInit, gui.contextHelp.ContextHelpMixin, wx.Panel)  *(line 363)*

  A settings panel, to be used in a multi category settings dialog.
  A settings panel consists of one or more settings controls.
  Action may be taken in response to the parent dialog's OK or Cancel buttons.
  
  To use this panel:
          * Set L{title} to the title of the category.
          * Override L{makeSettings} to populate a given sizer with the settings controls.
          * Optionally, extend L{onPanelActivated} to perform actions after the category has been selected in the list of categories, such as synthesizer or braille display list population.
          * Optionally, extend L{onPanelDeactivated} to perform actions after the category has been deselected (i.e. another category is selected) in the list of categories.
          * Optionally, extend one or both of L{onSave} or L{onDiscard} to perform actions in response to the parent dialog's OK or Cancel buttons, respectively.
          * Optionally, extend one or both of L{isValid} or L{postSave} to perform validation before or steps after saving, respectively.
  
  @ivar title: The title of the settings panel, also listed in the list of settings categories.
  @type title: str

### `SettingsPanel.__init__`(self, parent: wx.Window)  *(line 388)*

  @param parent: The parent for this panel; C{None} for no parent.

### `SettingsPanel._buildGui`(self)  *(line 403)*

### `SettingsPanel.makeSettings`(self, sizer: wx.BoxSizer)  *(line 412)*

  Populate the panel with settings controls.
  Subclasses must override this method.
  @param sizer: The sizer to which to add the settings controls.

### `SettingsPanel.onPanelActivated`(self)  *(line 419)*

  Called after the panel has been activated (i.e. de corresponding category is selected in the list of categories).
  For example, this might be used for resource intensive tasks.
  Sub-classes should extend this method.

### `SettingsPanel.onPanelDeactivated`(self)  *(line 426)*

  Called after the panel has been deactivated (i.e. another category has been selected in the list of categories).
  Sub-classes should extendthis method.

### `SettingsPanel.onSave`(self)  *(line 433)*

  Take action in response to the parent's dialog OK or apply button being pressed.
  Sub-classes should override this method.
  MultiCategorySettingsDialog is responsible for cleaning up the panel when OK is pressed.

### `SettingsPanel.isValid`(self) -> bool  *(line 440)*

  Evaluate whether the current circumstances of this panel are valid
  and allow saving all the settings in a L{MultiCategorySettingsDialog}.
  Sub-classes may extend this method.
  @returns: C{True} if validation should continue,
          C{False} otherwise.

### `SettingsPanel._validationErrorMessageBox`(self, message: str, option: str, category: Optional[str]=None)  *(line 449)*

### `SettingsPanel.postSave`(self)  *(line 472)*

  Take action whenever saving settings for all panels in a L{MultiCategorySettingsDialog} succeeded.
  Sub-classes may extend this method.

### `SettingsPanel.onDiscard`(self)  *(line 477)*

  Take action in response to the parent's dialog Cancel button being pressed.
  Sub-classes may override this method.
  MultiCategorySettingsDialog is responsible for cleaning up the panel when Cancel is pressed.

### `SettingsPanel._sendLayoutUpdatedEvent`(self)  *(line 483)*

  Notify any wx parents that may be listening that they should redo their layout in whatever way
  makes sense for them. It is expected that sub-classes call this method in response to changes in
  the number of GUI items in their panel.

### class `SettingsPanelAccessible`(wx.Accessible)  *(line 493)*

  WX Accessible implementation to set the role of a settings panel to property page,
  as well as to set the accessible description based on the panel's description.

### `SettingsPanelAccessible.GetRole`(self, childId)  *(line 501)*

### `SettingsPanelAccessible.GetDescription`(self, childId)  *(line 504)*

### class `MultiCategorySettingsDialog`(SettingsDialog)  *(line 508)*

  A settings dialog with multiple settings categories.
  A multi category settings dialog consists of a list view with settings categories on the left side,
  and a settings panel on the right side of the dialog.
  Furthermore, in addition to Ok and Cancel buttons, it has an Apply button by default,
  which is different  from the default behavior of L{SettingsDialog}.
  
  To use this dialog: set title and populate L{categoryClasses} with subclasses of SettingsPanel.
  Make sure that L{categoryClasses} only  contains panels that are available on a particular system.
  For example, if a certain category of settings is only supported on Windows 10 and higher,
  that category should be left out of L{categoryClasses}

### class `MultiCategorySettingsDialog.CategoryUnavailableError`(RuntimeError)  *(line 524)*

### `MultiCategorySettingsDialog.__init__`(self, parent, initialCategory=None)  *(line 527)*

  @param parent: The parent for this dialog; C{None} for no parent.
  @type parent: wx.Window
  @param initialCategory: The initial category to select when opening this dialog
  @type parent: SettingsPanel

### `MultiCategorySettingsDialog.makeSettings`(self, settingsSizer)  *(line 572)*

### `MultiCategorySettingsDialog._getCategoryPanel`(self, catId)  *(line 657)*

### `MultiCategorySettingsDialog.postInit`(self)  *(line 685)*

### `MultiCategorySettingsDialog.onCharHook`(self, evt)  *(line 697)*

  Listens for keyboard input and switches panels for control+tab

### `MultiCategorySettingsDialog._onPanelLayoutChanged`(self, evt)  *(line 722)*

### `MultiCategorySettingsDialog._doCategoryChange`(self, newCatId)  *(line 730)*

### `MultiCategorySettingsDialog.onCategoryChange`(self, evt)  *(line 751)*

### `MultiCategorySettingsDialog._validateAllPanels`(self)  *(line 759)*

  Check if all panels are valid, and can be saved
  @note: raises ValueError if a panel is not valid. See c{SettingsPanel.isValid}

### `MultiCategorySettingsDialog._saveAllPanels`(self)  *(line 767)*

### `MultiCategorySettingsDialog._notifyAllPanelsSaveOccurred`(self)  *(line 771)*

### `MultiCategorySettingsDialog._doSave`(self)  *(line 775)*

### `MultiCategorySettingsDialog.onOk`(self, evt)  *(line 780)*

### `MultiCategorySettingsDialog.onCancel`(self, evt)  *(line 789)*

### `MultiCategorySettingsDialog.onApply`(self, evt)  *(line 794)*

### class `GeneralSettingsPanel`(SettingsPanel)  *(line 804)*

### `GeneralSettingsPanel.makeSettings`(self, settingsSizer)  *(line 809)*

### `GeneralSettingsPanel.onChangeMirrorURL`(self, evt: wx.CommandEvent | wx.KeyEvent)  *(line 973)*

  Show the dialog to change the update mirror URL, and refresh the dialog in response to the URL being changed.

### `GeneralSettingsPanel._enterTriggersOnChangeMirrorURL`(self, evt: wx.KeyEvent)  *(line 995)*

  Open the change update mirror URL dialog in response to the enter key in the mirror URL read-only text box.

### `GeneralSettingsPanel.onCopySettings`(self, evt)  *(line 1002)*

### `GeneralSettingsPanel.onSave`(self)  *(line 1062)*

### `GeneralSettingsPanel.onPanelActivated`(self)  *(line 1092)*

### `GeneralSettingsPanel._updateCurrentMirrorURL`(self)  *(line 1097)*

### `GeneralSettingsPanel.postSave`(self)  *(line 1107)*

### class `LanguageRestartDialog`(gui.contextHelp.ContextHelpMixin, wx.Dialog)  *(line 1112)*

### `LanguageRestartDialog.__init__`(self, parent)  *(line 1118)*

### `LanguageRestartDialog.onRestartNowButton`(self, evt)  *(line 1145)*

### class `SpeechSettingsPanel`(SettingsPanel)  *(line 1151)*

### `SpeechSettingsPanel.makeSettings`(self, settingsSizer)  *(line 1156)*

### `SpeechSettingsPanel._enterTriggersOnChangeSynth`(self, evt)  *(line 1195)*

### `SpeechSettingsPanel.onChangeSynth`(self, evt)  *(line 1201)*

### `SpeechSettingsPanel.updateCurrentSynth`(self)  *(line 1211)*

### `SpeechSettingsPanel.onPanelActivated`(self)  *(line 1215)*

### `SpeechSettingsPanel.onPanelDeactivated`(self)  *(line 1220)*

### `SpeechSettingsPanel.onDiscard`(self)  *(line 1224)*

### `SpeechSettingsPanel.onSave`(self)  *(line 1227)*

### `SpeechSettingsPanel.isValid`(self) -> bool  *(line 1230)*

### class `SynthesizerSelectionDialog`(SettingsDialog)  *(line 1234)*

### `SynthesizerSelectionDialog.makeSettings`(self, settingsSizer)  *(line 1240)*

### `SynthesizerSelectionDialog.postInit`(self)  *(line 1249)*

### `SynthesizerSelectionDialog.updateSynthesizerList`(self)  *(line 1253)*

### `SynthesizerSelectionDialog.onOk`(self, evt)  *(line 1265)*

### class `DriverSettingChanger`(object)  *(line 1288)*

  Functor which acts as callback for GUI events.

### `DriverSettingChanger.__init__`(self, driver, setting)  *(line 1291)*

### @property `DriverSettingChanger.driver`(self)  *(line 1296)*

### `DriverSettingChanger.__call__`(self, evt)  *(line 1299)*

### class `StringDriverSettingChanger`(DriverSettingChanger)  *(line 1305)*

  Same as L{DriverSettingChanger} but handles combobox events.

### `StringDriverSettingChanger.__init__`(self, driver, setting, container)  *(line 1308)*

### `StringDriverSettingChanger.__call__`(self, evt)  *(line 1312)*

### class `AutoSettingsMixin`  *(line 1331)*

  Mixin class that provides support for driver/vision provider specific gui settings.
  Derived classes should implement:
  - L{getSettings}
  - L{settingsSizer}
  Derived classes likely need to inherit from L{SettingsPanel}, in particular
  the following methods must be provided:
  - makeSettings
  - onPanelActivated
  @note: This mixin uses self.lastControl and self.sizerDict to keep track of the
  controls added / and maintain ordering.
  If you plan to maintain other controls in the same panel care will need to be taken.

### `AutoSettingsMixin.__init__`(self, *args, **kwargs)  *(line 1346)*

  Mixin init, forwards args to other base class.
  The other base class is likely L{gui.SettingsPanel}.
  @param args: Positional args to passed to other base class.
  @param kwargs: Keyword args to passed to other base class.

### `AutoSettingsMixin.getSettings`(self) -> AutoSettings  *(line 1368)*

### `AutoSettingsMixin.makeSettings`(self, sizer: wx.BoxSizer)  *(line 1371)*

  Populate the panel with settings controls.
  @note: Normally classes also inherit from settingsDialogs.SettingsPanel.
  @param sizer: The sizer to which to add the settings controls.

### `AutoSettingsMixin._getSettingsStorage`(self) -> Any  *(line 1378)*

  Override to change storage object for setting values.

### @property `AutoSettingsMixin.hasOptions`(self) -> bool  *(line 1383)*

### @classmethod `AutoSettingsMixin._setSliderStepSizes`(cls, slider, setting)  *(line 1387)*

### `AutoSettingsMixin._getSettingControlHelpId`(self, controlId)  *(line 1391)*

  Define the helpId associated to this control.

### `AutoSettingsMixin._makeSliderSettingControl`(self, setting: NumericDriverSetting, settingsStorage: Any) -> wx.BoxSizer  *(line 1395)*

  Constructs appropriate GUI controls for given L{DriverSetting} such as label and slider.
  @param setting: Setting to construct controls for
  @param settingsStorage: where to get initial values / set values.
          This param must have an attribute with a name matching setting.id.
          In most cases it will be of type L{AutoSettings}
  @return: wx.BoxSizer containing newly created controls.

### `AutoSettingsMixin._makeStringSettingControl`(self, setting: DriverSetting, settingsStorage: Any)  *(line 1434)*

  Same as L{_makeSliderSettingControl} but for string settings displayed in a wx.Choice control
  Options for the choice control come from the availableXstringvalues property
  (Dict[id, StringParameterInfo]) on the instance returned by self.getSettings()
  The id of the value is stored on settingsStorage.
  Returns sizer with label and combobox.

### `AutoSettingsMixin._makeBooleanSettingControl`(self, setting: BooleanDriverSetting, settingsStorage: Any)  *(line 1489)*

  Same as L{_makeSliderSettingControl} but for boolean settings. Returns checkbox.

### `AutoSettingsMixin.updateDriverSettings`(self, changedSetting=None)  *(line 1518)*

  Creates, hides or updates existing GUI controls for all of supported settings.

### `AutoSettingsMixin._createNewControl`(self, setting, settingsStorage)  *(line 1549)*

### `AutoSettingsMixin._getSettingMaker`(self, setting)  *(line 1564)*

### `AutoSettingsMixin._updateValueForControl`(self, setting, settingsStorage)  *(line 1573)*

### `AutoSettingsMixin.onDiscard`(self)  *(line 1593)*

### `AutoSettingsMixin.onSave`(self)  *(line 1603)*

### `AutoSettingsMixin.refreshGui`(self)  *(line 1606)*

### `AutoSettingsMixin.onPanelActivated`(self)  *(line 1618)*

  Called after the panel has been activated
  @note: Normally classes also inherit from settingsDialogs.SettingsPanel.

### class `VoiceSettingsPanel`(AutoSettingsMixin, SettingsPanel)  *(line 1626)*

### @property `VoiceSettingsPanel.driver`(self)  *(line 1632)*

### `VoiceSettingsPanel.getSettings`(self) -> AutoSettings  *(line 1636)*

### `VoiceSettingsPanel._getSettingControlHelpId`(self, controlId: str) -> str  *(line 1639)*

### `VoiceSettingsPanel.makeSettings`(self, settingsSizer)  *(line 1656)*

### `VoiceSettingsPanel._appendSymbolDictionariesList`(self, settingsSizerHelper: guiHelper.BoxSizerHelper) -> None  *(line 1845)*

### `VoiceSettingsPanel._appendSpeechModesList`(self, settingsSizerHelper: guiHelper.BoxSizerHelper) -> None  *(line 1861)*

### `VoiceSettingsPanel._appendDelayedCharacterDescriptions`(self, settingsSizerHelper: guiHelper.BoxSizerHelper) -> None  *(line 1877)*

### `VoiceSettingsPanel.onAutoLanguageSwitchingChange`(self, evt: wx.CommandEvent)  *(line 1888)*

  Take action when the autoLanguageSwitching checkbox is pressed.

### `VoiceSettingsPanel.onSave`(self)  *(line 1892)*

### `VoiceSettingsPanel._onSpeechModesListChange`(self, evt: wx.CommandEvent)  *(line 1932)*

### `VoiceSettingsPanel._onUnicodeNormalizationChange`(self, evt: wx.CommandEvent)  *(line 1959)*

### `VoiceSettingsPanel.isValid`(self) -> bool  *(line 1965)*

### class `KeyboardSettingsPanel`(SettingsPanel)  *(line 1981)*

### `KeyboardSettingsPanel.makeSettings`(self, settingsSizer)  *(line 1986)*

### `KeyboardSettingsPanel.isValid`(self) -> bool  *(line 2119)*

### `KeyboardSettingsPanel.onSave`(self)  *(line 2133)*

### class `MouseSettingsPanel`(SettingsPanel)  *(line 2153)*

### `MouseSettingsPanel.makeSettings`(self, settingsSizer)  *(line 2158)*

### `MouseSettingsPanel.onSave`(self)  *(line 2228)*

### class `ReviewCursorPanel`(SettingsPanel)  *(line 2240)*

### `ReviewCursorPanel.makeSettings`(self, settingsSizer)  *(line 2245)*

### `ReviewCursorPanel.onSave`(self)  *(line 2271)*

### class `InputCompositionPanel`(SettingsPanel)  *(line 2278)*

### `InputCompositionPanel.makeSettings`(self, settingsSizer)  *(line 2283)*

### `InputCompositionPanel.onSave`(self)  *(line 2357)*

### class `ObjectPresentationPanel`(SettingsPanel)  *(line 2375)*

### `ObjectPresentationPanel.makeSettings`(self, settingsSizer)  *(line 2405)*

### `ObjectPresentationPanel.onSave`(self)  *(line 2515)*

### class `BrowseModePanel`(SettingsPanel)  *(line 2537)*

### `BrowseModePanel.makeSettings`(self, settingsSizer)  *(line 2542)*

### `BrowseModePanel.onSave`(self)  *(line 2652)*

### class `MathSettingsPanel`(SettingsPanel)  *(line 2673)*

### `MathSettingsPanel._getSpeechStyleDisplayString`(self, configValue: str) -> str  *(line 2683)*

  Helper function to get the display string for a speech style config value.
  
  :param configValue: The config value to find the display string for
  :return: The display string to show in the UI

### `MathSettingsPanel._getEnumIndexFromConfigValue`(self, enumClass: Type[DisplayStringEnum], configValue: Any) -> int  *(line 2700)*

  Helper function to get the index of an enum option based on its config value.
  
  :param enumClass: The DisplayStringEnum class to search in
  :param configValue: The config value to find the index for
  :return: The index of the enum option with the matching value

### `MathSettingsPanel._getEnumValueFromSelection`(self, enumClass: Type[DisplayStringEnum], selectionIndex: int) -> Any  *(line 2717)*

  Helper function to get the config value from a selection index.
  
  :param enumClass: The DisplayStringEnum class to get the value from
  :param selectionIndex: The index of the selected option
  :return: The config value of the selected enum option

### `MathSettingsPanel.makeSettings`(self, settingsSizer: wx.BoxSizer) -> None  *(line 2734)*

### `MathSettingsPanel.onSave`(self)  *(line 2994)*

### class `DocumentFormattingPanel`(SettingsPanel)  *(line 3071)*

### `DocumentFormattingPanel.makeSettings`(self, settingsSizer)  *(line 3079)*

### `DocumentFormattingPanel._onLineIndentationChange`(self, evt: wx.CommandEvent) -> None  *(line 3395)*

### `DocumentFormattingPanel._onLinksChange`(self, evt: wx.CommandEvent)  *(line 3398)*

### `DocumentFormattingPanel.onSave`(self)  *(line 3401)*

### class `DocumentNavigationPanel`(SettingsPanel)  *(line 3452)*

### `DocumentNavigationPanel.makeSettings`(self, settingsSizer: wx.BoxSizer) -> None  *(line 3457)*

### `DocumentNavigationPanel.onSave`(self)  *(line 3469)*

### `_synthWarningDialog`(newSynth: str)  *(line 3473)*

### class `AudioPanel`(SettingsPanel)  *(line 3485)*

### `AudioPanel.makeSettings`(self, settingsSizer: wx.BoxSizer) -> None  *(line 3490)*

### `AudioPanel._appendSoundSplitModesList`(self, settingsSizerHelper: guiHelper.BoxSizerHelper) -> None  *(line 3573)*

### `AudioPanel.onSave`(self)  *(line 3588)*

### `AudioPanel.onPanelActivated`(self)  *(line 3621)*

### `AudioPanel._onSoundVolChange`(self, event: wx.Event) -> None  *(line 3625)*

  Called when the sound volume follow checkbox is checked or unchecked.

### `AudioPanel.isValid`(self) -> bool  *(line 3629)*

### class `AddonStorePanel`(SettingsPanel)  *(line 3644)*

### `AddonStorePanel.makeSettings`(self, settingsSizer: wx.BoxSizer) -> None  *(line 3649)*

### `AddonStorePanel.onChangeMirrorURL`(self, evt: wx.CommandEvent | wx.KeyEvent)  *(line 3713)*

  Show the dialog to change the Add-on Store mirror URL, and refresh the dialog in response to the URL being changed.

### `AddonStorePanel._enterTriggersOnChangeMirrorURL`(self, evt: wx.KeyEvent)  *(line 3735)*

  Open the change update mirror URL dialog in response to the enter key in the mirror URL read-only text box.

### `AddonStorePanel._updateCurrentMirrorURL`(self)  *(line 3742)*

### `AddonStorePanel.onPanelActivated`(self)  *(line 3752)*

### `AddonStorePanel.onSave`(self)  *(line 3756)*

### class `RemoteSettingsPanel`(SettingsPanel)  *(line 3763)*

### `RemoteSettingsPanel.makeSettings`(self, sizer: wx.BoxSizer)  *(line 3768)*

### `RemoteSettingsPanel._disableDescendants`(self, sizer: wx.Sizer, excluded: Container[wx.Window])  *(line 3898)*

  Disable all but the specified discendant windows of this sizer.
  
  Disables all child windows, and recursively calls itself for all child sizers.
  
  :param sizer: Root sizer whose descendents should be disabled.
  :param excluded: Container of windows that should remain enabled.

### `RemoteSettingsPanel._setControls`(self) -> None  *(line 3912)*

  Ensure the state of the GUI is internally consistent, as well as consistent with the state of the config.
  
  Does not set the value of controls, just which ones are enabled.

### `RemoteSettingsPanel._setFromConfig`(self) -> None  *(line 3933)*

  Ensure the state of the GUI matches that of the saved configuration.
  
  Also ensures the state of the GUI is internally consistent.

### `RemoteSettingsPanel._onEnableRemote`(self, evt: wx.CommandEvent)  *(line 3950)*

### `RemoteSettingsPanel._onAutoconnect`(self, evt: wx.CommandEvent) -> None  *(line 3953)*

  Respond to the auto-connection checkbox being checked or unchecked.

### `RemoteSettingsPanel._onClientOrServer`(self, evt: wx.CommandEvent) -> None  *(line 3957)*

  Respond to the selected value of the client/server choice control changing.

### `RemoteSettingsPanel.onDeleteFingerprints`(self, evt: wx.CommandEvent) -> None  *(line 3961)*

  Respond to presses of the delete trusted fingerprints button.

### `RemoteSettingsPanel.isValid`(self) -> bool  *(line 3980)*

### `RemoteSettingsPanel.onSave`(self)  *(line 4007)*

### class `TouchInteractionPanel`(SettingsPanel)  *(line 4033)*

### `TouchInteractionPanel.makeSettings`(self, settingsSizer)  *(line 4038)*

### `TouchInteractionPanel.onSave`(self)  *(line 4052)*

### class `UwpOcrPanel`(SettingsPanel)  *(line 4058)*

### `UwpOcrPanel.makeSettings`(self, settingsSizer)  *(line 4063)*

### `UwpOcrPanel.onSave`(self)  *(line 4104)*

### class `AdvancedPanelControls`(gui.contextHelp.ContextHelpMixin, wx.Panel)  *(line 4111)*

  Holds the actual controls for the Advanced Settings panel, this allows the state of the controls to
  be more easily managed.

### `AdvancedPanelControls.__init__`(self, parent)  *(line 4121)*

### `AdvancedPanelControls.isValid`(self) -> bool  *(line 4619)*

### `AdvancedPanelControls.onOpenScratchpadDir`(self, evt)  *(line 4633)*

### `AdvancedPanelControls._getDefaultValue`(self, configPath)  *(line 4637)*

### `AdvancedPanelControls.haveConfigDefaultsBeenRestored`(self)  *(line 4640)*

### `AdvancedPanelControls.restoreToDefaults`(self)  *(line 4676)*

### `AdvancedPanelControls.onSave`(self)  *(line 4704)*

### class `AdvancedPanel`(SettingsPanel)  *(line 4754)*

### `AdvancedPanel.makeSettings`(self, settingsSizer)  *(line 4774)*

  :type settingsSizer: wx.BoxSizer

### `AdvancedPanel.onSave`(self)  *(line 4818)*

### `AdvancedPanel.onEnableControlsCheckBox`(self, evt)  *(line 4822)*

### `AdvancedPanel.isValid`(self) -> bool  *(line 4833)*

### class `BrailleSettingsPanel`(SettingsPanel)  *(line 4839)*

### `BrailleSettingsPanel.makeSettings`(self, settingsSizer)  *(line 4844)*

### `BrailleSettingsPanel._enterTriggersOnChangeDisplay`(self, evt)  *(line 4876)*

### `BrailleSettingsPanel.onChangeDisplay`(self, evt)  *(line 4882)*

### `BrailleSettingsPanel.updateCurrentDisplay`(self)  *(line 4892)*

### `BrailleSettingsPanel.onPanelActivated`(self)  *(line 4899)*

### `BrailleSettingsPanel.onPanelDeactivated`(self)  *(line 4903)*

### `BrailleSettingsPanel.onDiscard`(self)  *(line 4907)*

### `BrailleSettingsPanel.onSave`(self)  *(line 4910)*

### class `BrailleDisplaySelectionDialog`(SettingsDialog)  *(line 4914)*

### `BrailleDisplaySelectionDialog.makeSettings`(self, settingsSizer)  *(line 4921)*

### `BrailleDisplaySelectionDialog.postInit`(self)  *(line 4947)*

### @staticmethod `BrailleDisplaySelectionDialog.getCurrentAutoDisplayDescription`()  *(line 4952)*

### `BrailleDisplaySelectionDialog.updateBrailleDisplayLists`(self)  *(line 4961)*

### `BrailleDisplaySelectionDialog.updateStateDependentControls`(self)  *(line 4991)*

### `BrailleDisplaySelectionDialog.onDisplayNameChanged`(self, evt)  *(line 5019)*

### `BrailleDisplaySelectionDialog.onOk`(self, evt)  *(line 5022)*

### class `BrailleSettingsSubPanel`(AutoSettingsMixin, SettingsPanel)  *(line 5063)*

### @property `BrailleSettingsSubPanel.driver`(self)  *(line 5067)*

### `BrailleSettingsSubPanel.getSettings`(self) -> AutoSettings  *(line 5070)*

### `BrailleSettingsSubPanel.makeSettings`(self, settingsSizer)  *(line 5073)*

### `BrailleSettingsSubPanel.onSave`(self)  *(line 5437)*

### `BrailleSettingsSubPanel.onShowCursorChange`(self, evt)  *(line 5485)*

### `BrailleSettingsSubPanel.onBlinkCursorChange`(self, evt)  *(line 5491)*

### `BrailleSettingsSubPanel.onShowMessagesChange`(self, evt)  *(line 5494)*

### `BrailleSettingsSubPanel.onTetherToChange`(self, evt: wx.CommandEvent) -> None  *(line 5497)*

  Shows or hides "Move system caret when routing review cursor" braille setting.

### `BrailleSettingsSubPanel.onReadByParagraphChange`(self, evt: wx.CommandEvent)  *(line 5502)*

### `BrailleSettingsSubPanel._onModeChange`(self, evt: wx.CommandEvent)  *(line 5505)*

### `showStartErrorForProviders`(parent: wx.Window, providers: List[vision.providerInfo.ProviderInfo]) -> None  *(line 5509)*

### `showTerminationErrorForProviders`(parent: wx.Window, providers: List[vision.providerInfo.ProviderInfo]) -> None  *(line 5539)*

### class `VisionProviderStateControl`(vision.providerBase.VisionProviderStateControl)  *(line 5569)*

  Gives settings panels for vision enhancement providers a way to control a
  single vision enhancement provider, handling any error conditions in
  a UX friendly way.

### `VisionProviderStateControl.__init__`(self, parent: wx.Window, providerInfo: vision.providerInfo.ProviderInfo)  *(line 5576)*

### `VisionProviderStateControl.getProviderInfo`(self) -> vision.providerInfo.ProviderInfo  *(line 5584)*

### `VisionProviderStateControl.getProviderInstance`(self) -> Optional[vision.providerBase.VisionEnhancementProvider]  *(line 5587)*

### `VisionProviderStateControl.startProvider`(self, shouldPromptOnError: bool=True) -> bool  *(line 5590)*

  Initializes the provider, prompting user with the error if necessary.
  @param shouldPromptOnError: True if  the user should be presented with any errors that may occur.
  @return: True on success

### `VisionProviderStateControl.terminateProvider`(self, shouldPromptOnError: bool=True) -> bool  *(line 5603)*

  Terminate the provider, prompting user with the error if necessary.
  @param shouldPromptOnError: True if  the user should be presented with any errors that may occur.
  @return: True on success

### `VisionProviderStateControl._doStartProvider`(self) -> bool  *(line 5616)*

  Attempt to start the provider, catching any errors.
  @return True on successful termination.

### `VisionProviderStateControl._doTerminate`(self) -> bool  *(line 5630)*

  Attempt to terminate the provider, catching any errors.
  @return True on successful termination.

### class `VisionSettingsPanel`(SettingsPanel)  *(line 5649)*

### `VisionSettingsPanel._createProviderSettingsPanel`(self, providerInfo: vision.providerInfo.ProviderInfo) -> Optional[SettingsPanel]  *(line 5660)*

### `VisionSettingsPanel.makeSettings`(self, settingsSizer: wx.BoxSizer)  *(line 5685)*

### `VisionSettingsPanel.safeInitProviders`(self, providers: List[vision.providerInfo.ProviderInfo]) -> None  *(line 5706)*

  Initializes one or more providers in a way that is gui friendly,
  showing an error if appropriate.

### `VisionSettingsPanel.safeTerminateProviders`(self, providers: List[vision.providerInfo.ProviderInfo], verbose: bool=False) -> None  *(line 5720)*

  Terminates one or more providers in a way that is gui friendly,
  @verbose: Whether to show a termination error.
  @returns: Whether termination succeeded for all providers.

### `VisionSettingsPanel.refreshPanel`(self)  *(line 5737)*

### `VisionSettingsPanel.onPanelActivated`(self)  *(line 5744)*

### `VisionSettingsPanel.onDiscard`(self)  *(line 5747)*

### `VisionSettingsPanel.onSave`(self)  *(line 5770)*

### class `VisionProviderSubPanel_Settings`(AutoSettingsMixin, SettingsPanel)  *(line 5781)*

### `VisionProviderSubPanel_Settings.__init__`(self, parent: wx.Window, *, settingsCallable: Callable[[], vision.providerBase.VisionEnhancementProviderSettings])  *(line 5789)*

  @param settingsCallable: A callable that returns an instance to a VisionEnhancementProviderSettings.
          This will usually be a weakref, but could be any callable taking no arguments.

### `VisionProviderSubPanel_Settings.getSettings`(self) -> AutoSettings  *(line 5802)*

### `VisionProviderSubPanel_Settings.makeSettings`(self, settingsSizer)  *(line 5806)*

### class `VisionProviderSubPanel_Wrapper`(SettingsPanel)  *(line 5811)*

### `VisionProviderSubPanel_Wrapper.__init__`(self, parent: wx.Window, providerControl: VisionProviderStateControl)  *(line 5816)*

### `VisionProviderSubPanel_Wrapper.makeSettings`(self, settingsSizer)  *(line 5826)*

### `VisionProviderSubPanel_Wrapper._updateOptionsVisibility`(self)  *(line 5857)*

### `VisionProviderSubPanel_Wrapper._createProviderSettings`(self)  *(line 5865)*

### `VisionProviderSubPanel_Wrapper._nonEnableableGUI`(self, evt)  *(line 5880)*

### `VisionProviderSubPanel_Wrapper._enableToggle`(self, evt)  *(line 5891)*

### `VisionProviderSubPanel_Wrapper.onDiscard`(self)  *(line 5908)*

### `VisionProviderSubPanel_Wrapper.onSave`(self)  *(line 5912)*

### class `MagnifierPanel`(SettingsPanel)  *(line 5918)*

### `MagnifierPanel.makeSettings`(self, settingsSizer: wx.BoxSizer)  *(line 5923)*

### `MagnifierPanel.onSave`(self)  *(line 6004)*

  Save the current selections to config.

### class `PrivacyAndSecuritySettingsPanel`(SettingsPanel)  *(line 6018)*

### `PrivacyAndSecuritySettingsPanel.makeSettings`(self, sizer: wx.BoxSizer)  *(line 6023)*

### `PrivacyAndSecuritySettingsPanel.onSave`(self)  *(line 6106)*

### `PrivacyAndSecuritySettingsPanel._ocrActive`(self) -> bool  *(line 6124)*

  Outputs a message when trying to activate screen curtain when OCR is active.
  
  :return: ``True`` when OCR is active, ``False`` otherwise.

### `PrivacyAndSecuritySettingsPanel._ensureScreenCurtainEnableState`(self, evt: wx.CommandEvent)  *(line 6142)*

  Ensures that toggling the Screen Curtain checkbox toggles the Screen Curtain.

### `PrivacyAndSecuritySettingsPanel._confirmEnableScreenCurtainWithUser`(self) -> bool  *(line 6166)*

  Confirm with the user before enabling Screen Curtain, if configured to do so.
  
  :return: ``True`` if the Screen Curtain should be enabled; ``False`` otherwise.

### class `NVDASettingsDialog`(MultiCategorySettingsDialog)  *(line 6191)*

### `NVDASettingsDialog.makeSettings`(self, settingsSizer)  *(line 6225)*

### `NVDASettingsDialog._doOnCategoryChange`(self)  *(line 6232)*

### `NVDASettingsDialog._getDialogTitle`(self)  *(line 6251)*

### `NVDASettingsDialog.onCategoryChange`(self, evt)  *(line 6258)*

### `NVDASettingsDialog.Destroy`(self)  *(line 6264)*

### class `AddSymbolDialog`(gui.contextHelp.ContextHelpMixin, wx.Dialog)  *(line 6271)*

### `AddSymbolDialog.__init__`(self, parent)  *(line 6277)*

### class `SpeechSymbolsDialog`(SettingsDialog)  *(line 6296)*

### `SpeechSymbolsDialog.__init__`(self, parent)  *(line 6299)*

### `SpeechSymbolsDialog.makeSettings`(self, settingsSizer)  *(line 6320)*

### `SpeechSymbolsDialog.postInit`(self)  *(line 6416)*

### `SpeechSymbolsDialog.filter`(self, filterText='')  *(line 6419)*

### `SpeechSymbolsDialog.getItemTextForList`(self, item, column)  *(line 6462)*

### `SpeechSymbolsDialog.onSymbolEdited`(self)  *(line 6475)*

### `SpeechSymbolsDialog.onListItemFocused`(self, evt)  *(line 6484)*

### `SpeechSymbolsDialog.OnAddClick`(self, evt)  *(line 6499)*

### `SpeechSymbolsDialog.OnRemoveClick`(self, evt)  *(line 6541)*

### `SpeechSymbolsDialog.onOk`(self, evt)  *(line 6565)*

### `SpeechSymbolsDialog._refreshVisibleItems`(self)  *(line 6581)*

### `SpeechSymbolsDialog.onFilterEditTextChange`(self, evt)  *(line 6586)*

### `_isResponseAddonStoreCacheHash`(response: requests.Response) -> bool  *(line 6592)*

### `_isResponseUpdateMetadata`(response: requests.Response) -> bool  *(line 6604)*

---

*Documented: 407, Undocumented (skipped): 645*