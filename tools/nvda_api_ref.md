# NVDA API Reference — Multi-Version Comparison

Versions compared: **2023.3 | 2024.4 | 2025.3 | 2026.1+**
Modules scanned: 40
Source: auto-generated from NVDA source trees + NVDA What's New changelog

---

## Synth Driver API Changelog (What Changed When)

This section summarizes breaking changes, deprecations, and additions relevant
to synth driver and speech add-on development, extracted from NVDA's official
What's New document. Versions without synth-relevant changes are omitted.

### 2025.3

- **SAPI5 deprecations** (no replacement): `synthDrivers.sapi5.LP_c_ubyte`, `LP_c_ulong`, `LP__ULARGE_INTEGER`, `SynthDriver.isSpeaking`.
- `config.conf["speech"]["includeCLDR"]` **removed** — check `config.conf["speech"]["symbolDictionaries"]` for `"cldr"` instead.
- eSpeak NG updated to commit `3b8ef3d`.

### 2025.2

- **New extension point**: `synthDriverHandler.pre_synthSpeak` — fires before `speak()` is called on the current synth. Useful for intercepting/modifying speech sequences.
- **New function**: `utils.mmdevice.getOutputDevices()` for enumerating audio output devices (replaces removed `nvwave` functions).
- `synthDriverHandler.synthDriver.languageIsSupported(lang)` added for checking language support.
- `speech.commands.LangChangeCommand` gains static methods for language detection and voice switching.
- eSpeak NG updated to commit `e93d3a9`.

### 2025.1 (MAJOR BREAKING RELEASE)

- **WASAPI is now mandatory** — opt-out removed. All audio goes through WASAPI.
- **`nvwave.WasapiWavePlayer` renamed to `nvwave.WavePlayer`**. Constructor signature changed:
  - `outputDevice` now only accepts **string** arguments (Windows core audio endpoint device IDs).
  - `closeWhenIdle` parameter **removed**.
  - `buffered` parameter **removed**.
- **Audio config path moved**: `config.conf["speech"]["outputDevice"]` **removed**. Use `config.conf["audio"]["outputDevice"]` (string endpoint device ID).
- **Device enumeration removed from nvwave**: `nvwave.getOutputDeviceNames()`, `nvwave.outputDeviceIDToName()`, `nvwave.outputDeviceNameToID()` all **removed**. Use `utils.mmdevice.getOutputDevices()` (added 2025.2).
- **WASAPI config key removed**: `config.conf["audio"]["WASAPI"]` gone.
- **Hidden settings**: `DriverSetting` instances with `id` starting with `_` are no longer shown in NVDA's settings UI. Useful for internal parameters.
- **SAPI5 now uses `nvwave.WavePlayer`** for audio output. `synthDrivers.sapi5.SPAudioState` and `.SynthDriver.ttsAudioStream` **removed**.
- Unicode normalization enabled by default for speech output.
- eSpeak NG stays at 1.52.0.

### 2024.4

- eSpeak NG updated to **1.52.0** (first stable 1.52 release).
- `[documentFormatting][reportFontAttributes]` deprecated as `bool`; migrating to `int` (`OutputMode` enum).

### 2024.1

- **Minimum Windows version raised to 8.1** (Windows 7/8 dropped).
- **WASAPI introduced as experimental** option in Advanced settings. Includes volume-following and separate NVDA sound volume.
- Audio output device and ducking options moved from "Select Synthesizer" dialog to Audio settings panel (`NVDA+Ctrl+U`).
- eSpeak NG updated to commit `530bf0abf`.

### 2023.2

- **New extension points**:
  - `synthDriverHandler.synthChanged` — fires when the synth driver changes.
  - `nvwave.decide_playWaveFile` — can intercept wave file playback.
  - `tones.decide_beep` — can intercept beeps.
  - `inputCore.decide_executeGesture` — can intercept gesture execution.
- Synth Settings Ring now caches available values lazily (first access, not load time).
- `useConfig=False` can now be set on supported settings for a synth driver.

### 2023.1

- Config spec changes: `[keyboard]` modifier key booleans replaced by `NVDAModifierKeys` enum. `[documentFormatting]` `reportLineIndentation` changed from `bool` to `int` (0-3).

---

### Key Compatibility Notes for TGSpeechBox

| Topic | 2023.3 | 2024.4 | 2025.1+ |
|-------|--------|--------|---------|
| WavePlayer constructor | `purpose=` and `buffered=` both accepted | Same | `purpose=` only; `buffered=` **removed** |
| WavePlayer class name | `WasapiWavePlayer` / `WinmmWavePlayer` | Same | Unified `WavePlayer` |
| `feed()` signature | `(data, size=None, onDone=None)` | Same | Same |
| Output device config | `config.conf["speech"]["outputDevice"]` | Same | `config.conf["audio"]["outputDevice"]` |
| `AudioPurpose.SPEECH` | Available | Available | Available |
| `BooleanDriverSetting` | Available | Available | Available |
| `IndexCommand`, `PitchCommand` | `speech.commands` | Same | Same |
| `_espeak.getVoiceList()` | Available | Available | Available |
| `_espeak.setVoiceByName()` | Available | Available | Available |
| WASAPI | Optional (off by default) | Optional | **Mandatory** |

### eSpeak NG Version Timeline

| NVDA | eSpeak NG |
|------|-----------|
| 2023.1 | commit `9de65fcb` (1.52-dev) |
| 2023.2 | commit `f520fecb` (1.52-dev) |
| 2023.3 | commit `54ee11a79` (1.52-dev) |
| 2024.1 | commit `530bf0abf` (1.52-dev) |
| 2024.2 | commit `cb62d93fd7` (1.52-dev) |
| 2024.3 | commit `961454ff` (1.52-dev) |
| 2024.4 | **1.52.0** (first stable) |
| 2025.1 | 1.52.0 |
| 2025.2 | commit `e93d3a9` (post-1.52.0) |
| 2025.3 | commit `3b8ef3d` (post-1.52.0) |

---

### Legend

| Tag | Meaning |
|-----|---------|
| `ALL` | Present in all versions (2023.3, 2024.4, 2025.3, 2026.1+) |
| `since X` | Added in version X (not in earlier) |
| `until X` | Removed after version X |
| `changed X` | Signature changed in version X |
| `NEW MODULE` | Entire module is new in that version |

---

## `_bridge/base.py`  — `NEW MODULE since 2026.1+`

### class `Connection`  — `since 2026.1+`  *(line 167)*

### `Connection.__del__`(self)  — `since 2026.1+`  *(line 271)*

### `Connection.__init__`(self, stream: Stream, localService: LocalService_t=None, name: str='unknown')  — `since 2026.1+`  *(line 173)*

### @classmethod `Connection._bgEventLoop`(cls, connRef: weakref.ref[Connection], name: str)  — `since 2026.1+`  *(line 192)*

### @classmethod `Connection._closeRawConnection`(cls, conn: rpyc.Connection, name: str)  — `since 2026.1+`  *(line 237)*

### `Connection.bgEventLoop`(self, daemon: bool=False) -> threading.Thread  — `since 2026.1+`  *(line 209)*

### `Connection.close`(self)  — `since 2026.1+`  *(line 247)*

### @property `Connection.closed`(self) -> bool  — `since 2026.1+`  *(line 188)*

### `Connection.eventLoop`(self)  — `since 2026.1+`  *(line 219)*

### @property `Connection.name`(self) -> str  — `since 2026.1+`  *(line 184)*

### @property `Connection.remoteService`(self) -> RemoteService_t  — `since 2026.1+`  *(line 233)*

### class `Proxy`  — `since 2026.1+`  *(line 115)*

  Proxy for wrapping remote rpyc services.
  All NVDA Bridge proxies should inherit from this class.
  
  This class stores a reference to a remote rpyc service and provides a
  helper method to create a subclass with the remote service bound.
  
  :ivar _remoteService: The remote rpyc service instance associated with this proxy.

### `Proxy.__del__`(self)  — `since 2026.1+`  *(line 151)*

### `Proxy.__init__`(self, remoteService: Service_t, *args, **kwargs)  — `since 2026.1+`  *(line 128)*

### `Proxy._connectToDependentServiceOverPipes`(self, r_handle: int, w_handle: int, localService: Service | None=None, name: str='unknown') -> Service  — `since 2026.1+`  *(line 137)*

### `Proxy.holdConnection`(self, conn: Connection)  — `since 2026.1+`  *(line 133)*

### class `Service`(rpyc.Service)  — `since 2026.1+`  *(line 24)*

### `Service.__del__`(self)  — `since 2026.1+`  *(line 109)*

### `Service.__init__`(self, childProcess: subprocess.Popen | None=None)  — `since 2026.1+`  *(line 30)*

### `Service._createDependentConnection`(self, localService: Service, name: str | None=None) -> tuple[int, int]  — `since 2026.1+`  *(line 37)*

### @classmethod `Service.exposed`(cls, func: Callable[..., ResType]) -> Callable[..., ResType]  — `since 2026.1+`  *(line 58)*

### `Service.terminate`(self)  — `since 2026.1+`  *(line 86)*

  Terminate this service and any dependent connections and services.

### @property `Service.terminated`(self) -> bool  — `since 2026.1+`  *(line 83)*

---

## `_bridge/components/proxies/synthDriver.py`  — `NEW MODULE since 2026.1+`

### class `SynthDriverProxy`(Proxy, SynthDriver)  — `since 2026.1+`  *(line 38)*

  Wraps a remote SynthDriverService, providing the same interface as a local SynthDriver.

### `SynthDriverProxy.__init__`(self, service: SynthDriverService)  — `since 2026.1+`  *(line 41)*

### `SynthDriverProxy._getAvailableVariants`(self)  — `since 2026.1+`  *(line 137)*

### `SynthDriverProxy._getAvailableVoices`(self)  — `since 2026.1+`  *(line 130)*

### `SynthDriverProxy._get_pitch`(self)  — `since 2026.1+`  *(line 226)*

### `SynthDriverProxy._get_rate`(self)  — `since 2026.1+`  *(line 220)*

### `SynthDriverProxy._get_rateBoost`(self)  — `since 2026.1+`  *(line 244)*

### `SynthDriverProxy._get_supportedCommands`(self) -> list[type[SynthCommand]]  — `since 2026.1+`  *(line 89)*

### `SynthDriverProxy._get_supportedNotifications`(self) -> set[extensionPoints.Action]  — `since 2026.1+`  *(line 115)*

### `SynthDriverProxy._get_supportedSettings`(self) -> list[DriverSetting]  — `since 2026.1+`  *(line 67)*

### `SynthDriverProxy._get_variant`(self)  — `since 2026.1+`  *(line 238)*

### `SynthDriverProxy._get_voice`(self)  — `since 2026.1+`  *(line 211)*

### `SynthDriverProxy._get_volume`(self)  — `since 2026.1+`  *(line 232)*

### `SynthDriverProxy._set_pitch`(self, value)  — `since 2026.1+`  *(line 229)*

### `SynthDriverProxy._set_rate`(self, value)  — `since 2026.1+`  *(line 223)*

### `SynthDriverProxy._set_rateBoost`(self, value)  — `since 2026.1+`  *(line 247)*

### `SynthDriverProxy._set_variant`(self, value)  — `since 2026.1+`  *(line 241)*

### `SynthDriverProxy._set_voice`(self, value)  — `since 2026.1+`  *(line 214)*

### `SynthDriverProxy._set_volume`(self, value)  — `since 2026.1+`  *(line 235)*

### `SynthDriverProxy.cancel`(self)  — `since 2026.1+`  *(line 205)*

### `SynthDriverProxy.pause`(self, switch: bool)  — `since 2026.1+`  *(line 208)*

### `SynthDriverProxy.speak`(self, speechSequence)  — `since 2026.1+`  *(line 144)*

---

## `_bridge/components/services/synthDriver.py`  — `NEW MODULE since 2026.1+`

### class `SynthDriverService`(Service)  — `since 2026.1+`  *(line 34)*

  Wraps a SynthDriver instance, exposing wire-safe methods for remote usage.
  When accessed remotely, this service must be wrapped in a `_bridge.components.proxies.synthDriver.SynthDriverProxy` which will handle any deserialization and provide the same interface as a local SynthDriver.
  Arguments and return types on the methods here are an internal detail and not thoroughly documented, as they should not be used directly.
  :ivar _synth: The SynthDriver instance being wrapped.

### `SynthDriverService.__init__`(self, synthDriver: SynthDriver)  — `since 2026.1+`  *(line 44)*

### `SynthDriverService.cancel`(self)  — `since 2026.1+`  *(line 192)*

### `SynthDriverService.getAvailableVariants`(self) -> tuple[tuple[str, str], ...]  — `since 2026.1+`  *(line 139)*

### `SynthDriverService.getAvailableVoices`(self) -> tuple[tuple[str, str, str], ...]  — `since 2026.1+`  *(line 135)*

### `SynthDriverService.getParam`(self, param: str) -> Any  — `since 2026.1+`  *(line 200)*

### `SynthDriverService.getSupportedCommands`(self) -> frozenset[str]  — `since 2026.1+`  *(line 99)*

### `SynthDriverService.getSupportedNotifications`(self) -> frozenset[str]  — `since 2026.1+`  *(line 123)*

### `SynthDriverService.getSupportedSettings`(self) -> _SerializedSupportedSettings  — `since 2026.1+`  *(line 89)*

### `SynthDriverService.pause`(self, switch: bool)  — `since 2026.1+`  *(line 196)*

### `SynthDriverService.registerSynthDoneSpeakingNotification`(self, callback: Callable[[], Any])  — `since 2026.1+`  *(line 74)*

### `SynthDriverService.registerSynthIndexReachedNotification`(self, callback: Callable[[int], Any])  — `since 2026.1+`  *(line 63)*

### `SynthDriverService.setParam`(self, param: str, val: Any)  — `since 2026.1+`  *(line 206)*

### `SynthDriverService.speak`(self, data: str)  — `since 2026.1+`  *(line 143)*

### `SynthDriverService.terminate`(self)  — `since 2026.1+`  *(line 215)*

---

## `api.py`

> General functions for NVDA
> Functions should mostly refer to getting an object (NVDAObject) or a position (TextInfo).

### `copyToClip`(text: str, notify: Optional[bool]=False) -> bool  — `ALL`  *(line 398)*

  Copies the given text to the windows clipboard.
  @returns: True if it succeeds, False otherwise.
  @param text: the text which will be copied to the clipboard
  @param notify: whether to emit a confirmation message

### `createStateList`(states)  — `ALL`  *(line 368)*

  Breaks down the given integer in to a list of numbers that are 2 to the power of their position.

### `filterFileName`(name)  — `ALL`  *(line 485)*

  Replaces invalid characters in a given string to make a windows compatible file name.
  @param name: The file name to filter.
  @type name: str
  @returns: The filtered file name.
  @rtype: str

### `getCaretObject`() -> 'documentBase.TextContainerObject'  — `ALL`  *(line 547)*

  Gets the object which contains the caret.
  This is normally the NVDAObject with focus, unless it has a browse mode tree interceptor to return instead.
  @return: The object containing the caret.
  @note: Note: this may not be the NVDA Object closest to the caret, EG an edit text box may have focus,
  and contain multiple NVDAObjects closer to the caret position, consider instead:
          ti = getCaretPosition()
          ti.expand(textInfos.UNIT_CHARACTER)
          closestObj = ti.NVDAObjectAtStart

### `getCaretPosition`() -> 'textInfos.TextInfo'  — `ALL`  *(line 539)*

  Gets a text info at the position of the caret.

### `getClipData`()  — `ALL`  *(line 426)*

  Receives text from the windows clipboard.
  @returns: Clipboard text
  @rtype: string

### `getDesktopObject`() -> NVDAObjects.NVDAObject  — `ALL`  *(line 221)*

  Get the desktop object

### `getFocusAncestors`()  — `ALL`  *(line 200)*

  An array of NVDAObjects that are all parents of the object which currently has focus

### `getFocusDifferenceLevel`()  — `ALL`  *(line 196)*

### `getFocusObject`() -> NVDAObjects.NVDAObject  — `ALL`  *(line 38)*

  Gets the current object with focus.
  @returns: the object with focus

### `getForegroundObject`() -> NVDAObjects.NVDAObject  — `ALL`  *(line 46)*

  Gets the current foreground object.
  This (cached) object is the (effective) top-level "window" (hwnd).
  EG a Dialog rather than the focused control within the dialog.
  The cache is updated as queued events are processed, as such there will be a delay between the winEvent
  and this function matching. However, within NVDA this should be used in order to be in sync with other
  functions such as "getFocusAncestors".
  @returns: the current foreground object

### `getMouseObject`()  — `ALL`  *(line 205)*

  Returns the object that is directly under the mouse

### `getNavigatorObject`() -> NVDAObjects.NVDAObject  — `ALL`  *(line 293)*

  Gets the current navigator object.
  Navigator objects can be used to navigate around the operating system (with the numpad),
  without moving the focus.
  If the navigator object is not set, it fetches and sets it from the review position.
  @returns: the current navigator object

### `getReviewPosition`() -> textInfos.TextInfo  — `ALL`  *(line 234)*

  Retrieves the current TextInfo instance representing the user's review position.
  If it is not set, it uses navigator object to create a TextInfo.

### `getStatusBar`() -> Optional[NVDAObjects.NVDAObject]  — `ALL`  *(line 437)*

  Obtain the status bar for the current foreground object.
  @return: The status bar object or C{None} if no status bar was found.

### `getStatusBarText`(obj)  — `ALL`  *(line 462)*

  Get the text from a status bar.
  This includes the name of the status bar and the names and values of all of its children.
  @param obj: The status bar.
  @type obj: L{NVDAObjects.NVDAObject}
  @return: The status bar text.
  @rtype: str

### `isCursorManager`(obj: Any) -> bool  — `ALL`  *(line 517)*

  Returns whether the supplied object is a L{cursorManager.CursorManager}

### `isFakeNVDAObject`(obj: Any) -> bool  — `since 2026.1+`  *(line 512)*

  Returns whether the supplied object is a fake :class:`NVDAObjects.NVDAObject`.

### `isNVDAObject`(obj: Any) -> bool  — `ALL`  *(line 498)*

  Returns whether the supplied object is a L{NVDAObjects.NVDAObject}

### `isObjectInActiveTreeInterceptor`(obj: NVDAObjects.NVDAObject) -> bool  — `ALL`  *(line 527)*

  Returns whether the supplied L{NVDAObjects.NVDAObject} is
  in an active L{treeInterceptorHandler.TreeInterceptor},
  i.e. a tree interceptor that is not in pass through mode.

### `isTreeInterceptor`(obj: Any) -> bool  — `ALL`  *(line 522)*

  Returns whether the supplied object is a L{treeInterceptorHandler.TreeInterceptor}

### `isTypingProtected`()  — `ALL`  *(line 356)*

  Checks to see if key echo should be suppressed because the focus is currently on an object that has its protected state set.
  @returns: True if it should be suppressed, False otherwise.
  @rtype: boolean

### `moveMouseToNVDAObject`(obj)  — `ALL`  *(line 373)*

  Moves the mouse to the given NVDA object's position

### `processPendingEvents`(processEventQueue=True)  — `ALL`  *(line 380)*

### `setDesktopObject`(obj: NVDAObjects.NVDAObject) -> None  — `ALL`  *(line 226)*

  Tells NVDA to remember the given object as the desktop object.
  We cannot prevent setting this when objectBelowLockScreenAndWindowsIsLocked is True,
  as NVDA needs to set the desktopObject on start, and NVDA may start from the lockscreen.

### `setFocusObject`(obj: NVDAObjects.NVDAObject) -> bool  — `ALL`  *(line 79)*

  Stores an object as the current focus object.
  Note: this does not physically change the window with focus in the operating system,
  but allows NVDA to keep track of the correct object.
  Before overriding the last object,
  this function calls event_loseFocus on the object to notify it that it is losing focus.
  @param obj: the object that will be stored as the focus object

### `setForegroundObject`(obj: NVDAObjects.NVDAObject) -> bool  — `ALL`  *(line 58)*

  Stores the given object as the current foreground object.
  Note: does not cause the operating system to change the foreground window,
          but simply allows NVDA to keep track of what the foreground window is.
          Alternative names for this function may have been:
          - setLastForegroundWindow
          - setLastForegroundEventObject
  @param obj: the object that will be stored as the current foreground object

### `setMouseObject`(obj: NVDAObjects.NVDAObject) -> bool  — `ALL`  *(line 210)*

  Tells NVDA to remember the given object as the object that is directly under the mouse

### `setNavigatorObject`(obj: NVDAObjects.NVDAObject, isFocus: bool=False) -> bool  — `ALL`  *(line 316)*

  Sets an object to be the current navigator object.
  Navigator objects can be used to navigate around the operating system (with the numpad),
  without moving the focus.
  It also sets the current review position to None so that next time the review position is asked for,
  it is created from the navigator object.
  @param obj: the object that will be set as the current navigator object
  @param isFocus: true if the navigator object was set due to a focus change.

### `setReviewPosition`(reviewPosition: textInfos.TextInfo, clearNavigatorObject: bool=True, isCaret: bool=False, isMouse: bool=False) -> bool  — `ALL`  *(line 246)*

  Sets a TextInfo instance as the review position.
  @param clearNavigatorObject: if True, It sets the current navigator object to C{None}.
          In that case, the next time the navigator object is asked for it fetches it from the review position.
  @param isCaret: Whether the review position is changed due to caret following.
  @param isMouse: Whether the review position is changed due to mouse following.

---

## `audio/soundSplit.py`  — `NEW MODULE since 2024.4`

### class `SoundSplitState`(DisplayStringIntEnum)  — `since 2024.4`  *(line 25)*

### @property `SoundSplitState._displayStringLabels`(self) -> dict[IntEnum, str]  — `since 2024.4`  *(line 36)*

### `SoundSplitState.getAppVolume`(self) -> VolumeTupleT  — `since 2024.4`  *(line 59)*

### `SoundSplitState.getNVDAVolume`(self) -> VolumeTupleT  — `since 2024.4`  *(line 74)*

### class `_AudioSessionNotificationWrapper`(AudioSessionNotification)  — `since 2024.4`  *(line 114)*

### `_AudioSessionNotificationWrapper.on_session_created`(self, new_session: AudioSession)  — `since 2024.4`  *(line 117)*

### class `_VolumeRestorer`(AudioSessionEvents)  — `since 2024.4`  *(line 227)*

### `_VolumeRestorer.on_state_changed`(self, new_state: str, new_state_id: int)  — `since 2024.4`  *(line 231)*

### `_VolumeRestorer.restoreVolume`(self)  — `since 2024.4`  *(line 236)*

### `_VolumeRestorer.unregister`(self)  — `since 2024.4`  *(line 252)*

### class `_VolumeSetter`(AudioSessionNotification)  — `since 2024.4`  *(line 159)*

### `_VolumeSetter.on_session_created`(self, new_session: AudioSession)  — `since 2024.4`  *(line 166)*

### `_applyToAllAudioSessions`(callback: AudioSessionNotification, applyToFuture: bool=True) -> None  — `since 2024.4`  *(line 127)*

  Executes provided callback function on all active audio sessions.
  Additionally, if applyToFuture is True, then it will register a notification with audio session manager,
  which will execute the same callback for all future sessions as they are created.
  That notification will be active until next invokation of this function,
  or until _unregisterCallback() is called.

### `_setSoundSplitState`(state: SoundSplitState, initial: bool=False) -> dict  — `since 2024.4`  *(line 184)*

### `_toggleSoundSplitState`() -> None  — `since 2024.4`  *(line 203)*

### `_unregisterCallback`() -> None  — `since 2024.4`  *(line 151)*

### `initialize`() -> None  — `since 2024.4`  *(line 94)*

### `terminate`()  — `since 2024.4`  *(line 106)*

---

## `autoSettingsUtils/autoSettings.py`

> autoSettings for add-ons

### class `AutoSettings`(AutoPropertyObject)  — `ALL`  *(line 25)*

  An AutoSettings instance is used to simplify the load/save of user config for NVDA extensions
  (Synth drivers, braille drivers, vision providers) and make it possible to automatically provide a
  standard GUI for these settings.
  Derived classes must implement:
  - getId
  - getDisplayName
  - _get_supportedSettings

### `AutoSettings.__del__`(self)  — `ALL`  *(line 42)*

### `AutoSettings.__init__`(self)  — `ALL`  *(line 35)*

  Perform any initialisation
  @note: registers with the config save action extension point

### @classmethod `AutoSettings._getConfigSection`(cls) -> str  — `ALL`  *(line 74)*

  @return: The section of the config that these settings belong in.

### @classmethod `AutoSettings._getConfigSpecForSettings`(cls, settings: SupportedSettingType) -> dict[str, str]  — `changed 2026.1+`  *(line 129)*

  **Signature history:**
  - **2023.3:** `AutoSettings._getConfigSpecForSettings(cls, settings: SupportedSettingType) -> Dict`
  - **2026.1+:** `AutoSettings._getConfigSpecForSettings(cls, settings: SupportedSettingType) -> dict[str, str]`

### `AutoSettings._get_supportedSettings`(self) -> SupportedSettingType  — `ALL`  *(line 117)*

  The settings supported by the AutoSettings instance. Abstract.

### @classmethod `AutoSettings._initSpecificSettings`(cls, clsOrInst: Any, settings: SupportedSettingType) -> None  — `ALL`  *(line 81)*

### @classmethod `AutoSettings._loadSpecificSettings`(cls, clsOrInst: Any, settings: SupportedSettingType, onlyChanged: bool=False) -> None  — `ALL`  *(line 182)*

  Load settings from config, set them on `clsOrInst`.
  @param clsOrInst: Destination for the values.
  @param settings: The settings to load.
  @param onlyChanged: When True, only settings that no longer match the config are set.
  @note: attributes are set on clsOrInst using setattr.
          The id of each setting in `settings` is used as the attribute name.

### @classmethod `AutoSettings._paramToPercent`(cls, current: int, min: int, max: int) -> int  — `ALL`  *(line 232)*

  Convert a raw parameter value to a percentage given the current, minimum and maximum raw values.
  @param current: The current value.
  @param min: The minimum value.
  @param max: The maximum value.

### @classmethod `AutoSettings._percentToParam`(cls, percent: int, min: int, max: int) -> int  — `ALL`  *(line 241)*

  Convert a percentage to a raw parameter value given the current percentage and the minimum and maximum
  raw parameter values.
  @param percent: The current percentage.
  @param min: The minimum raw parameter value.
  @param max: The maximum raw parameter value.

### `AutoSettings._registerConfigSaveAction`(self)  — `ALL`  *(line 45)*

  Overrideable pre_configSave registration

### @classmethod `AutoSettings._saveSpecificSettings`(cls, clsOrInst: Any, settings: SupportedSettingType) -> None  — `ALL`  *(line 145)*

  Save values for settings to config.
  The values from the attributes of `clsOrInst` that match the `id` of each setting are saved to config.
  @param clsOrInst: Destination for the values.
  @param settings: The settings to load.

### `AutoSettings._unregisterConfigSaveAction`(self)  — `ALL`  *(line 50)*

  Overrideable pre_configSave de-registration

### `AutoSettings.getConfigSpec`(self)  — `ALL`  *(line 141)*

### @classmethod `AutoSettings.getDisplayName`(cls) -> str  — `ALL`  *(line 65)*

  @return: The translated name for this collection of settings. This is for use in the GUI to represent the
  group of these settings.

### @classmethod `AutoSettings.getId`(cls) -> str  — `ALL`  *(line 56)*

  @return: Application friendly name, should be globally unique, however since this is used in the config file
  human readable is also beneficial.

### `AutoSettings.initSettings`(self)  — `ALL`  *(line 105)*

  Initializes the configuration for this AutoSettings instance.
  This method is called when initializing the AutoSettings instance.

### `AutoSettings.isSupported`(self, settingID) -> bool  — `ALL`  *(line 121)*

  Checks whether given setting is supported by the AutoSettings instance.

### `AutoSettings.loadSettings`(self, onlyChanged: bool=False)  — `ALL`  *(line 221)*

  Loads settings for this AutoSettings instance from the configuration.
  This method assumes that the instance has attributes o/properties
  corresponding with the name of every setting in L{supportedSettings}.
  @param onlyChanged: When loading settings, only apply those for which
          the value in the configuration differs from the current value.

### `AutoSettings.saveSettings`(self)  — `ALL`  *(line 173)*

  Saves the current settings for the AutoSettings instance to the configuration.
  This method is also executed when the AutoSettings instance is loaded for the first time,
  in order to populate the configuration with the initial settings..

---

## `autoSettingsUtils/driverSetting.py`

> Classes used to represent settings for Drivers and other AutoSettings instances
> 
> Naming of these classes is historical, kept for backwards compatibility purposes.

### class `BooleanDriverSetting`(DriverSetting)  — `ALL`  *(line 127)*

  Represents a boolean driver setting such as rate boost or automatic time sync.
  GUI representation is a wx.Checkbox

### `BooleanDriverSetting.__init__`(self, id: str, displayNameWithAccelerator: str, availableInSettingsRing: bool=False, displayName: Optional[str]=None, defaultVal: bool=False, useConfig: bool=True)  — `ALL`  *(line 134)*

  @param defaultVal: Specifies the default value for a boolean driver setting.

### `BooleanDriverSetting._get_configSpec`(self)  — `ALL`  *(line 155)*

### class `DriverSetting`(AutoPropertyObject)  — `ALL`  *(line 16)*

  As a base class, represents a setting to be shown in GUI and saved to config.
  
  GUI representation is a string selection GUI control, a wx.Choice control.
  
  Used for synthesizer or braille display setting such as voice, variant or dot firmness as
  well as for settings in Vision Providers

### `DriverSetting.__init__`(self, id: str, displayNameWithAccelerator: str, availableInSettingsRing: bool=False, defaultVal: object=None, displayName: Optional[str]=None, useConfig: bool=True)  — `ALL`  *(line 41)*

  @param id: internal identifier of the setting
          If this starts with a `_`, it will not be shown in the settings GUI.
  @param displayNameWithAccelerator: the localized string shown in voice or braille settings dialog
  @param availableInSettingsRing: Will this option be available in a settings ring?
  @param defaultVal: Specifies the default value for a driver setting.
  @param displayName: the localized string used in synth settings ring or
          None to use displayNameWithAccelerator
  @param useConfig: Whether the value of this option is loaded from and saved to NVDA's configuration.
          Set this to C{False} if the driver deals with loading and saving.

### `DriverSetting._get_configSpec`(self)  — `ALL`  *(line 35)*

  Returns the configuration specification of this particular setting for config file validator.
  @rtype: str

### class `NumericDriverSetting`(DriverSetting)  — `ALL`  *(line 72)*

  Represents a numeric driver setting such as rate, volume, pitch or dot firmness.
  GUI representation is a slider control.

### `NumericDriverSetting.__init__`(self, id, displayNameWithAccelerator, availableInSettingsRing=False, defaultVal: int=50, minVal: int=0, maxVal: int=100, minStep: int=1, normalStep: int=5, largeStep: int=10, displayName: Optional[str]=None, useConfig: bool=True)  — `ALL`  *(line 86)*

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

### `NumericDriverSetting._get_configSpec`(self)  — `ALL`  *(line 79)*

---

## `baseObject.py`

> Contains the base classes that many of NVDA's classes such as NVDAObjects, virtualBuffers, appModules, synthDrivers inherit from. These base classes provide such things as auto properties, and methods and properties for scripting and key binding.

### class `AutoPropertyObject`(garbageHandler.TrackedObject)  — `ALL`  *(line 123)*

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

### `AutoPropertyObject.__new__`(cls, *args, **kwargs)  — `ALL`  *(line 150)*

### `AutoPropertyObject._getPropertyViaCache`(self, getterMethod: Optional[GetterMethodT]=None) -> GetterReturnT  — `ALL`  *(line 158)*

### `AutoPropertyObject.invalidateCache`(self)  — `ALL`  *(line 171)*

### @classmethod `AutoPropertyObject.invalidateCaches`(cls)  — `ALL`  *(line 175)*

  Invalidate the caches for all current instances.

### class `AutoPropertyType`(ABCMeta)  — `ALL`  *(line 62)*

### `AutoPropertyType.__init__`(**kwargs: Any)  — `changed 2025.3`  *(line 63)*

  **Signature history:**
  - **2023.3:** `AutoPropertyType.__init__(self, name, bases, dict)`
  - **2025.3:** `AutoPropertyType.__init__(**kwargs: Any)`

### class `CachingGetter`(Getter)  — `ALL`  *(line 48)*

### `CachingGetter.__get__`(self, instance: Union[Any, None, 'AutoPropertyObject'], owner) -> Union[GetterReturnT, 'CachingGetter']  — `ALL`  *(line 49)*

### class `Getter`(object)  — `ALL`  *(line 24)*

### `Getter.__get__`(self, instance: Union[Any, None, 'AutoPropertyObject'], owner) -> Union[GetterReturnT, 'Getter']  — `ALL`  *(line 30)*

### `Getter.__init__`(self, fget, abstract=False)  — `ALL`  *(line 25)*

### `Getter.deleter`(self, func)  — `ALL`  *(line 44)*

### `Getter.setter`(self, func)  — `ALL`  *(line 41)*

### class `ScriptableObject`(AutoPropertyObject)  — `ALL`  *(line 208)*

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

### `ScriptableObject.__init__`(self)  — `ALL`  *(line 223)*

### `ScriptableObject.bindGesture`(self, gestureIdentifier, scriptName)  — `ALL`  *(line 241)*

  Bind an input gesture to a script.
  @param gestureIdentifier: The identifier of the input gesture.
  @type gestureIdentifier: str
  @param scriptName: The name of the script, which is the name of the method excluding the C{script_} prefix.
  @type scriptName: str
  @raise LookupError: If there is no script with the provided name.

### `ScriptableObject.bindGestures`(self, gestureMap)  — `ALL`  *(line 281)*

  Bind or unbind multiple input gestures.
  This is a convenience method which simply calls L{bindGesture} for each gesture and script pair, logging any errors.
  For the case where script is None, L{removeGestureBinding} is called instead.
  @param gestureMap: A mapping of gesture identifiers to script names.
  @type gestureMap: dict of str to str

### `ScriptableObject.clearGestureBindings`(self)  — `ALL`  *(line 277)*

  Remove all input gesture bindings from this object.

### `ScriptableObject.getScript`(self, gesture)  — `ALL`  *(line 300)*

  Retrieve the script bound to a given gesture.
  @param gesture: The input gesture in question.
  @type gesture: L{inputCore.InputGesture}
  @return: The script function or C{None} if none was found.
  @rtype: script function

### `ScriptableObject.removeGestureBinding`(self, gestureIdentifier)  — `ALL`  *(line 265)*

  Removes the binding for the given gesture identifier if a binding exists.
  @param gestureIdentifier: The identifier of the input gesture.
  @type gestureIdentifier: str
  @raise LookupError: If there is no binding for this gesture

### class `ScriptableType`(AutoPropertyType)  — `ALL`  *(line 183)*

  A metaclass used for collecting and caching gestures on a ScriptableObject

### `ScriptableType.__new__`(**kwargs: Any)  — `changed 2025.3`  *(line 186)*

  **Signature history:**
  - **2023.3:** `ScriptableType.__new__(meta, name, bases, dict)`
  - **2025.3:** `ScriptableType.__new__(**kwargs: Any)`

---

## `config/__init__.py`

> Manages NVDA configuration.
> The heart of NVDA's configuration is Configuration Manager, which records current options, profile information and functions to load, save, and switch amongst configuration profiles.
> In addition, this module provides three actions: profile switch notifier, an action to be performed when NVDA saves settings, and action to be performed when NVDA is asked to reload configuration from disk or reset settings to factory defaults.
> For the latter two actions, one can perform actions prior to and/or after they take place.

### class `AggregatedSection`  — `ALL`  *(line 1045)*

  A view of a section of configuration which aggregates settings from all active profiles.

### `AggregatedSection.__contains__`(self, key)  — `ALL`  *(line 1133)*

### `AggregatedSection.__getitem__`(self, key: aggregatedSection._cacheKeyT, checkValidity: bool=True)  — `ALL`  *(line 1069)*

### `AggregatedSection.__init__`(self, manager: ConfigManager, path: Tuple[str], spec: ConfigObj, profiles: List[ConfigObj])  — `ALL`  *(line 1050)*

### `AggregatedSection.__iter__`(self)  — `ALL`  *(line 1166)*

### `AggregatedSection.__setitem__`(self, key: aggregatedSection._cacheKeyT, val: aggregatedSection._cacheValueT)  — `ALL`  *(line 1211)*

### `AggregatedSection._cacheLeaf`(self, key, spec, val)  — `ALL`  *(line 1159)*

### `AggregatedSection._getUpdateSection`(self)  — `ALL`  *(line 1305)*

### @staticmethod `AggregatedSection._isSection`(val: Any) -> bool  — `ALL`  *(line 1065)*

  Checks if a given value or spec is a section of a config profile.

### `AggregatedSection._linkDeprecatedValues`(self, key: aggregatedSection._cacheKeyT, val: aggregatedSection._cacheValueT)  — `since 2024.4`  *(line 1270)*

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

### `AggregatedSection.copy`(self)  — `ALL`  *(line 1191)*

### `AggregatedSection.dict`(self)  — `ALL`  *(line 1194)*

  Return a deepcopy of self as a dictionary.
  Adapted from L{configobj.Section.dict}.

### `AggregatedSection.get`(self, key, default=None)  — `ALL`  *(line 1140)*

### `AggregatedSection.isSet`(self, key)  — `ALL`  *(line 1146)*

  Check whether a given key has been explicitly set.
  This is sometimes useful because it can return C{False} even if there is a default for the key.
  @return: C{True} if the key has been explicitly set, C{False} if not.
  @rtype: bool

### `AggregatedSection.items`(self)  — `ALL`  *(line 1183)*

### `AggregatedSection.spec`(self, val)  — `ALL`  *(line 1330)*

### class `AllowUiaInChromium`(Enum)  — `ALL`  *(line 1408)*

### @staticmethod `AllowUiaInChromium.getConfig`() -> 'AllowUiaInChromium'  — `ALL`  *(line 1415)*

### class `AllowUiaInMSWord`(Enum)  — `ALL`  *(line 1422)*

### @staticmethod `AllowUiaInMSWord.getConfig`() -> 'AllowUiaInMSWord'  — `ALL`  *(line 1429)*

### class `ConfigManager`(object)  — `ALL`  *(line 464)*

  Manages and provides access to configuration.
  In addition to the base configuration, there can be multiple active configuration profiles.
  Settings in more recently activated profiles take precedence,
  with the base configuration being consulted last.
  This allows a profile to override settings in profiles activated earlier and the base configuration.
  A profile need only include a subset of the available settings.
  Changed settings are written to the most recently activated profile.

### `ConfigManager.__contains__`(self, key)  — `ALL`  *(line 608)*

### `ConfigManager.__getitem__`(self, key)  — `ALL`  *(line 602)*

### `ConfigManager.__init__`(self)  — `ALL`  *(line 489)*

### `ConfigManager.__setitem__`(self, key, val)  — `ALL`  *(line 614)*

### `ConfigManager._getConfigValidation`(self, spec)  — `ALL`  *(line 1004)*

  returns a tuple with the spec for the config spec:
  ("type", [], {}, "default value") EG:
  - (u'boolean', [], {}, u'false')
  - (u'integer', [], {'max': u'255', 'min': u'1'}, u'192')
  - (u'option', [u'changedContext', u'fill', u'scroll'], {}, u'changedContext')

### `ConfigManager._getProfile`(self, name, load=True)  — `ALL`  *(line 634)*

### `ConfigManager._getProfileFn`(self, name: str) -> str  — `ALL`  *(line 631)*

### `ConfigManager._getSpecFromKeyPath`(self, keyPath)  — `ALL`  *(line 995)*

### `ConfigManager._handleProfileSwitch`(self, shouldNotify=True)  — `ALL`  *(line 514)*

### `ConfigManager._initBaseConf`(self, factoryDefaults=False)  — `ALL`  *(line 528)*

### `ConfigManager._loadConfig`(self, fn, fileError=False)  — `ALL`  *(line 571)*

### `ConfigManager._loadProfileTriggers`(self)  — `ALL`  *(line 967)*

### `ConfigManager._markWriteProfileDirty`(self)  — `ALL`  *(line 679)*

### `ConfigManager._triggerProfileEnter`(self, trigger)  — `ALL`  *(line 855)*

  Called by L{ProfileTrigger.enter}}}.

### `ConfigManager._triggerProfileExit`(self, trigger)  — `ALL`  *(line 878)*

  Called by L{ProfileTrigger.exit}}}.

### `ConfigManager._writeProfileToFile`(self, filename, profile)  — `ALL`  *(line 685)*

### `ConfigManager.atomicProfileSwitch`(self)  — `ALL`  *(line 905)*

  Indicate that multiple profile switches should be treated as one.
  This is useful when multiple triggers may be exited/entered at once;
  e.g. when switching applications.
  While multiple switches aren't harmful, they might take longer;
  e.g. unnecessarily switching speech synthesizers or braille displays.
  This is a context manager to be used with the C{with} statement.

### `ConfigManager.createProfile`(self, name)  — `ALL`  *(line 725)*

  Create a profile.
  @param name: The name of the profile to create.
  @type name: str
  @raise ValueError: If a profile with this name already exists.

### `ConfigManager.deleteProfile`(self, name)  — `ALL`  *(line 747)*

  Delete a profile.
  @param name: The name of the profile to delete.
  @type name: str
  @raise LookupError: If the profile doesn't exist.

### `ConfigManager.dict`(self)  — `ALL`  *(line 617)*

### `ConfigManager.disableProfileTriggers`(self)  — `ALL`  *(line 946)*

  Temporarily disable all profile triggers.
  Any triggered profiles will be deactivated and subsequent triggers will not apply.
  Call L{enableTriggers} to re-enable triggers.

### `ConfigManager.enableProfileTriggers`(self)  — `ALL`  *(line 963)*

  Re-enable profile triggers after they were previously disabled.

### `ConfigManager.get`(self, key, default=None)  — `ALL`  *(line 611)*

### `ConfigManager.getConfigValidation`(self, keyPath)  — `ALL`  *(line 1013)*

  Get a config validation details
  This can be used to get a L{ConfigValidationData} containing the type, default, options list, or
  other validation parameters (min, max, etc) for a config key.
  @param keyPath: a sequence of the identifiers leading to the config key. EG ("braille", "messageTimeout")
  @return ConfigValidationData

### `ConfigManager.getProfile`(self, name)  — `ALL`  *(line 650)*

  Get a profile given its name.
  This is useful for checking whether a profile has been manually activated or triggered.
  @param name: The name of the profile.
  @type name: str
  @return: The profile object.
  @raise KeyError: If the profile is not loaded.

### `ConfigManager.listProfiles`(self)  — `ALL`  *(line 620)*

### `ConfigManager.manualActivateProfile`(self, name)  — `ALL`  *(line 660)*

  Manually activate a profile.
  Only one profile can be manually active at a time.
  If another profile was manually activated, deactivate it first.
  If C{name} is C{None}, a profile will not be activated.
  @param name: The name of the profile or C{None} for no profile.
  @type name: str

### `ConfigManager.renameProfile`(self, oldName, newName)  — `ALL`  *(line 801)*

  Rename a profile.
  @param oldName: The current name of the profile.
  @type oldName: str
  @param newName: The new name for the profile.
  @type newName: str
  @raise LookupError: If the profile doesn't exist.
  @raise ValueError: If a profile with the new name already exists.

### `ConfigManager.reset`(self, factoryDefaults=False)  — `ALL`  *(line 711)*

  Reset the configuration to saved settings or factory defaults.
  @param factoryDefaults: C{True} to reset to factory defaults, C{False} to reset to saved configuration.
  @type factoryDefaults: bool

### `ConfigManager.resumeProfileTriggers`(self)  — `ALL`  *(line 932)*

  Resume handling of profile triggers after previous suspension.
  Any trigger enters or exits that occurred while triggers were suspended will be applied.
  Trigger handling will then return to normal.
  @see: L{suspendTriggers}

### `ConfigManager.save`(self)  — `ALL`  *(line 689)*

  Save all modified profiles and the base configuration to disk.

### `ConfigManager.saveProfileTriggers`(self)  — `ALL`  *(line 984)*

  Save profile trigger information to disk.
  This should be called whenever L{profilesToTriggers} is modified.

### `ConfigManager.suspendProfileTriggers`(self)  — `ALL`  *(line 922)*

  Suspend handling of profile triggers.
  Any triggers that currently apply will continue to apply.
  Subsequent enters or exits will not apply until triggers are resumed.
  @see: L{resumeTriggers}

### class `ConfigValidationData`(object)  — `ALL`  *(line 1029)*

### `ConfigValidationData.__init__`(self, validationFuncName)  — `ALL`  *(line 1032)*

### class `ProfileTrigger`(object)  — `ALL`  *(line 1337)*

  A trigger for automatic activation/deactivation of a configuration profile.
  The user can associate a profile with a trigger.
  When the trigger applies, the associated profile is activated.
  When the trigger no longer applies, the profile is deactivated.
  L{spec} is a string used to search for this trigger and must be implemented.
  To signal that this trigger applies, call L{enter}.
  To signal that it no longer applies, call L{exit}.
  Alternatively, you can use this object as a context manager via the with statement;
  i.e. this trigger will apply only inside the with block.

### `ProfileTrigger.__exit__`(self, excType, excVal, traceback)  — `ALL`  *(line 1404)*

### `ProfileTrigger.enter`(self)  — `ALL`  *(line 1371)*

  Signal that this trigger applies.
  The associated profile (if any) will be activated.

### `ProfileTrigger.exit`(self)  — `ALL`  *(line 1390)*

  Signal that this trigger no longer applies.
  The associated profile (if any) will be deactivated.

### @property `ProfileTrigger.hasProfile`(self)  — `ALL`  *(line 1365)*

  Whether this trigger has an associated profile.
  @rtype: bool

### `ProfileTrigger.spec`(self)  — `ALL`  *(line 1357)*

  The trigger specification.
  This is a string used to search for this trigger in the user's configuration.
  @rtype: str

### class `RegistryKey`(str, Enum)  — `until 2024.4`  *(line 125)*

### `__getattr__`(attrName: str) -> Any  — `ALL`  *(line 80)*

  Module level `__getattr__` used to preserve backward compatibility.

### `_prepareToCopyAddons`(fromPath: str, toPath: str, addonDirs: list[str], addonsToCopy: Collection[str])  — `since 2026.1+`  *(line 378)*

  Determine which add-on directories to copy to the system profile, and create the appropriate addonsState file.
  
  .. Note::
          While this function returns ``None``, it has two major side-effects:
          1. The ``addonDirs`` list is mutated to contain only the add-ons that should be copied.
          2. A new `addonsState.pickle` is created in ``toPath``.
  
  :param fromPath: Root of the source configuration directory.
  :param toPath: Root of the destination configuration directory
  :param addonDirs: Subdirectories of ``addons/`` in ``fromPath``.
          This will be mutated to only contain the add-ons that should be copied.
  :param addonsToCopy: Add-on IDs of the add-ons that should be copied.

### `_setStartOnLogonScreen`(enable: bool) -> None  — `ALL`  *(line 303)*

### `_setSystemConfig`(fromPath: str, *, prefix: str=sys.prefix, addonsToCopy: Collection[str]=())  — `changed 2026.1+`  *(line 338)*

  **Signature history:**
  - **2023.3:** `_setSystemConfig(fromPath)`
  - **2026.1+:** `_setSystemConfig(fromPath: str, *, prefix: str=sys.prefix, addonsToCopy: Collection[str]=())`

### `_transformSpec`(spec: ConfigObj)  — `ALL`  *(line 448)*

  To make the spec less verbose, transform the spec:
  - Add default="default" to all featureFlag items. This is required so that the key can be read,
  even if it is missing from the config.

### `getInstalledUserConfigPath`() -> Optional[str]  — `ALL`  *(line 171)*

### `getScratchpadDir`(ensureExists: bool=False) -> str  — `ALL`  *(line 226)*

  Returns the path where custom appModules, globalPlugins and drivers can be placed while being developed.

### `getStartAfterLogon`() -> bool  — `ALL`  *(line 270)*

  Not to be confused with getStartOnLogonScreen.
  
  Checks if NVDA is set to start after a logon.
  Checks related easeOfAccess current user registry keys.

### `getStartOnLogonScreen`() -> bool  — `ALL`  *(line 293)*

  Not to be confused with getStartAfterLogon.
  
  Checks if NVDA is set to start on the logon screen.
  
  Checks related easeOfAccess local machine registry keys.

### `getUserDefaultConfigPath`(useInstalledPathIfExists=False)  — `ALL`  *(line 195)*

  Get the default path for the user configuration directory.
  This is the default path and doesn't reflect overriding from the command line,
  which includes temporary copies.
  Most callers will want the C{NVDAState.WritePaths.configDir variable} instead.

### `initConfigPath`(configPath: Optional[str]=None) -> None  — `ALL`  *(line 239)*

  Creates the current configuration path if it doesn't exist. Also makes sure that various sub directories also exist.
  @param configPath: an optional path which should be used instead (only useful when being called from outside of NVDA)

### `initialize`()  — `ALL`  *(line 110)*

### `isInstalledCopy`() -> bool  — `ALL`  *(line 127)*

  Checks to see if this running copy of NVDA is installed on the system

### `saveOnExit`()  — `ALL`  *(line 115)*

  Save the configuration if configured to save on exit.
  This should only be called if NVDA is about to exit.
  Errors are ignored.

### `setStartAfterLogon`(enable: bool) -> None  — `ALL`  *(line 279)*

  Not to be confused with setStartOnLogonScreen.
  
  Toggle if NVDA automatically starts after a logon.
  Sets easeOfAccess related registry keys.

### `setStartOnLogonScreen`(enable: bool) -> None  — `ALL`  *(line 415)*

  Not to be confused with setStartAfterLogon.
  
  Toggle whether NVDA starts on the logon screen automatically.
  On failure to set, retries with escalated permissions.
  
  Raises a RuntimeError on failure.

### `setSystemConfigToCurrentConfig`(*, addonsToCopy: Collection[str]=())  — `changed 2026.1+`  *(line 307)*

  **Signature history:**
  - **2023.3:** `setSystemConfigToCurrentConfig()`
  - **2026.1+:** `setSystemConfigToCurrentConfig(*, addonsToCopy: Collection[str]=())`

  Replaces the system configuration with the current user configuration.
  
  :param addonsToCopy: IDs of the add-ons to copy, defaults to ()
          .. warning::
                  Only enabled add-ons should be copied.
                  Providing IDs of add-ons that are disabled may cause unexpected results,
                  especially for add-ons that are not compatible with the current API version.
  :raises installer.RetriableFailure: If copying the user to the system config fails.
  :raises RuntimeError: If calling ``nvda_slave`` fails for some other reason.

---

## `config/configFlags.py`

> Flags used to define the possible values for an option in the configuration.
> Use Flag.MEMBER.value to set a new value or compare with an option in the config;
> use Flag.MEMBER.displayString in the UI for a translatable description of this member.
> 
> When creating new parameter options, consider using F{FeatureFlag} which explicitely defines
> the default value.

### class `AddonsAutomaticUpdate`(DisplayStringStrEnum)  — `since 2024.4`  *(line 278)*

### @property `AddonsAutomaticUpdate._displayStringLabels`(self)  — `since 2024.4`  *(line 284)*

### class `BrailleMode`(DisplayStringStrEnum)  — `since 2024.4`  *(line 132)*

  Enumeration containing the possible config values for "Braille mode" option in braille settings.
  Use BrailleMode.MEMBER.value to compare with the config;
  use BrailleMode.MEMBER.displayString in the UI for a translatable description of this member.

### @property `BrailleMode._displayStringLabels`(self) -> dict['BrailleMode', str]  — `since 2024.4`  *(line 142)*

### class `LoggingLevel`(DisplayStringIntEnum)  — `since 2026.1+`  *(line 412)*

  Enumeration containing the possible logging levels.
  
  Use LoggingLevel.MEMBER.value to compare with the config;
  use LoggingLevel.MEMBER.displayString in the UI for a translatable description of this member.

### @property `LoggingLevel._displayStringLabels`(self) -> dict[int, str]  — `since 2026.1+`  *(line 426)*

### class `NVDAKey`(DisplayStringIntFlag)  — `ALL`  *(line 29)*

  IntFlag enumeration containing the possible config values for "Select NVDA Modifier Keys" option in
  keyboard settings.
  
  Use NVDAKey.MEMBER.value to compare with the config;
  the config stores a bitwise combination of one or more of these values.
  use NVDAKey.MEMBER.displayString in the UI for a translatable description of this member.

### @property `NVDAKey._displayStringLabels`(self)  — `ALL`  *(line 43)*

### class `OutputMode`(DisplayStringIntFlag)  — `since 2024.4`  *(line 297)*

  Enumeration for ways to output information, such as formatting.
  Use OutputMode.MEMBER.value to compare with the config;
  use OutputMode.MEMBER.displayString in the UI for a translatable description of this member.

### @property `OutputMode._displayStringLabels`(self)  — `since 2024.4`  *(line 309)*

### class `ParagraphStartMarker`(DisplayStringStrEnum)  — `since 2024.4`  *(line 322)*

### @property `ParagraphStartMarker._displayStringLabels`(self)  — `since 2024.4`  *(line 328)*

### class `PlayErrorSound`(DisplayStringIntEnum)  — `since 2026.1+`  *(line 442)*

  Enumeration containing the possible config values to play a sound when an error is logged, depending on
  NVDA version type.
  
  Use PlayErrorSound.MEMBER.value to compare with the config;
  use PlayErrorSound.MEMBER.displayString in the UI for a translatable description of this member.

### @property `PlayErrorSound._displayStringLabels`(self)  — `since 2026.1+`  *(line 455)*

### class `RemoteConnectionMode`(DisplayStringIntEnum)  — `since 2025.3`  *(line 359)*

  Enumeration containing the possible remote connection modes (roles for connected clients).
  
  Use RemoteConnectionMode.MEMBER.value to compare with the config;
  use RemoteConnectionMode.MEMBER.displayString in the UI for a translatable description of this member.
  
  Note: This datatype has been chosen as it may be desireable to implement further roles in future.
  For instance, an "observer" role, which is neither controlling or controlled, but which allows the user to listen to the other computers in the channel.

### @property `RemoteConnectionMode._displayStringLabels`(self)  — `since 2025.3`  *(line 373)*

### `RemoteConnectionMode.toConnectionMode`(self) -> '_remoteClient.connectionInfo.ConnectionMode'  — `since 2025.3`  *(line 381)*

### class `RemoteServerType`(DisplayStringFlag)  — `since 2025.3`  *(line 392)*

  Enumeration containing the possible types of Remote relay server.
  
  Use RemoteServerType.MEMBER.value to compare with the config;
  use RemoteServerType.MEMBER.displayString in the UI for a translatable description of this member.

### @property `RemoteServerType._displayStringLabels`(self)  — `since 2025.3`  *(line 403)*

### class `ReportCellBorders`(DisplayStringIntEnum)  — `ALL`  *(line 252)*

  Enumeration containing the possible config values to report cell borders.
  
  Use ReportCellBorders.MEMBER.value to compare with the config;
  use ReportCellBorders.MEMBER.displayString in the UI for a translatable description of this member.

### @property `ReportCellBorders._displayStringLabels`(self)  — `ALL`  *(line 264)*

### class `ReportLineIndentation`(DisplayStringIntEnum)  — `ALL`  *(line 152)*

  Enumeration containing the possible config values to report line indent.
  
  Use ReportLineIndentation.MEMBER.value to compare with the config;
  use ReportLineIndentation.MEMBER.displayString in the UI for a translatable description of this member.

### @property `ReportLineIndentation._displayStringLabels`(self)  — `ALL`  *(line 165)*

### class `ReportNotSupportedLanguage`(DisplayStringStrEnum)  — `since 2025.3`  *(line 341)*

### @property `ReportNotSupportedLanguage._displayStringLabels`(self) -> dict['ReportNotSupportedLanguage', str]  — `since 2025.3`  *(line 347)*

### class `ReportSpellingErrors`(DisplayStringIntFlag)  — `since 2026.1+`  *(line 185)*

  IntFlag enumeration containing the possible config values to report spelling errors while reading.
  
  Use ReportSpellingErrors.MEMBER.value to compare with the config;
  the config stores a bitwise combination of zero, one or more of these values.
  Use ReportSpellingErrors.MEMBER.displayString in the UI for a translatable description of this member.

### @property `ReportSpellingErrors._displayStringLabels`(self) -> dict['ReportSpellingErrors', str]  — `since 2026.1+`  *(line 200)*

### class `ReportTableHeaders`(DisplayStringIntEnum)  — `ALL`  *(line 221)*

  Enumeration containing the possible config values to report table headers.
  
  Use ReportTableHeaders.MEMBER.value to compare with the config;
  use ReportTableHeaders.MEMBER.displayString in the UI for a translatable description of this member.

### @property `ReportTableHeaders._displayStringLabels`(self)  — `ALL`  *(line 234)*

### class `ShowMessages`(DisplayStringIntEnum)  — `ALL`  *(line 79)*

  Enumeration containing the possible config values for "Show messages" option in braille settings.
  
  Use ShowMessages.MEMBER.value to compare with the config;
  use ShowMessages.MEMBER.displayString in the UI for a translatable description of this member.

### @property `ShowMessages._displayStringLabels`(self)  — `ALL`  *(line 91)*

### class `TetherTo`(DisplayStringStrEnum)  — `ALL`  *(line 106)*

  Enumeration containing the possible config values for "Tether to" option in braille settings.
  
  Use TetherTo.MEMBER.value to compare with the config;
  use TetherTo.MEMBER.displayString in the UI for a translatable description of this member.

### @property `TetherTo._displayStringLabels`(self)  — `ALL`  *(line 118)*

### class `TypingEcho`(DisplayStringIntEnum)  — `since 2025.3`  *(line 55)*

  Enumeration containing the possible config values for typing echo (characters and words).
  
  Use TypingEcho.MEMBER.value to compare with the config;
  use TypingEcho.MEMBER.displayString in the UI for a translatable description of this member.

### @property `TypingEcho._displayStringLabels`(self)  — `since 2025.3`  *(line 67)*

---

## `driverHandler.py`

> Handler for driver functionality that is global to synthesizers and braille displays.

### class `Driver`(AutoSettings)  — `ALL`  *(line 12)*

  Abstract base class for drivers, such as speech synthesizer and braille display drivers.
  Abstract subclasses such as L{braille.BrailleDisplayDriver} should set L{_configSection}.
  
  At a minimum, drivers must set L{name} and L{description} and override the L{check} method.
  
  L{supportedSettings} should be set as appropriate for the settings supported by the driver.
  Each setting is retrieved and set using attributes named after the setting;
  e.g. the L{dotFirmness} attribute is used for the L{dotFirmness} setting.
  These will usually be properties.

### `Driver.__init__`(self)  — `ALL`  *(line 35)*

  Initialize this driver.
  This method can also set default settings for the driver.
  @raise Exception: If an error occurs.
  @postcondition: This driver can be used.

### `Driver.__repr__`(self)  — `ALL`  *(line 52)*

### @classmethod `Driver._getConfigSection`(cls) -> str  — `ALL`  *(line 75)*

### @classmethod `Driver.check`(cls)  — `ALL`  *(line 56)*

  Determine whether this driver is available.
  The driver will be excluded from the list of available drivers if this method returns C{False}.
  For example, if a speech synthesizer requires installation and it is not installed, C{False} should be returned.
  @return: C{True} if this driver is available, C{False} if not.
  @rtype: bool

### @classmethod `Driver.getDisplayName`(cls) -> str  — `ALL`  *(line 71)*

### @classmethod `Driver.getId`(cls) -> str  — `ALL`  *(line 67)*

### `Driver.terminate`(self)  — `ALL`  *(line 43)*

  Save settings and terminate this driver.
  This should be used for any required clean up.
  @precondition: L{initialize} has been called.
  @postcondition: This driver can no longer be used.

---

## `extensionPoints/__init__.py`

> Framework to enable extensibility at specific points in the code.
> This allows interested parties to register to be notified when some action occurs
> or to modify a specific kind of data.
> For example, you might wish to notify about a configuration profile switch
> or allow modification of spoken messages before they are passed to the synthesizer.
> See the L{Action}, L{Filter}, L{Decider} and L{AccumulatingDecider} classes.

### class `AccumulatingDecider`(HandlerRegistrar[Callable[..., bool]])  — `ALL`  *(line 174)*

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

### `AccumulatingDecider.__init__`(self, defaultDecision: bool) -> None  — `ALL`  *(line 206)*

### `AccumulatingDecider.decide`(self, **kwargs) -> bool  — `ALL`  *(line 210)*

  Call handlers to make a decision.
  Results returned from all handlers are collected
  and if at least one handler returns value different than the one specifed as default it is returned.
  If there are no handlers or all handlers return the default value, the default value is returned.
  @param kwargs: Arguments to pass to the handlers.
  @return: The decision.

### class `Action`(HandlerRegistrar[Callable[..., None]])  — `ALL`  *(line 27)*

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

### `Action.notify`(self, **kwargs)  — `ALL`  *(line 50)*

  Notify all registered handlers that the action has occurred.
  @param kwargs: Arguments to pass to the handlers.

### `Action.notifyOnce`(self, **kwargs)  — `ALL`  *(line 60)*

  Notify all registered handlers that the action has occurred.
  Unregister handlers after calling.
  @param kwargs: Arguments to pass to the handlers.

### class `Chain`(HandlerRegistrar[Callable[..., Iterable[ChainValueTypeT]]], Generic[ChainValueTypeT])  — `ALL`  *(line 233)*

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

### `Chain.iter`(self, **kwargs) -> Generator[ChainValueTypeT, None, None]  — `ALL`  *(line 265)*

  Returns a generator yielding all values generated by the registered handlers.
  @param kwargs: Arguments to pass to the handlers.

### class `Decider`(HandlerRegistrar[Callable[..., bool]])  — `ALL`  *(line 123)*

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

### `Decider.decide`(self, **kwargs)  — `ALL`  *(line 154)*

  Call handlers to make a decision.
  If a handler returns False, processing stops
  and False is returned.
  If there are no handlers or all handlers return True, True is returned.
  @param kwargs: Arguments to pass to the handlers.
  @return: The decision.
  @rtype: bool

### class `Filter`(HandlerRegistrar[Union[Callable[..., FilterValueT], Callable[[FilterValueT], FilterValueT]]], Generic[FilterValueT])  — `ALL`  *(line 77)*

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

### `Filter.apply`(self, value: FilterValueT, **kwargs) -> FilterValueT  — `ALL`  *(line 105)*

  Pass a value to be filtered through all registered handlers.
  The value is passed to the first handler
  and the return value from that handler is passed to the next handler.
  This process continues for all handlers until the final handler.
  The return value from the final handler is returned to the caller.
  @param value: The value to be filtered.
  @param kwargs: Arguments to pass to the handlers.
  @return: The filtered value.

---

## `extensionPoints/util.py`

> Utilities used withing the extension points framework. Generally it is expected that the class in __init__.py are
> used, however for more advanced requirements these utilities can be used directly.

### class `AnnotatableWeakref`(weakref.ref, Generic[HandlerT])  — `ALL`  *(line 33)*

  A weakref.ref which allows annotation with custom attributes.

### class `BoundMethodWeakref`(Generic[HandlerT])  — `ALL`  *(line 39)*

  Weakly references a bound instance method.
  Instance methods are bound dynamically each time they are fetched.
  weakref.ref on a bound instance method doesn't work because
  as soon as you drop the reference, the method object dies.
  Instead, this class holds weak references to both the instance and the function,
  which can then be used to bind an instance method.
  To get the actual method, you call an instance as you would a weakref.ref.

### `BoundMethodWeakref.__call__`(self) -> Optional[HandlerT]  — `ALL`  *(line 68)*

### `BoundMethodWeakref.__init__`(self, target: HandlerT, onDelete: Optional[Callable[[BoundMethodWeakref], None]]=None)  — `ALL`  *(line 51)*

### class `HandlerRegistrar`(Generic[HandlerT])  — `ALL`  *(line 90)*

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

### `HandlerRegistrar.__init__`(self, *, _deprecationMessage: str | None=None)  — `changed 2025.3`  *(line 103)*

  **Signature history:**
  - **2023.3:** `HandlerRegistrar.__init__(self)`
  - **2025.3:** `HandlerRegistrar.__init__(self, *, _deprecationMessage: str | None=None)`

  Initialise the handler registrar.
  
  :param _deprecationMessage: Optional deprecation message to be logged when :method:`register` is called on the handler.

### @property `HandlerRegistrar.handlers`(self) -> Generator[HandlerT, None, None]  — `ALL`  *(line 173)*

  Generator of registered handler functions.
  This should be used when you want to call the handlers.

### `HandlerRegistrar.moveToEnd`(self, handler: HandlerT, last: bool=False) -> bool  — `ALL`  *(line 141)*

  Move a registered handler to the start or end of the collection with registered handlers.
  This can be used to modify the order in which handlers are called.
  @param last: Whether to move the handler to the end.
          If C{False} (default), the handler is moved to the start.
  @returns: Whether the handler was found.

### `HandlerRegistrar.register`(self, handler: HandlerT)  — `ALL`  *(line 117)*

  You can register functions, bound instance methods, class methods, static methods or lambdas.
  However, the callable must be kept alive by your code otherwise it will be de-registered.
  This is due to the use of weak references.
  This is especially relevant when using lambdas.

### `HandlerRegistrar.unregister`(self, handler: Union[AnnotatableWeakref[HandlerT], BoundMethodWeakref[HandlerT], HandlerT])  — `ALL`  *(line 158)*

### `_getHandlerKey`(handler: Callable) -> HandlerKeyT  — `changed 2025.3`  *(line 78)*

  **Signature history:**
  - **2023.3:** `_getHandlerKey(handler: HandlerT) -> HandlerKeyT`
  - **2025.3:** `_getHandlerKey(handler: Callable) -> HandlerKeyT`

  Get a key which identifies a handler function.
  This is needed because we store weak references, not the actual functions.
  We store the key on the weak reference.
  When the handler dies, we can use the key on the weak reference to remove the handler.

### `callWithSupportedKwargs`(func, *args, **kwargs)  — `ALL`  *(line 184)*

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

### class `DefaultAppArgs`(argparse.Namespace)  — `ALL`  *(line 38)*

---

## `gui/settingsDialogs.py`

### class `AddSymbolDialog`(gui.contextHelp.ContextHelpMixin, wx.Dialog)  — `ALL`  *(line 6256)*

### `AddSymbolDialog.__init__`(self, parent)  — `ALL`  *(line 6262)*

### class `AddonStorePanel`(SettingsPanel)  — `ALL`  *(line 3634)*

### `AddonStorePanel._enterTriggersOnChangeMirrorURL`(self, evt: wx.KeyEvent)  — `since 2025.3`  *(line 3725)*

  Open the change update mirror URL dialog in response to the enter key in the mirror URL read-only text box.

### `AddonStorePanel._updateCurrentMirrorURL`(self)  — `since 2025.3`  *(line 3732)*

### `AddonStorePanel.makeSettings`(self, settingsSizer: wx.BoxSizer) -> None  — `ALL`  *(line 3639)*

### `AddonStorePanel.onChangeMirrorURL`(self, evt: wx.CommandEvent | wx.KeyEvent)  — `since 2025.3`  *(line 3703)*

  Show the dialog to change the Add-on Store mirror URL, and refresh the dialog in response to the URL being changed.

### `AddonStorePanel.onPanelActivated`(self)  — `since 2025.3`  *(line 3742)*

### `AddonStorePanel.onSave`(self)  — `ALL`  *(line 3746)*

### class `AdvancedPanel`(SettingsPanel)  — `ALL`  *(line 4739)*

### `AdvancedPanel.isValid`(self) -> bool  — `since 2024.4`  *(line 4818)*

### `AdvancedPanel.makeSettings`(self, settingsSizer)  — `ALL`  *(line 4759)*

  :type settingsSizer: wx.BoxSizer

### `AdvancedPanel.onEnableControlsCheckBox`(self, evt)  — `ALL`  *(line 4807)*

### `AdvancedPanel.onSave`(self)  — `ALL`  *(line 4803)*

### class `AdvancedPanelControls`(gui.contextHelp.ContextHelpMixin, wx.Panel)  — `ALL`  *(line 4101)*

  Holds the actual controls for the Advanced Settings panel, this allows the state of the controls to
  be more easily managed.

### `AdvancedPanelControls.__init__`(self, parent)  — `ALL`  *(line 4111)*

### `AdvancedPanelControls._getDefaultValue`(self, configPath)  — `ALL`  *(line 4622)*

### `AdvancedPanelControls.haveConfigDefaultsBeenRestored`(self)  — `ALL`  *(line 4625)*

### `AdvancedPanelControls.isValid`(self) -> bool  — `since 2024.4`  *(line 4604)*

### `AdvancedPanelControls.onOpenScratchpadDir`(self, evt)  — `ALL`  *(line 4618)*

### `AdvancedPanelControls.onSave`(self)  — `ALL`  *(line 4689)*

### `AdvancedPanelControls.restoreToDefaults`(self)  — `ALL`  *(line 4661)*

### class `AudioPanel`(SettingsPanel)  — `ALL`  *(line 3475)*

### @staticmethod `AudioPanel._addAudioCombos`(panel: SettingsPanel, sHelper: guiHelper.BoxSizerHelper)  — `until 2023.3`  *(line 2590)*

### `AudioPanel._appendSoundSplitModesList`(self, settingsSizerHelper: guiHelper.BoxSizerHelper) -> None  — `since 2024.4`  *(line 3563)*

### `AudioPanel._onSoundVolChange`(self, event: wx.Event) -> None  — `ALL`  *(line 3615)*

  Called when the sound volume follow checkbox is checked or unchecked.

### `AudioPanel.isValid`(self) -> bool  — `since 2024.4`  *(line 3619)*

### `AudioPanel.makeSettings`(self, settingsSizer: wx.BoxSizer) -> None  — `ALL`  *(line 3480)*

### `AudioPanel.onPanelActivated`(self)  — `ALL`  *(line 3611)*

### `AudioPanel.onSave`(self)  — `ALL`  *(line 3578)*

### class `AutoSettingsMixin`  — `ALL`  *(line 1321)*

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

### `AutoSettingsMixin.__init__`(self, *args, **kwargs)  — `ALL`  *(line 1336)*

  Mixin init, forwards args to other base class.
  The other base class is likely L{gui.SettingsPanel}.
  @param args: Positional args to passed to other base class.
  @param kwargs: Keyword args to passed to other base class.

### `AutoSettingsMixin._createNewControl`(self, setting, settingsStorage)  — `ALL`  *(line 1539)*

### `AutoSettingsMixin._getSettingControlHelpId`(self, controlId)  — `ALL`  *(line 1381)*

  Define the helpId associated to this control.

### `AutoSettingsMixin._getSettingMaker`(self, setting)  — `ALL`  *(line 1554)*

### `AutoSettingsMixin._getSettingsStorage`(self) -> Any  — `ALL`  *(line 1368)*

  Override to change storage object for setting values.

### `AutoSettingsMixin._makeBooleanSettingControl`(self, setting: BooleanDriverSetting, settingsStorage: Any)  — `ALL`  *(line 1479)*

  Same as L{_makeSliderSettingControl} but for boolean settings. Returns checkbox.

### `AutoSettingsMixin._makeSliderSettingControl`(self, setting: NumericDriverSetting, settingsStorage: Any) -> wx.BoxSizer  — `ALL`  *(line 1385)*

  Constructs appropriate GUI controls for given L{DriverSetting} such as label and slider.
  @param setting: Setting to construct controls for
  @param settingsStorage: where to get initial values / set values.
          This param must have an attribute with a name matching setting.id.
          In most cases it will be of type L{AutoSettings}
  @return: wx.BoxSizer containing newly created controls.

### `AutoSettingsMixin._makeStringSettingControl`(self, setting: DriverSetting, settingsStorage: Any)  — `ALL`  *(line 1424)*

  Same as L{_makeSliderSettingControl} but for string settings displayed in a wx.Choice control
  Options for the choice control come from the availableXstringvalues property
  (Dict[id, StringParameterInfo]) on the instance returned by self.getSettings()
  The id of the value is stored on settingsStorage.
  Returns sizer with label and combobox.

### @classmethod `AutoSettingsMixin._setSliderStepSizes`(cls, slider, setting)  — `ALL`  *(line 1377)*

### `AutoSettingsMixin._updateValueForControl`(self, setting, settingsStorage)  — `ALL`  *(line 1563)*

### `AutoSettingsMixin.getSettings`(self) -> AutoSettings  — `ALL`  *(line 1358)*

### @property `AutoSettingsMixin.hasOptions`(self) -> bool  — `ALL`  *(line 1373)*

### `AutoSettingsMixin.makeSettings`(self, sizer: wx.BoxSizer)  — `ALL`  *(line 1361)*

  Populate the panel with settings controls.
  @note: Normally classes also inherit from settingsDialogs.SettingsPanel.
  @param sizer: The sizer to which to add the settings controls.

### `AutoSettingsMixin.onDiscard`(self)  — `ALL`  *(line 1583)*

### `AutoSettingsMixin.onPanelActivated`(self)  — `ALL`  *(line 1608)*

  Called after the panel has been activated
  @note: Normally classes also inherit from settingsDialogs.SettingsPanel.

### `AutoSettingsMixin.onSave`(self)  — `ALL`  *(line 1593)*

### `AutoSettingsMixin.refreshGui`(self)  — `ALL`  *(line 1596)*

### `AutoSettingsMixin.updateDriverSettings`(self, changedSetting=None)  — `ALL`  *(line 1508)*

  Creates, hides or updates existing GUI controls for all of supported settings.

### class `BrailleDisplaySelectionDialog`(SettingsDialog)  — `ALL`  *(line 4899)*

### @staticmethod `BrailleDisplaySelectionDialog.getCurrentAutoDisplayDescription`()  — `ALL`  *(line 4937)*

### `BrailleDisplaySelectionDialog.makeSettings`(self, settingsSizer)  — `ALL`  *(line 4906)*

### `BrailleDisplaySelectionDialog.onDisplayNameChanged`(self, evt)  — `ALL`  *(line 5004)*

### `BrailleDisplaySelectionDialog.onOk`(self, evt)  — `ALL`  *(line 5007)*

### `BrailleDisplaySelectionDialog.postInit`(self)  — `ALL`  *(line 4932)*

### `BrailleDisplaySelectionDialog.updateBrailleDisplayLists`(self)  — `ALL`  *(line 4946)*

### `BrailleDisplaySelectionDialog.updateStateDependentControls`(self)  — `ALL`  *(line 4976)*

### class `BrailleSettingsPanel`(SettingsPanel)  — `ALL`  *(line 4824)*

### `BrailleSettingsPanel._enterTriggersOnChangeDisplay`(self, evt)  — `ALL`  *(line 4861)*

### `BrailleSettingsPanel.makeSettings`(self, settingsSizer)  — `ALL`  *(line 4829)*

### `BrailleSettingsPanel.onChangeDisplay`(self, evt)  — `ALL`  *(line 4867)*

### `BrailleSettingsPanel.onDiscard`(self)  — `ALL`  *(line 4892)*

### `BrailleSettingsPanel.onPanelActivated`(self)  — `ALL`  *(line 4884)*

### `BrailleSettingsPanel.onPanelDeactivated`(self)  — `ALL`  *(line 4888)*

### `BrailleSettingsPanel.onSave`(self)  — `ALL`  *(line 4895)*

### `BrailleSettingsPanel.updateCurrentDisplay`(self)  — `ALL`  *(line 4877)*

### class `BrailleSettingsSubPanel`(AutoSettingsMixin, SettingsPanel)  — `ALL`  *(line 5048)*

### `BrailleSettingsSubPanel._onModeChange`(self, evt: wx.CommandEvent)  — `since 2024.4`  *(line 5490)*

### @property `BrailleSettingsSubPanel.driver`(self)  — `ALL`  *(line 5052)*

### `BrailleSettingsSubPanel.getSettings`(self) -> AutoSettings  — `ALL`  *(line 5055)*

### `BrailleSettingsSubPanel.makeSettings`(self, settingsSizer)  — `ALL`  *(line 5058)*

### `BrailleSettingsSubPanel.onBlinkCursorChange`(self, evt)  — `ALL`  *(line 5476)*

### `BrailleSettingsSubPanel.onReadByParagraphChange`(self, evt: wx.CommandEvent)  — `since 2024.4`  *(line 5487)*

### `BrailleSettingsSubPanel.onSave`(self)  — `ALL`  *(line 5422)*

### `BrailleSettingsSubPanel.onShowCursorChange`(self, evt)  — `ALL`  *(line 5470)*

### `BrailleSettingsSubPanel.onShowMessagesChange`(self, evt)  — `ALL`  *(line 5479)*

### `BrailleSettingsSubPanel.onTetherToChange`(self, evt: wx.CommandEvent) -> None  — `ALL`  *(line 5482)*

  Shows or hides "Move system caret when routing review cursor" braille setting.

### class `BrowseModePanel`(SettingsPanel)  — `ALL`  *(line 2527)*

### `BrowseModePanel.makeSettings`(self, settingsSizer)  — `ALL`  *(line 2532)*

### `BrowseModePanel.onSave`(self)  — `ALL`  *(line 2642)*

### class `DocumentFormattingPanel`(SettingsPanel)  — `ALL`  *(line 3061)*

### `DocumentFormattingPanel._onLineIndentationChange`(self, evt: wx.CommandEvent) -> None  — `ALL`  *(line 3385)*

### `DocumentFormattingPanel._onLinksChange`(self, evt: wx.CommandEvent)  — `since 2025.3`  *(line 3388)*

### `DocumentFormattingPanel.makeSettings`(self, settingsSizer)  — `ALL`  *(line 3069)*

### `DocumentFormattingPanel.onSave`(self)  — `ALL`  *(line 3391)*

### class `DocumentNavigationPanel`(SettingsPanel)  — `ALL`  *(line 3442)*

### `DocumentNavigationPanel.makeSettings`(self, settingsSizer: wx.BoxSizer) -> None  — `ALL`  *(line 3447)*

### `DocumentNavigationPanel.onSave`(self)  — `ALL`  *(line 3459)*

### class `DriverSettingChanger`(object)  — `ALL`  *(line 1278)*

  Functor which acts as callback for GUI events.

### `DriverSettingChanger.__call__`(self, evt)  — `ALL`  *(line 1289)*

### `DriverSettingChanger.__init__`(self, driver, setting)  — `ALL`  *(line 1281)*

### @property `DriverSettingChanger.driver`(self)  — `ALL`  *(line 1286)*

### class `GeneralSettingsPanel`(SettingsPanel)  — `ALL`  *(line 804)*

### `GeneralSettingsPanel._enterTriggersOnChangeMirrorURL`(self, evt: wx.KeyEvent)  — `since 2025.3`  *(line 995)*

  Open the change update mirror URL dialog in response to the enter key in the mirror URL read-only text box.

### `GeneralSettingsPanel._updateCurrentMirrorURL`(self)  — `since 2025.3`  *(line 1087)*

### `GeneralSettingsPanel.makeSettings`(self, settingsSizer)  — `ALL`  *(line 809)*

### `GeneralSettingsPanel.onChangeMirrorURL`(self, evt: wx.CommandEvent | wx.KeyEvent)  — `since 2025.3`  *(line 973)*

  Show the dialog to change the update mirror URL, and refresh the dialog in response to the URL being changed.

### `GeneralSettingsPanel.onCopySettings`(self, evt)  — `ALL`  *(line 1002)*

### `GeneralSettingsPanel.onPanelActivated`(self)  — `since 2025.3`  *(line 1082)*

### `GeneralSettingsPanel.onSave`(self)  — `ALL`  *(line 1052)*

### `GeneralSettingsPanel.postSave`(self)  — `ALL`  *(line 1097)*

### class `InputCompositionPanel`(SettingsPanel)  — `ALL`  *(line 2268)*

### `InputCompositionPanel.makeSettings`(self, settingsSizer)  — `ALL`  *(line 2273)*

### `InputCompositionPanel.onSave`(self)  — `ALL`  *(line 2347)*

### class `KeyboardSettingsPanel`(SettingsPanel)  — `ALL`  *(line 1971)*

### `KeyboardSettingsPanel.isValid`(self) -> bool  — `changed 2024.4`  *(line 2109)*

  **Signature history:**
  - **2023.3:** `KeyboardSettingsPanel.isValid(self)`
  - **2024.4:** `KeyboardSettingsPanel.isValid(self) -> bool`

### `KeyboardSettingsPanel.makeSettings`(self, settingsSizer)  — `ALL`  *(line 1976)*

### `KeyboardSettingsPanel.onSave`(self)  — `ALL`  *(line 2123)*

### class `LanguageRestartDialog`(gui.contextHelp.ContextHelpMixin, wx.Dialog)  — `ALL`  *(line 1102)*

### `LanguageRestartDialog.__init__`(self, parent)  — `ALL`  *(line 1108)*

### `LanguageRestartDialog.onRestartNowButton`(self, evt)  — `ALL`  *(line 1135)*

### class `MagnifierPanel`(SettingsPanel)  — `since 2026.1+`  *(line 5903)*

### `MagnifierPanel.makeSettings`(self, settingsSizer: wx.BoxSizer)  — `since 2026.1+`  *(line 5908)*

### `MagnifierPanel.onSave`(self)  — `since 2026.1+`  *(line 5989)*

  Save the current selections to config.

### class `MathSettingsPanel`(SettingsPanel)  — `since 2026.1+`  *(line 2663)*

### `MathSettingsPanel._getEnumIndexFromConfigValue`(self, enumClass: Type[DisplayStringEnum], configValue: Any) -> int  — `since 2026.1+`  *(line 2690)*

  Helper function to get the index of an enum option based on its config value.
  
  :param enumClass: The DisplayStringEnum class to search in
  :param configValue: The config value to find the index for
  :return: The index of the enum option with the matching value

### `MathSettingsPanel._getEnumValueFromSelection`(self, enumClass: Type[DisplayStringEnum], selectionIndex: int) -> Any  — `since 2026.1+`  *(line 2707)*

  Helper function to get the config value from a selection index.
  
  :param enumClass: The DisplayStringEnum class to get the value from
  :param selectionIndex: The index of the selected option
  :return: The config value of the selected enum option

### `MathSettingsPanel._getSpeechStyleDisplayString`(self, configValue: str) -> str  — `since 2026.1+`  *(line 2673)*

  Helper function to get the display string for a speech style config value.
  
  :param configValue: The config value to find the display string for
  :return: The display string to show in the UI

### `MathSettingsPanel.makeSettings`(self, settingsSizer: wx.BoxSizer) -> None  — `since 2026.1+`  *(line 2724)*

### `MathSettingsPanel.onSave`(self)  — `since 2026.1+`  *(line 2984)*

### class `MouseSettingsPanel`(SettingsPanel)  — `ALL`  *(line 2143)*

### `MouseSettingsPanel.makeSettings`(self, settingsSizer)  — `ALL`  *(line 2148)*

### `MouseSettingsPanel.onSave`(self)  — `ALL`  *(line 2218)*

### class `MultiCategorySettingsDialog`(SettingsDialog)  — `ALL`  *(line 508)*

  A settings dialog with multiple settings categories.
  A multi category settings dialog consists of a list view with settings categories on the left side,
  and a settings panel on the right side of the dialog.
  Furthermore, in addition to Ok and Cancel buttons, it has an Apply button by default,
  which is different  from the default behavior of L{SettingsDialog}.
  
  To use this dialog: set title and populate L{categoryClasses} with subclasses of SettingsPanel.
  Make sure that L{categoryClasses} only  contains panels that are available on a particular system.
  For example, if a certain category of settings is only supported on Windows 10 and higher,
  that category should be left out of L{categoryClasses}

### class `MultiCategorySettingsDialog.CategoryUnavailableError`(RuntimeError)  — `ALL`  *(line 524)*

### `MultiCategorySettingsDialog.__init__`(self, parent, initialCategory=None)  — `ALL`  *(line 527)*

  @param parent: The parent for this dialog; C{None} for no parent.
  @type parent: wx.Window
  @param initialCategory: The initial category to select when opening this dialog
  @type parent: SettingsPanel

### `MultiCategorySettingsDialog._doCategoryChange`(self, newCatId)  — `ALL`  *(line 730)*

### `MultiCategorySettingsDialog._doSave`(self)  — `ALL`  *(line 775)*

### `MultiCategorySettingsDialog._getCategoryPanel`(self, catId)  — `ALL`  *(line 657)*

### `MultiCategorySettingsDialog._notifyAllPanelsSaveOccurred`(self)  — `ALL`  *(line 771)*

### `MultiCategorySettingsDialog._onPanelLayoutChanged`(self, evt)  — `ALL`  *(line 722)*

### `MultiCategorySettingsDialog._saveAllPanels`(self)  — `ALL`  *(line 767)*

### `MultiCategorySettingsDialog._validateAllPanels`(self)  — `ALL`  *(line 759)*

  Check if all panels are valid, and can be saved
  @note: raises ValueError if a panel is not valid. See c{SettingsPanel.isValid}

### `MultiCategorySettingsDialog.makeSettings`(self, settingsSizer)  — `ALL`  *(line 572)*

### `MultiCategorySettingsDialog.onApply`(self, evt)  — `ALL`  *(line 794)*

### `MultiCategorySettingsDialog.onCancel`(self, evt)  — `ALL`  *(line 789)*

### `MultiCategorySettingsDialog.onCategoryChange`(self, evt)  — `ALL`  *(line 751)*

### `MultiCategorySettingsDialog.onCharHook`(self, evt)  — `ALL`  *(line 697)*

  Listens for keyboard input and switches panels for control+tab

### `MultiCategorySettingsDialog.onOk`(self, evt)  — `ALL`  *(line 780)*

### `MultiCategorySettingsDialog.postInit`(self)  — `ALL`  *(line 685)*

### class `NVDASettingsDialog`(MultiCategorySettingsDialog)  — `ALL`  *(line 6176)*

### `NVDASettingsDialog.Destroy`(self)  — `ALL`  *(line 6249)*

### `NVDASettingsDialog._doOnCategoryChange`(self)  — `ALL`  *(line 6217)*

### `NVDASettingsDialog._getDialogTitle`(self)  — `ALL`  *(line 6236)*

### `NVDASettingsDialog.makeSettings`(self, settingsSizer)  — `ALL`  *(line 6210)*

### `NVDASettingsDialog.onCategoryChange`(self, evt)  — `ALL`  *(line 6243)*

### class `ObjectPresentationPanel`(SettingsPanel)  — `ALL`  *(line 2365)*

### `ObjectPresentationPanel.makeSettings`(self, settingsSizer)  — `ALL`  *(line 2395)*

### `ObjectPresentationPanel.onSave`(self)  — `ALL`  *(line 2505)*

### class `PrivacyAndSecuritySettingsPanel`(SettingsPanel)  — `since 2026.1+`  *(line 6003)*

### `PrivacyAndSecuritySettingsPanel._confirmEnableScreenCurtainWithUser`(self) -> bool  — `since 2026.1+`  *(line 6151)*

  Confirm with the user before enabling Screen Curtain, if configured to do so.
  
  :return: ``True`` if the Screen Curtain should be enabled; ``False`` otherwise.

### `PrivacyAndSecuritySettingsPanel._ensureScreenCurtainEnableState`(self, evt: wx.CommandEvent)  — `since 2026.1+`  *(line 6127)*

  Ensures that toggling the Screen Curtain checkbox toggles the Screen Curtain.

### `PrivacyAndSecuritySettingsPanel._ocrActive`(self) -> bool  — `since 2026.1+`  *(line 6109)*

  Outputs a message when trying to activate screen curtain when OCR is active.
  
  :return: ``True`` when OCR is active, ``False`` otherwise.

### `PrivacyAndSecuritySettingsPanel.makeSettings`(self, sizer: wx.BoxSizer)  — `since 2026.1+`  *(line 6008)*

### `PrivacyAndSecuritySettingsPanel.onSave`(self)  — `since 2026.1+`  *(line 6091)*

### class `RemoteSettingsPanel`(SettingsPanel)  — `since 2025.3`  *(line 3753)*

### `RemoteSettingsPanel._disableDescendants`(self, sizer: wx.Sizer, excluded: Container[wx.Window])  — `since 2025.3`  *(line 3888)*

  Disable all but the specified discendant windows of this sizer.
  
  Disables all child windows, and recursively calls itself for all child sizers.
  
  :param sizer: Root sizer whose descendents should be disabled.
  :param excluded: Container of windows that should remain enabled.

### `RemoteSettingsPanel._onAutoconnect`(self, evt: wx.CommandEvent) -> None  — `since 2025.3`  *(line 3943)*

  Respond to the auto-connection checkbox being checked or unchecked.

### `RemoteSettingsPanel._onClientOrServer`(self, evt: wx.CommandEvent) -> None  — `since 2025.3`  *(line 3947)*

  Respond to the selected value of the client/server choice control changing.

### `RemoteSettingsPanel._onEnableRemote`(self, evt: wx.CommandEvent)  — `since 2025.3`  *(line 3940)*

### `RemoteSettingsPanel._setControls`(self) -> None  — `since 2025.3`  *(line 3902)*

  Ensure the state of the GUI is internally consistent, as well as consistent with the state of the config.
  
  Does not set the value of controls, just which ones are enabled.

### `RemoteSettingsPanel._setFromConfig`(self) -> None  — `since 2025.3`  *(line 3923)*

  Ensure the state of the GUI matches that of the saved configuration.
  
  Also ensures the state of the GUI is internally consistent.

### `RemoteSettingsPanel.isValid`(self) -> bool  — `since 2025.3`  *(line 3970)*

### `RemoteSettingsPanel.makeSettings`(self, sizer: wx.BoxSizer)  — `since 2025.3`  *(line 3758)*

### `RemoteSettingsPanel.onDeleteFingerprints`(self, evt: wx.CommandEvent) -> None  — `since 2025.3`  *(line 3951)*

  Respond to presses of the delete trusted fingerprints button.

### `RemoteSettingsPanel.onSave`(self)  — `since 2025.3`  *(line 3997)*

### class `ReviewCursorPanel`(SettingsPanel)  — `ALL`  *(line 2230)*

### `ReviewCursorPanel.makeSettings`(self, settingsSizer)  — `ALL`  *(line 2235)*

### `ReviewCursorPanel.onSave`(self)  — `ALL`  *(line 2261)*

### class `SettingsDialog`(DpiScalingHelperMixinWithoutInit, gui.contextHelp.ContextHelpMixin, wx.Dialog)  — `ALL`  *(line 114)*

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

### class `SettingsDialog.DialogState`(IntEnum)  — `ALL`  *(line 146)*

### class `SettingsDialog.MultiInstanceError`(RuntimeError)  — `ALL`  *(line 136)*

### class `SettingsDialog.MultiInstanceErrorWithDialog`(MultiInstanceError)  — `ALL`  *(line 139)*

### `SettingsDialog.MultiInstanceErrorWithDialog.__init__`(self, dialog: 'SettingsDialog', *args: object) -> None  — `ALL`  *(line 142)*

### `SettingsDialog.__init__`(self, parent: wx.Window, resizeable: bool=False, hasApplyButton: bool=False, settingsSizerOrientation: int=wx.VERTICAL, multiInstanceAllowed: bool=False, buttons: Set[int]={wx.OK, wx.CANCEL})  — `ALL`  *(line 207)*

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

### `SettingsDialog.__new__`(cls, *args, **kwargs)  — `ALL`  *(line 156)*

### `SettingsDialog._enterActivatesOk_ctrlSActivatesApply`(self, evt)  — `ALL`  *(line 284)*

  Listens for keyboard input and triggers ok button on enter and triggers apply button when control + S is
  pressed. Cancel behavior is built into wx.
  Pressing enter will also close the dialog when a list has focus
  (e.g. the list of symbols in the symbol pronunciation dialog).
  Without this custom handler, enter would propagate to the list control (wx ticket #3725).

### `SettingsDialog._onWindowDestroy`(self, evt: wx.WindowDestroyEvent)  — `ALL`  *(line 345)*

### `SettingsDialog._setInstanceDestroyedState`(self)  — `ALL`  *(line 185)*

### `SettingsDialog.makeSettings`(self, sizer)  — `ALL`  *(line 299)*

  Populate the dialog with settings controls.
  Subclasses must override this method.
  @param sizer: The sizer to which to add the settings controls.
  @type sizer: wx.Sizer

### `SettingsDialog.onApply`(self, evt: wx.CommandEvent)  — `ALL`  *(line 337)*

  Take action in response to the Apply button being pressed.
  Sub-classes may extend or override this method.
  This base method should be called to run the postInit method.

### `SettingsDialog.onCancel`(self, evt: wx.CommandEvent)  — `ALL`  *(line 321)*

  Take action in response to the Cancel button being pressed.
  Sub-classes may extend this method.
  This base method should always be called to clean up the dialog.

### `SettingsDialog.onClose`(self, evt: wx.CommandEvent)  — `ALL`  *(line 329)*

  Take action in response to the Close button being pressed.
  Sub-classes may extend this method.
  This base method should always be called to clean up the dialog.

### `SettingsDialog.onOk`(self, evt: wx.CommandEvent)  — `ALL`  *(line 313)*

  Take action in response to the OK button being pressed.
  Sub-classes may extend this method.
  This base method should always be called to clean up the dialog.

### `SettingsDialog.postInit`(self)  — `ALL`  *(line 307)*

  Called after the dialog has been created.
  For example, this might be used to set focus to the desired control.
  Sub-classes may override this method.

### class `SettingsPanel`(DpiScalingHelperMixinWithoutInit, gui.contextHelp.ContextHelpMixin, wx.Panel)  — `ALL`  *(line 363)*

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

### `SettingsPanel.__init__`(self, parent: wx.Window)  — `ALL`  *(line 388)*

  @param parent: The parent for this panel; C{None} for no parent.

### `SettingsPanel._buildGui`(self)  — `ALL`  *(line 403)*

### `SettingsPanel._sendLayoutUpdatedEvent`(self)  — `ALL`  *(line 483)*

  Notify any wx parents that may be listening that they should redo their layout in whatever way
  makes sense for them. It is expected that sub-classes call this method in response to changes in
  the number of GUI items in their panel.

### `SettingsPanel._validationErrorMessageBox`(self, message: str, option: str, category: Optional[str]=None)  — `since 2024.4`  *(line 449)*

### `SettingsPanel.isValid`(self) -> bool  — `changed 2024.4`  *(line 440)*

  **Signature history:**
  - **2023.3:** `SettingsPanel.isValid(self)`
  - **2024.4:** `SettingsPanel.isValid(self) -> bool`

  Evaluate whether the current circumstances of this panel are valid
  and allow saving all the settings in a L{MultiCategorySettingsDialog}.
  Sub-classes may extend this method.
  @returns: C{True} if validation should continue,
          C{False} otherwise.

### `SettingsPanel.makeSettings`(self, sizer: wx.BoxSizer)  — `ALL`  *(line 412)*

  Populate the panel with settings controls.
  Subclasses must override this method.
  @param sizer: The sizer to which to add the settings controls.

### `SettingsPanel.onDiscard`(self)  — `ALL`  *(line 477)*

  Take action in response to the parent's dialog Cancel button being pressed.
  Sub-classes may override this method.
  MultiCategorySettingsDialog is responsible for cleaning up the panel when Cancel is pressed.

### `SettingsPanel.onPanelActivated`(self)  — `ALL`  *(line 419)*

  Called after the panel has been activated (i.e. de corresponding category is selected in the list of categories).
  For example, this might be used for resource intensive tasks.
  Sub-classes should extend this method.

### `SettingsPanel.onPanelDeactivated`(self)  — `ALL`  *(line 426)*

  Called after the panel has been deactivated (i.e. another category has been selected in the list of categories).
  Sub-classes should extendthis method.

### `SettingsPanel.onSave`(self)  — `ALL`  *(line 433)*

  Take action in response to the parent's dialog OK or apply button being pressed.
  Sub-classes should override this method.
  MultiCategorySettingsDialog is responsible for cleaning up the panel when OK is pressed.

### `SettingsPanel.postSave`(self)  — `ALL`  *(line 472)*

  Take action whenever saving settings for all panels in a L{MultiCategorySettingsDialog} succeeded.
  Sub-classes may extend this method.

### class `SettingsPanelAccessible`(wx.Accessible)  — `ALL`  *(line 493)*

  WX Accessible implementation to set the role of a settings panel to property page,
  as well as to set the accessible description based on the panel's description.

### `SettingsPanelAccessible.GetDescription`(self, childId)  — `ALL`  *(line 504)*

### `SettingsPanelAccessible.GetRole`(self, childId)  — `ALL`  *(line 501)*

### class `SpeechSettingsPanel`(SettingsPanel)  — `ALL`  *(line 1141)*

### `SpeechSettingsPanel._enterTriggersOnChangeSynth`(self, evt)  — `ALL`  *(line 1185)*

### `SpeechSettingsPanel.isValid`(self) -> bool  — `since 2024.4`  *(line 1220)*

### `SpeechSettingsPanel.makeSettings`(self, settingsSizer)  — `ALL`  *(line 1146)*

### `SpeechSettingsPanel.onChangeSynth`(self, evt)  — `ALL`  *(line 1191)*

### `SpeechSettingsPanel.onDiscard`(self)  — `ALL`  *(line 1214)*

### `SpeechSettingsPanel.onPanelActivated`(self)  — `ALL`  *(line 1205)*

### `SpeechSettingsPanel.onPanelDeactivated`(self)  — `ALL`  *(line 1210)*

### `SpeechSettingsPanel.onSave`(self)  — `ALL`  *(line 1217)*

### `SpeechSettingsPanel.updateCurrentSynth`(self)  — `ALL`  *(line 1201)*

### class `SpeechSymbolsDialog`(SettingsDialog)  — `ALL`  *(line 6281)*

### `SpeechSymbolsDialog.OnAddClick`(self, evt)  — `ALL`  *(line 6484)*

### `SpeechSymbolsDialog.OnRemoveClick`(self, evt)  — `ALL`  *(line 6526)*

### `SpeechSymbolsDialog.__init__`(self, parent)  — `ALL`  *(line 6284)*

### `SpeechSymbolsDialog._refreshVisibleItems`(self)  — `ALL`  *(line 6566)*

### `SpeechSymbolsDialog.filter`(self, filterText='')  — `ALL`  *(line 6404)*

### `SpeechSymbolsDialog.getItemTextForList`(self, item, column)  — `ALL`  *(line 6447)*

### `SpeechSymbolsDialog.makeSettings`(self, settingsSizer)  — `ALL`  *(line 6305)*

### `SpeechSymbolsDialog.onFilterEditTextChange`(self, evt)  — `ALL`  *(line 6571)*

### `SpeechSymbolsDialog.onListItemFocused`(self, evt)  — `ALL`  *(line 6469)*

### `SpeechSymbolsDialog.onOk`(self, evt)  — `ALL`  *(line 6550)*

### `SpeechSymbolsDialog.onSymbolEdited`(self)  — `ALL`  *(line 6460)*

### `SpeechSymbolsDialog.postInit`(self)  — `ALL`  *(line 6401)*

### class `StringDriverSettingChanger`(DriverSettingChanger)  — `ALL`  *(line 1295)*

  Same as L{DriverSettingChanger} but handles combobox events.

### `StringDriverSettingChanger.__call__`(self, evt)  — `ALL`  *(line 1302)*

### `StringDriverSettingChanger.__init__`(self, driver, setting, container)  — `ALL`  *(line 1298)*

### class `SynthesizerSelectionDialog`(SettingsDialog)  — `ALL`  *(line 1224)*

### `SynthesizerSelectionDialog.makeSettings`(self, settingsSizer)  — `ALL`  *(line 1230)*

### `SynthesizerSelectionDialog.onOk`(self, evt)  — `ALL`  *(line 1255)*

### `SynthesizerSelectionDialog.postInit`(self)  — `ALL`  *(line 1239)*

### `SynthesizerSelectionDialog.updateSynthesizerList`(self)  — `ALL`  *(line 1243)*

### class `TouchInteractionPanel`(SettingsPanel)  — `ALL`  *(line 4023)*

### `TouchInteractionPanel.makeSettings`(self, settingsSizer)  — `ALL`  *(line 4028)*

### `TouchInteractionPanel.onSave`(self)  — `ALL`  *(line 4042)*

### class `UwpOcrPanel`(SettingsPanel)  — `ALL`  *(line 4048)*

### `UwpOcrPanel.makeSettings`(self, settingsSizer)  — `ALL`  *(line 4053)*

### `UwpOcrPanel.onSave`(self)  — `ALL`  *(line 4094)*

### class `VisionProviderStateControl`(vision.providerBase.VisionProviderStateControl)  — `ALL`  *(line 5554)*

  Gives settings panels for vision enhancement providers a way to control a
  single vision enhancement provider, handling any error conditions in
  a UX friendly way.

### `VisionProviderStateControl.__init__`(self, parent: wx.Window, providerInfo: vision.providerInfo.ProviderInfo)  — `ALL`  *(line 5561)*

### `VisionProviderStateControl._doStartProvider`(self) -> bool  — `ALL`  *(line 5601)*

  Attempt to start the provider, catching any errors.
  @return True on successful termination.

### `VisionProviderStateControl._doTerminate`(self) -> bool  — `ALL`  *(line 5615)*

  Attempt to terminate the provider, catching any errors.
  @return True on successful termination.

### `VisionProviderStateControl.getProviderInfo`(self) -> vision.providerInfo.ProviderInfo  — `ALL`  *(line 5569)*

### `VisionProviderStateControl.getProviderInstance`(self) -> Optional[vision.providerBase.VisionEnhancementProvider]  — `ALL`  *(line 5572)*

### `VisionProviderStateControl.startProvider`(self, shouldPromptOnError: bool=True) -> bool  — `ALL`  *(line 5575)*

  Initializes the provider, prompting user with the error if necessary.
  @param shouldPromptOnError: True if  the user should be presented with any errors that may occur.
  @return: True on success

### `VisionProviderStateControl.terminateProvider`(self, shouldPromptOnError: bool=True) -> bool  — `ALL`  *(line 5588)*

  Terminate the provider, prompting user with the error if necessary.
  @param shouldPromptOnError: True if  the user should be presented with any errors that may occur.
  @return: True on success

### class `VisionProviderSubPanel_Settings`(AutoSettingsMixin, SettingsPanel)  — `ALL`  *(line 5766)*

### `VisionProviderSubPanel_Settings.__init__`(self, parent: wx.Window, *, settingsCallable: Callable[[], vision.providerBase.VisionEnhancementProviderSettings])  — `ALL`  *(line 5774)*

  @param settingsCallable: A callable that returns an instance to a VisionEnhancementProviderSettings.
          This will usually be a weakref, but could be any callable taking no arguments.

### `VisionProviderSubPanel_Settings.getSettings`(self) -> AutoSettings  — `ALL`  *(line 5787)*

### `VisionProviderSubPanel_Settings.makeSettings`(self, settingsSizer)  — `ALL`  *(line 5791)*

### class `VisionProviderSubPanel_Wrapper`(SettingsPanel)  — `ALL`  *(line 5796)*

### `VisionProviderSubPanel_Wrapper.__init__`(self, parent: wx.Window, providerControl: VisionProviderStateControl)  — `ALL`  *(line 5801)*

### `VisionProviderSubPanel_Wrapper._createProviderSettings`(self)  — `ALL`  *(line 5850)*

### `VisionProviderSubPanel_Wrapper._enableToggle`(self, evt)  — `ALL`  *(line 5876)*

### `VisionProviderSubPanel_Wrapper._nonEnableableGUI`(self, evt)  — `ALL`  *(line 5865)*

### `VisionProviderSubPanel_Wrapper._updateOptionsVisibility`(self)  — `ALL`  *(line 5842)*

### `VisionProviderSubPanel_Wrapper.makeSettings`(self, settingsSizer)  — `ALL`  *(line 5811)*

### `VisionProviderSubPanel_Wrapper.onDiscard`(self)  — `ALL`  *(line 5893)*

### `VisionProviderSubPanel_Wrapper.onSave`(self)  — `ALL`  *(line 5897)*

### class `VisionSettingsPanel`(SettingsPanel)  — `ALL`  *(line 5634)*

### `VisionSettingsPanel._createProviderSettingsPanel`(self, providerInfo: vision.providerInfo.ProviderInfo) -> Optional[SettingsPanel]  — `ALL`  *(line 5645)*

### `VisionSettingsPanel.makeSettings`(self, settingsSizer: wx.BoxSizer)  — `ALL`  *(line 5670)*

### `VisionSettingsPanel.onDiscard`(self)  — `ALL`  *(line 5732)*

### `VisionSettingsPanel.onPanelActivated`(self)  — `ALL`  *(line 5729)*

### `VisionSettingsPanel.onSave`(self)  — `ALL`  *(line 5755)*

### `VisionSettingsPanel.refreshPanel`(self)  — `ALL`  *(line 5722)*

### `VisionSettingsPanel.safeInitProviders`(self, providers: List[vision.providerInfo.ProviderInfo]) -> None  — `ALL`  *(line 5691)*

  Initializes one or more providers in a way that is gui friendly,
  showing an error if appropriate.

### `VisionSettingsPanel.safeTerminateProviders`(self, providers: List[vision.providerInfo.ProviderInfo], verbose: bool=False) -> None  — `ALL`  *(line 5705)*

  Terminates one or more providers in a way that is gui friendly,
  @verbose: Whether to show a termination error.
  @returns: Whether termination succeeded for all providers.

### class `VoiceSettingsPanel`(AutoSettingsMixin, SettingsPanel)  — `ALL`  *(line 1616)*

### `VoiceSettingsPanel._appendDelayedCharacterDescriptions`(self, settingsSizerHelper: guiHelper.BoxSizerHelper) -> None  — `ALL`  *(line 1867)*

### `VoiceSettingsPanel._appendSpeechModesList`(self, settingsSizerHelper: guiHelper.BoxSizerHelper) -> None  — `since 2024.4`  *(line 1851)*

### `VoiceSettingsPanel._appendSymbolDictionariesList`(self, settingsSizerHelper: guiHelper.BoxSizerHelper) -> None  — `since 2024.4`  *(line 1835)*

### `VoiceSettingsPanel._getSettingControlHelpId`(self, controlId: str) -> str  — `changed 2025.3`  *(line 1629)*

  **Signature history:**
  - **2023.3:** `VoiceSettingsPanel._getSettingControlHelpId(self, controlId)`
  - **2025.3:** `VoiceSettingsPanel._getSettingControlHelpId(self, controlId: str) -> str`

### `VoiceSettingsPanel._onSpeechModesListChange`(self, evt: wx.CommandEvent)  — `since 2024.4`  *(line 1922)*

### `VoiceSettingsPanel._onUnicodeNormalizationChange`(self, evt: wx.CommandEvent)  — `since 2024.4`  *(line 1949)*

### @property `VoiceSettingsPanel.driver`(self)  — `ALL`  *(line 1622)*

### `VoiceSettingsPanel.getSettings`(self) -> AutoSettings  — `ALL`  *(line 1626)*

### `VoiceSettingsPanel.isValid`(self) -> bool  — `since 2024.4`  *(line 1955)*

### `VoiceSettingsPanel.makeSettings`(self, settingsSizer)  — `ALL`  *(line 1646)*

### `VoiceSettingsPanel.onAutoLanguageSwitchingChange`(self, evt: wx.CommandEvent)  — `since 2025.3`  *(line 1878)*

  Take action when the autoLanguageSwitching checkbox is pressed.

### `VoiceSettingsPanel.onSave`(self)  — `ALL`  *(line 1882)*

### `_isResponseAddonStoreCacheHash`(response: requests.Response) -> bool  — `since 2025.3`  *(line 6577)*

### `_isResponseUpdateMetadata`(response: requests.Response) -> bool  — `since 2025.3`  *(line 6589)*

### `_synthWarningDialog`(newSynth: str)  — `ALL`  *(line 3463)*

### `showStartErrorForProviders`(parent: wx.Window, providers: List[vision.providerInfo.ProviderInfo]) -> None  — `ALL`  *(line 5494)*

### `showTerminationErrorForProviders`(parent: wx.Window, providers: List[vision.providerInfo.ProviderInfo]) -> None  — `ALL`  *(line 5524)*

---

## `jobObject.py`  — `NEW MODULE since 2026.1+`

### class `Job`  — `since 2026.1+`  *(line 31)*

  Manage a Windows Job object.
  
  This class wraps a Windows Job object, allowing processes to be
  assigned to the job and optionally ensuring all processes are
  terminated when the job handle is closed.
  The job is automatically closed when the Job object is destroyed.

### `Job.__del__`(self)  — `since 2026.1+`  *(line 98)*

### `Job.__init__`(self)  — `since 2026.1+`  *(line 40)*

  Initialize the Job object.
  
  Create a Windows Job object.

### `Job.assignProcess`(self, processHandle: HANDLE)  — `since 2026.1+`  *(line 76)*

  Assign a process to the job object.
  
  Assign the given process handle to this job so the process becomes
  subject to the job's limits and controls. The caller is responsible for
  providing a valid process handle with the necessary access rights.
  
  :param processHandle: Handle of the process to assign to the job.
  :raises RuntimeError: If assigning the process to the job fails.

### `Job.close`(self)  — `since 2026.1+`  *(line 91)*

  Closes the job object.

### `Job.setBasicLimits`(self, basicLimitFlags: JOB_OBJECT_LIMIT)  — `since 2026.1+`  *(line 52)*

### `Job.setUiRestrictions`(self, uiLimitFlags: JOB_OBJECT_UILIMIT)  — `since 2026.1+`  *(line 64)*

---

## `languageHandler.py`

> Language and localization support.
> This module assists in NVDA going global through language services
> such as converting Windows locale ID's to friendly names and presenting available languages.

### class `LOCALE`(enum.IntEnum)  — `ALL`  *(line 69)*

### `_createGettextTranslation`(localeName: str) -> tuple[None | gettext.GNUTranslations | gettext.NullTranslations, str | None]  — `changed 2025.3`  *(line 321)*

  **Signature history:**
  - **2023.3:** `_createGettextTranslation(localeName: str) -> Union[None, gettext.GNUTranslations, gettext.NullTranslations]`
  - **2025.3:** `_createGettextTranslation(localeName: str) -> tuple[None | gettext.GNUTranslations | gettext.NullTranslations, str | None]`

### `_setPythonLocale`(localeString: str) -> bool  — `ALL`  *(line 385)*

  Sets Python locale to a specified one.
  Returns `True` if succesfull `False` if locale cannot be set or retrieved.

### `ansiCodePageFromNVDALocale`(localeName: str) -> str | None  — `changed 2026.1+`  *(line 234)*

  **Signature history:**
  - **2023.3:** `ansiCodePageFromNVDALocale(localeName: str) -> Optional[str]`
  - **2026.1+:** `ansiCodePageFromNVDALocale(localeName: str) -> str | None`

  Returns either ANSI code page for a given locale using GetLocaleInfoEx or None
  if the given locale is not known to Windows.

### `englishCountryNameFromNVDALocale`(localeName: str) -> str | None  — `changed 2026.1+`  *(line 215)*

  **Signature history:**
  - **2023.3:** `englishCountryNameFromNVDALocale(localeName: str) -> Optional[str]`
  - **2026.1+:** `englishCountryNameFromNVDALocale(localeName: str) -> str | None`

  Returns either English name of the given country using GetLocaleInfoEx or None
  if the given locale is not known to Windows.

### `englishLanguageNameFromNVDALocale`(localeName: str) -> str | None  — `changed 2026.1+`  *(line 182)*

  **Signature history:**
  - **2023.3:** `englishLanguageNameFromNVDALocale(localeName: str) -> Optional[str]`
  - **2026.1+:** `englishLanguageNameFromNVDALocale(localeName: str) -> str | None`

  Returns either English name of the given language  using `GetLocaleInfoEx` or None
  if the given locale is not known to Windows.

### `getAvailableLanguages`(presentational: bool=False) -> list[tuple[str, str]]  — `changed 2026.1+`  *(line 277)*

  **Signature history:**
  - **2023.3:** `getAvailableLanguages(presentational: bool=False) -> List[Tuple[str, str]]`
  - **2026.1+:** `getAvailableLanguages(presentational: bool=False) -> list[tuple[str, str]]`

  generates a list of locale names, plus their full localized language and country names.
  @param presentational: whether this is meant to be shown alphabetically by language description

### `getLanguage`() -> str  — `ALL`  *(line 448)*

### `getLanguageCliArgs`() -> Tuple[str, ...]  — `until 2024.4`  *(line 294)*

  Returns all command line arguments which were used to set current NVDA language
  or an empty tuple if language has not been specified from the CLI.

### `getLanguageDescription`(language: str) -> weakref.ReferenceType | None  — `changed 2026.1+`  *(line 153)*

  **Signature history:**
  - **2023.3:** `getLanguageDescription(language: str) -> Optional[str]`
  - **2026.1+:** `getLanguageDescription(language: str) -> weakref.ReferenceType | None`

  Finds out the description (localized full name) of a given local name

### `getWindowsLanguage`()  — `ALL`  *(line 308)*

  Fetches the locale name of the user's configured language in Windows.

### `isLanguageForced`() -> bool  — `ALL`  *(line 303)*

  Returns `True` if language is provided from the command line - `False` otherwise.

### `isNormalizedWin32Locale`(localeName: str) -> bool  — `ALL`  *(line 82)*

  Checks if the given locale is in a form which can be used by Win32 locale functions such as
  `GetLocaleInfoEx`. See `normalizeLocaleForWin32` for more comments.

### `listNVDALocales`() -> list[str]  — `changed 2026.1+`  *(line 259)*

  **Signature history:**
  - **2023.3:** `listNVDALocales() -> List[str]`
  - **2026.1+:** `listNVDALocales() -> list[str]`

### `localeNameToWindowsLCID`(localeName: str) -> int  — `ALL`  *(line 110)*

  Retrieves the Windows locale identifier (LCID) for the given locale name
  @param localeName: a string of 2letterLanguage_2letterCountry
  or just language (2letterLanguage or 3letterLanguage)
  @returns: a Windows LCID or L{LCID_NONE} if it could not be retrieved.

### `localeStringFromLocaleCode`(localeCode: str) -> str  — `ALL`  *(line 368)*

  Given an NVDA locale such as 'en' or or a Windows locale such as 'pl_PL'
  creates a locale representation in a standard form for Win32
  which can be safely passed to Python's `setlocale`.
  The required format is:
  'englishLanguageName_englishCountryName.localeANSICodePage'
  Raises exception if the given locale is not known to Windows.

### `makeNpgettext`(translations: Union[None, gettext.GNUTranslations, gettext.NullTranslations]) -> Callable[[str, str, str, Union[int, float]], str]  — `until 2023.3`  *(line 315)*

  Obtain a  npgettext function for use with a gettext translations instance.
  npgettext is used to support message contexts with respect to ngettext,
  but Python 3.7's gettext module doesn't support this,
  so NVDA must provide its own implementation.

### `makePgettext`(translations)  — `until 2023.3`  *(line 293)*

  Obtain a pgettext function for use with a gettext translations instance.
  pgettext is used to support message contexts,
  but Python 3.7's gettext module doesn't support this,
  so NVDA must provide its own implementation.

### `normalizeLanguage`(lang: str) -> str | None  — `changed 2026.1+`  *(line 452)*

  **Signature history:**
  - **2023.3:** `normalizeLanguage(lang: str) -> Optional[str]`
  - **2026.1+:** `normalizeLanguage(lang: str) -> str | None`

  Normalizes a  language-dialect string  in to a standard form we can deal with.
  Converts  any dash to underline, and makes sure that language is lowercase and dialect is upercase.

### `normalizeLocaleForWin32`(localeName: str) -> str  — `ALL`  *(line 94)*

  Converts given locale to a form which can be used by Win32 locale functions such as
  `GetLocaleInfoEx` unless locale is normalized already.
  Uses hyphen as a language/country separator taking care not to replace underscores used
  as a separator between country name and alternate order specifiers.
  For example locales using alternate sorts see:
  https://docs.microsoft.com/en-us/openspecs/windows_protocols/ms-lcid/e6a54e86-9660-44fa-a005-d00da97722f2
  While NVDA does not support locales requiring multiple sorting orders users may still have their Windows
  set to such locale and if all underscores were replaced unconditionally
  we would be unable to generate Python locale from their default UI language.

### `setLanguage`(lang: str) -> None  — `ALL`  *(line 334)*

  Sets the following using `lang` such as "en", "ru_RU", or "es-ES". Use "Windows" to use the system locale
   - the windows locale for the thread (fallback to system locale)
   - the translation service (fallback to English)
   - Current NVDA language (match the translation service)
   - the python locale for the thread (match the translation service, fallback to system default)

### `setLocale`(localeName: str) -> None  — `ALL`  *(line 401)*

  Set python's locale using a `localeName` such as "en", "ru_RU", or "es-ES".
  Will fallback on current NVDA language if it cannot be set and finally fallback to the system locale.
  Passing NVDA locales straight to python `locale.setlocale` does now work since it tries to normalize the
  parameter using `locale.normalize` which results in locales unknown to Windows (Python issue 37945).
  For example executing: `locale.setlocale(locale.LC_ALL, "pl")`
  results in locale being set to `('pl_PL', 'ISO8859-2')`
  which is meaningless to Windows,

### `stripLocaleFromLangCode`(langWithOptionalLocale: str) -> str  — `ALL`  *(line 479)*

  Get the lang code eg "en" for "en-au" or "chr" for "chr-US-Qaaa-x-west".
  @param langWithOptionalLocale: may already be language only, or include locale specifier
  (e.g. "en" or "en-au").
  @return The language only part, before the first dash.

### `useImperialMeasurements`() -> bool  — `ALL`  *(line 468)*

  Whether or not measurements should be reported as imperial, rather than metric.

### `windowsLCIDToLocaleName`(lcid: int) -> str | None  — `changed 2026.1+`  *(line 128)*

  **Signature history:**
  - **2023.3:** `windowsLCIDToLocaleName(lcid: int) -> Optional[str]`
  - **2026.1+:** `windowsLCIDToLocaleName(lcid: int) -> str | None`

  Gets a normalized locale from a Windows LCID.
  
  NVDA should avoid relying on LCIDs in future, as they have been deprecated by MS:
  https://docs.microsoft.com/en-us/globalization/locale/locale-names

---

## `nvwave.py`

> Provides a simple Python interface to playing audio using the Windows Audio Session API (WASAPI), as well as other useful utilities.

### class `AudioPurpose`(Enum)  — `ALL`  *(line 75)*

  The purpose of a particular stream of audio.

### class `WAVEFORMATEX`(Structure)  — `until 2025.3`  *(line 63)*

### class `WAVEHDR`(Structure)  — `until 2024.4`  *(line 94)*

### class `WAVEOUTCAPS`(Structure)  — `until 2024.4`  *(line 124)*

### class `WasapiWavePlayer`(garbageHandler.TrackedObject)  — `until 2024.4`  *(line 781)*

  Synchronously play a stream of audio using WASAPI.
  To use, construct an instance and feed it waveform audio using L{feed}.
  Keeps device open until it is either not available, or WavePlayer is explicitly closed / deleted.
  Will attempt to use the preferred device, if not will fallback to the default device.

### `WasapiWavePlayer.__del__`(self)  — `until 2024.4`  *(line 875)*

### `WasapiWavePlayer.__init__`(self, channels: int, samplesPerSec: int, bitsPerSample: int, outputDevice: typing.Union[str, int]=WAVE_MAPPER, closeWhenIdle: bool=False, wantDucking: bool=True, buffered: bool=False, purpose: AudioPurpose=AudioPurpose.SPEECH)  — `until 2024.4`  *(line 807)*

  Constructor.
  @param channels: The number of channels of audio; e.g. 2 for stereo, 1 for mono.
  @param samplesPerSec: Samples per second (hz).
  @param bitsPerSample: The number of bits per sample.
  @param outputDevice: The name of the audio output device to use,
          WAVE_MAPPER for default.
  @param closeWhenIdle: Deprecated; ignored.
  @param wantDucking: if true then background audio will be ducked on Windows 8 and higher
  @param buffered: Whether to buffer small chunks of audio to prevent audio glitches.
  @param purpose: The purpose of this audio.
  @note: If C{outputDevice} is a name and no such device exists, the default device will be used.
  @raise WindowsError: If there was an error opening the audio output device.

### `WasapiWavePlayer._callback`(cppPlayer, feedId)  — `until 2024.4`  *(line 869)*

### @staticmethod `WasapiWavePlayer._deviceNameToId`(name, fallbackToDefault=True)  — `until 2023.3`  *(line 1038)*

### @staticmethod `WasapiWavePlayer._getDevices`()  — `until 2023.3`  *(line 1026)*

### @classmethod `WasapiWavePlayer._idleCheck`(cls)  — `until 2024.4`  *(line 1061)*

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

### @classmethod `WasapiWavePlayer._isDefaultDevice`(cls, name)  — `since 2024.4`  *(line 1103)*

### @classmethod `WasapiWavePlayer._scheduleIdleCheck`(cls)  — `until 2024.4`  *(line 1046)*

### `WasapiWavePlayer._setVolumeFromConfig`(self)  — `until 2024.4`  *(line 1033)*

### `WasapiWavePlayer.close`(self)  — `until 2024.4`  *(line 908)*

  Close the output device.

### `WasapiWavePlayer.feed`(self, data: typing.Union[bytes, c_void_p], size: typing.Optional[int]=None, onDone: typing.Optional[typing.Callable]=None) -> None  — `until 2024.4`  *(line 912)*

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

### `WasapiWavePlayer.idle`(self)  — `until 2024.4`  *(line 968)*

  Indicate that this player is now idle; i.e. the current continuous segment  of audio is complete.

### `WasapiWavePlayer.open`(self)  — `until 2024.4`  *(line 892)*

  Open the output device.
  This will be called automatically when required.
  It is not an error if the output device is already open.

### `WasapiWavePlayer.pause`(self, switch: bool)  — `until 2024.4`  *(line 984)*

  Pause or unpause playback.
  @param switch: C{True} to pause playback, C{False} to unpause.

### `WasapiWavePlayer.setVolume`(self, *, all: Optional[float]=None, left: Optional[float]=None, right: Optional[float]=None)  — `until 2024.4`  *(line 1004)*

  Set the volume of one or more channels in this stream.
  Levels must be specified as a number between 0 and 1.
  @param all: The level to set for all channels.
  @param left: The level to set for the left channel.
  @param right: The level to set for the right channel.

### `WasapiWavePlayer.stop`(self)  — `until 2024.4`  *(line 974)*

  Stop playback.

### `WasapiWavePlayer.sync`(self)  — `until 2024.4`  *(line 962)*

  Synchronise with playback.
  This method blocks until the previously fed chunk of audio has finished playing.

### class `WavePlayer`(garbageHandler.TrackedObject)  — `since 2025.3`  *(line 180)*

  Synchronously play a stream of audio using WASAPI.
  To use, construct an instance and feed it waveform audio using L{feed}.
  Keeps device open until it is either not available, or WavePlayer is explicitly closed / deleted.
  Will attempt to use the preferred device, if not will fallback to the default device.

### `WavePlayer.__del__`(self)  — `since 2025.3`  *(line 277)*

### `WavePlayer.__init__`(self, channels: int, samplesPerSec: int, bitsPerSample: int, outputDevice: str=DEFAULT_DEVICE_KEY, wantDucking: bool=True, purpose: AudioPurpose=AudioPurpose.SPEECH)  — `since 2025.3`  *(line 206)*

  Constructor.
  @param channels: The number of channels of audio; e.g. 2 for stereo, 1 for mono.
  @param samplesPerSec: Samples per second (hz).
  @param bitsPerSample: The number of bits per sample.
  @param outputDevice: The name of the audio output device to use, defaults to WasapiWavePlayer.DEFAULT_DEVICE_KEY
  @param wantDucking: if true then background audio will be ducked on Windows 8 and higher
  @param purpose: The purpose of this audio.
  @note: If C{outputDevice} is a name and no such device exists, the default device will be used.
  @raise WindowsError: If there was an error opening the audio output device.

### `WavePlayer._callback`(cppPlayer, feedId)  — `since 2025.3`  *(line 271)*

### @classmethod `WavePlayer._idleCheck`(cls)  — `since 2025.3`  *(line 485)*

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

### `WavePlayer._onPreSpeak`(self, speechSequence: SpeechSequence)  — `since 2025.3`  *(line 528)*

### @classmethod `WavePlayer._scheduleIdleCheck`(cls)  — `since 2025.3`  *(line 470)*

### `WavePlayer._setVolumeFromConfig`(self)  — `since 2025.3`  *(line 457)*

### `WavePlayer.close`(self)  — `since 2025.3`  *(line 311)*

  Close the output device.

### `WavePlayer.enableTrimmingLeadingSilence`(self, enable: bool) -> None  — `since 2025.3`  *(line 446)*

  Enable or disable automatic leading silence removal.
  This is by default enabled for speech audio, and disabled for non-speech audio.

### `WavePlayer.feed`(self, data: typing.Union[bytes, c_void_p], size: typing.Optional[int]=None, onDone: typing.Optional[typing.Callable]=None) -> None  — `since 2025.3`  *(line 315)*

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

### `WavePlayer.idle`(self)  — `since 2025.3`  *(line 377)*

  Indicate that this player is now idle; i.e. the current continuous segment  of audio is complete.

### `WavePlayer.open`(self)  — `since 2025.3`  *(line 295)*

  Open the output device.
  This will be called automatically when required.
  It is not an error if the output device is already open.

### `WavePlayer.pause`(self, switch: bool)  — `since 2025.3`  *(line 397)*

  Pause or unpause playback.
  @param switch: C{True} to pause playback, C{False} to unpause.

### `WavePlayer.setVolume`(self, *, all: Optional[float]=None, left: Optional[float]=None, right: Optional[float]=None)  — `since 2025.3`  *(line 417)*

  Set the volume of one or more channels in this stream.
  Levels must be specified as a number between 0 and 1.
  @param all: The level to set for all channels.
  @param left: The level to set for the left channel.
  @param right: The level to set for the right channel.

### `WavePlayer.startTrimmingLeadingSilence`(self, start: bool=True) -> None  — `since 2025.3`  *(line 453)*

  Start or stop trimming the leading silence from the next audio chunk.

### `WavePlayer.stop`(self)  — `since 2025.3`  *(line 385)*

  Stop playback.

### `WavePlayer.sync`(self)  — `since 2025.3`  *(line 371)*

  Synchronise with playback.
  This method blocks until the previously fed chunk of audio has finished playing.

### class `WinmmWavePlayer`(garbageHandler.TrackedObject)  — `until 2024.4`  *(line 176)*

  Synchronously play a stream of audio.
  To use, construct an instance and feed it waveform audio using L{feed}.
  Keeps device open until it is either not available, or WavePlayer is explicitly closed / deleted.
  Will attempt to use the preferred device, if not will fallback to the WAVE_MAPPER device.
  When not using the preferred device, when idle devices will be checked to see if the preferred
  device has become available again. If so, it will be re-instated.

### `WinmmWavePlayer.__del__`(self)  — `until 2024.4`  *(line 598)*

### `WinmmWavePlayer.__init__`(self, channels: int, samplesPerSec: int, bitsPerSample: int, outputDevice: typing.Union[str, int]=WAVE_MAPPER, closeWhenIdle: bool=False, wantDucking: bool=True, buffered: bool=False, purpose: AudioPurpose=AudioPurpose.SPEECH)  — `until 2024.4`  *(line 217)*

  Constructor.
  @param channels: The number of channels of audio; e.g. 2 for stereo, 1 for mono.
  @param samplesPerSec: Samples per second (hz).
  @param bitsPerSample: The number of bits per sample.
  @param outputDevice: The device ID or name of the audio output device to use.
  @param closeWhenIdle: If C{True}, close the output device when no audio is being played.
  @param wantDucking: if true then background audio will be ducked on Windows 8 and higher
  @param buffered: Whether to buffer small chunks of audio to prevent audio glitches.
  @note: If C{outputDevice} is a name and no such device exists, the default device will be used.
  @raise WindowsError: If there was an error opening the audio output device.

### `WinmmWavePlayer._close`(self)  — `until 2024.4`  *(line 585)*

### `WinmmWavePlayer._feedUnbuffered`(self, data, onDone=None)  — `until 2024.4`  *(line 425)*

  @note: Raises WindowsError on invalid device (see winmm functions

### `WinmmWavePlayer._feedUnbuffered_handleErrors`(self, data, onDone=None) -> bool  — `until 2024.4`  *(line 407)*

  Tries to feed the device, on error resets the device and tries again.
  @return: False if second attempt fails

### `WinmmWavePlayer._handleWinmmError`(self, message: str)  — `until 2024.4`  *(line 604)*

### `WinmmWavePlayer._idleUnbuffered`(self)  — `until 2024.4`  *(line 530)*

### `WinmmWavePlayer._isPreferredDeviceAvailable`(self) -> bool  — `until 2024.4`  *(line 311)*

  @note: Depending on number of devices being fetched, this may take some time (~3ms)
  @return: True if the preferred device is available

### `WinmmWavePlayer._isPreferredDeviceOpen`(self) -> bool  — `until 2024.4`  *(line 301)*

### `WinmmWavePlayer._safe_winmm_call`(self, winmmCall: Callable[[Optional[int]], None], messageOnFailure: str) -> bool  — `until 2024.4`  *(line 615)*

### `WinmmWavePlayer._setCurrentDevice`(self, preferredDevice: typing.Union[str, int]) -> None  — `until 2024.4`  *(line 273)*

  Sets the _outputDeviceID and _outputDeviceName to the preferredDevice if
  it is available, otherwise falls back to WAVE_MAPPER.
  @param preferredDevice: The preferred device to use.

### `WinmmWavePlayer.close`(self)  — `until 2024.4`  *(line 576)*

  Close the output device.

### `WinmmWavePlayer.feed`(self, data: typing.Union[bytes, c_void_p], size: typing.Optional[int]=None, onDone: typing.Optional[typing.Callable]=None) -> None  — `until 2024.4`  *(line 375)*

  Feed a chunk of audio data to be played.
  This is normally synchronous.
  However, synchronisation occurs on the previous chunk, rather than the current chunk;
  i.e. calling this while no audio is playing will begin playing the chunk
  but return immediately.
  This allows for uninterrupted playback as long as a new chunk is fed before
  the previous chunk has finished playing.
  @param data: Waveform audio in the format specified when this instance was constructed.
  @param size: The size of the data in bytes if data is a ctypes pointer.
          If data is a Python bytes object, size should be None.
  @param onDone: Function to call when this chunk has finished playing.
  @raise WindowsError: If there was an error playing the audio.

### `WinmmWavePlayer.idle`(self)  — `until 2024.4`  *(line 515)*

  Indicate that this player is now idle; i.e. the current continuous segment  of audio is complete.
  This will first call L{sync} to synchronise with playback.
  If L{closeWhenIdle} is C{True}, the output device will be closed.
  A subsequent call to L{feed} will reopen it.

### `WinmmWavePlayer.open`(self)  — `until 2024.4`  *(line 326)*

  Open the output device.
  This will be called automatically when required.
  It is not an error if the output device is already open.

### `WinmmWavePlayer.pause`(self, switch)  — `until 2024.4`  *(line 496)*

  Pause or unpause playback.
  @param switch: C{True} to pause playback, C{False} to unpause.
  @type switch: bool

### `WinmmWavePlayer.stop`(self)  — `until 2024.4`  *(line 551)*

  Stop playback.

### `WinmmWavePlayer.sync`(self)  — `until 2024.4`  *(line 450)*

  Synchronise with playback.
  This method blocks until the previously fed chunk of audio has finished playing.
  It is called automatically by L{feed}, so usually need not be called directly by the user.
  
  Note: it must be possible to call stop concurrently with sync, sync should be considered to be blocking
  the synth driver thread most of the time (ie sync waiting for the last pushed block of audio to
  complete, via the 'winKernal.waitForSingleObject' mechanism)

### `_cleanup`()  — `ALL`  *(line 167)*

### `_getOutputDevices`()  — `until 2024.4`  *(line 635)*

  Generator, returning device ID and device Name in device ID order.
  @note: Depending on number of devices being fetched, this may take some time (~3ms)

### `_isDebugForNvWave`()  — `ALL`  *(line 71)*

### `_wasPlay_errcheck`(res, func, args)  — `until 2023.3`  *(line 747)*

### `_winmm_errcheck`(res, func, args)  — `until 2024.4`  *(line 143)*

### `getOutputDeviceNames`()  — `until 2024.4`  *(line 649)*

  Obtain the names of all audio output devices on the system.
  @return: The names of all output devices on the system.
  @rtype: [str, ...]
  @note: Depending on number of devices being fetched, this may take some time (~3ms)

### `initialize`()  — `ALL`  *(line 543)*

### `isInError`() -> bool  — `ALL`  *(line 173)*

### `outputDeviceIDToName`(ID)  — `until 2024.4`  *(line 658)*

  Obtain the name of an output device given its device ID.
  @param ID: The device ID.
  @type ID: int
  @return: The device name.
  @rtype: str

### `outputDeviceNameToID`(name: str, useDefaultIfInvalid=False) -> int  — `until 2024.4`  *(line 673)*

  Obtain the device ID of an output device given its name.
  @param name: The device name.
  @param useDefaultIfInvalid: C{True} to use the default device (wave mapper) if there is no such device,
          C{False} to raise an exception.
  @return: The device ID.
  @raise LookupError: If there is no such device and C{useDefaultIfInvalid} is C{False}.
  @note: Depending on number of devices, and the position of the device in the list,
  this may take some time (~3ms)

### `playErrorSound`() -> None  — `since 2024.4`  *(line 568)*

### `playWaveFile`(fileName: str, asynchronous: bool=True, isSpeechWaveFileCommand: bool=False)  — `ALL`  *(line 82)*

  plays a specified wave file.
  
  :param fileName: the path to the wave file, usually absolute.
  :param asynchronous: whether the wave file should be played asynchronously
          If ``False``, the calling thread is blocked until the wave has finished playing.
  :param isSpeechWaveFileCommand: whether this wave is played as part of a speech sequence.

### `terminate`() -> None  — `since 2024.4`  *(line 562)*

### `usingWasapiWavePlayer`() -> bool  — `until 2024.4`  *(line 1140)*

---

## `speech/__init__.py`

### `initialize`()  — `ALL`  *(line 157)*

  Loads and sets the synth driver configured in nvda.ini.
  Initializes the state of speech and initializes the sayAllHandler

### `terminate`()  — `ALL`  *(line 174)*

---

## `speech/commands.py`

> Commands that can be embedded in a speech sequence for changing synth parameters, playing sounds or running
>  other callbacks.

### class `BaseCallbackCommand`(SpeechCommand)  — `ALL`  *(line 403)*

  Base class for commands which cause a function to be called when speech reaches them.
  This class should not be instantiated directly.
  It is designed to be subclassed to provide specific functionality;
  e.g. L{BeepCommand}.
  To supply a generic function to run, use L{CallbackCommand}.
  This command is never passed to synth drivers.

### `BaseCallbackCommand.run`(self)  — `ALL`  *(line 413)*

  Code to run when speech reaches this command.
  This method is executed in NVDA's main thread,
  therefore must return as soon as practically possible,
  otherwise it will block production of further speech and or other functionality in NVDA.

### class `BaseProsodyCommand`(SynthParamCommand)  — `ALL`  *(line 252)*

  Base class for commands which change voice prosody; i.e. pitch, rate, etc.
  The change to the setting is specified using either an offset or a multiplier, but not both.
  The L{offset} and L{multiplier} properties convert between the two if necessary.
  To return to the default value, specify neither.
  This base class should not be instantiated directly.

### `BaseProsodyCommand.__eq__`(self, __o: object) -> bool  — `since 2024.4`  *(line 338)*

### `BaseProsodyCommand.__init__`(self, offset=0, multiplier=1)  — `ALL`  *(line 263)*

  Constructor.
  Either of C{offset} or C{multiplier} may be specified, but not both.
  @param offset: The amount by which to increase/decrease the user configured setting;
          e.g. 30 increases by 30, -10 decreases by 10, 0 returns to the configured setting.
  @type offset: int
  @param multiplier: The number by which to multiply the user configured setting;
          e.g. 0.5 is half, 1 returns to the configured setting.
  @param multiplier: int/float

### `BaseProsodyCommand.__ne__`(self, __o) -> bool  — `since 2024.4`  *(line 345)*

### `BaseProsodyCommand.__repr__`(self)  — `ALL`  *(line 326)*

### @property `BaseProsodyCommand.defaultValue`(self)  — `ALL`  *(line 280)*

  The default value for the setting as configured by the user.

### @property `BaseProsodyCommand.multiplier`(self)  — `ALL`  *(line 287)*

  The number by which to multiply the default value.

### @property `BaseProsodyCommand.newValue`(self)  — `ALL`  *(line 315)*

  The new absolute value after the offset or multiplier is applied to the default value.

### @property `BaseProsodyCommand.offset`(self)  — `ALL`  *(line 301)*

  The amount by which to increase/decrease the default value.

### class `BeepCommand`(BaseCallbackCommand)  — `ALL`  *(line 442)*

  Produce a beep.

### `BeepCommand.__init__`(self, hz, length, left=50, right=50)  — `ALL`  *(line 445)*

### `BeepCommand.__repr__`(self)  — `ALL`  *(line 462)*

### `BeepCommand.run`(self)  — `ALL`  *(line 451)*

### class `BreakCommand`(SynthCommand)  — `ALL`  *(line 203)*

  Insert a break between words.

### `BreakCommand.__eq__`(self, __o: object) -> bool  — `since 2024.4`  *(line 216)*

### `BreakCommand.__init__`(self, time: int=0)  — `ALL`  *(line 206)*

  @param time: The duration of the pause to be inserted in milliseconds.

### `BreakCommand.__repr__`(self)  — `ALL`  *(line 213)*

### class `CallbackCommand`(BaseCallbackCommand)  — `ALL`  *(line 421)*

  Call a function when speech reaches this point.
  Note that  the provided function is executed in NVDA's main thread,
          therefore must return as soon as practically possible,
          otherwise it will block production of further speech and or other functionality in NVDA.

### `CallbackCommand.__init__`(self, callback, name: Optional[str]=None)  — `ALL`  *(line 429)*

### `CallbackCommand.__repr__`(self)  — `ALL`  *(line 436)*

### `CallbackCommand.run`(self, *args, **kwargs)  — `ALL`  *(line 433)*

### class `CharacterModeCommand`(SynthParamCommand)  — `ALL`  *(line 156)*

  Turns character mode on and off for speech synths.

### `CharacterModeCommand.__eq__`(self, __o: object) -> bool  — `since 2024.4`  *(line 172)*

### `CharacterModeCommand.__init__`(self, state)  — `ALL`  *(line 159)*

  @param state: if true character mode is on, if false its turned off.
  @type state: boolean

### `CharacterModeCommand.__repr__`(self)  — `ALL`  *(line 169)*

### class `ConfigProfileTriggerCommand`(SpeechCommand)  — `ALL`  *(line 486)*

  Applies (or stops applying) a configuration profile trigger to subsequent speech.

### `ConfigProfileTriggerCommand.__init__`(self, trigger, enter=True)  — `ALL`  *(line 489)*

  @param trigger: The configuration profile trigger.
  @type trigger: L{config.ProfileTrigger}
  @param enter: C{True} to apply the trigger, C{False} to stop applying it.
  @type enter: bool

### class `EndUtteranceCommand`(SpeechCommand)  — `ALL`  *(line 224)*

  End the current utterance at this point in the speech.
  Any text after this will be sent to the synthesizer as a separate utterance.

### `EndUtteranceCommand.__repr__`(self)  — `ALL`  *(line 229)*

### class `IndexCommand`(SynthCommand)  — `ALL`  *(line 116)*

  Marks this point in the speech with an index.
  When speech reaches this index, the synthesizer notifies NVDA,
  thus allowing NVDA to perform actions at specific points in the speech;
  e.g. synchronizing the cursor, beeping or playing a sound.
  Callers should not use this directly.
  Instead, use one of the subclasses of L{BaseCallbackCommand}.
  NVDA handles the indexing and dispatches callbacks as appropriate.

### `IndexCommand.__eq__`(self, __o: object) -> bool  — `since 2024.4`  *(line 138)*

### `IndexCommand.__init__`(self, index)  — `ALL`  *(line 126)*

  @param index: the value of this index
  @type index: integer

### `IndexCommand.__repr__`(self)  — `ALL`  *(line 135)*

### class `LangChangeCommand`(SynthParamCommand)  — `ALL`  *(line 180)*

  A command to switch the language within speech.

### `LangChangeCommand.__eq__`(self, __o: object) -> bool  — `ALL`  *(line 193)*

### `LangChangeCommand.__init__`(self, lang: str | None)  — `changed 2025.3`  *(line 183)*

  **Signature history:**
  - **2023.3:** `LangChangeCommand.__init__(self, lang: Optional[str])`
  - **2025.3:** `LangChangeCommand.__init__(self, lang: str | None)`

  :param lang: The language to switch to: If None then the NVDA locale will be used.

### `LangChangeCommand.__repr__`(self)  — `ALL`  *(line 190)*

### class `PhonemeCommand`(SynthCommand)  — `ALL`  *(line 371)*

  Insert a specific pronunciation.
  This command accepts Unicode International Phonetic Alphabet (IPA) characters.
  Note that this is not well supported by synthesizers.

### `PhonemeCommand.__eq__`(self, __o: object) -> bool  — `since 2024.4`  *(line 395)*

### `PhonemeCommand.__init__`(self, ipa, text=None)  — `ALL`  *(line 377)*

  @param ipa: Unicode IPA characters.
  @type ipa: str
  @param text: Text to speak if the synthesizer does not support
          some or all of the specified IPA characters,
          C{None} to ignore this command instead.
  @type text: str

### `PhonemeCommand.__repr__`(self)  — `ALL`  *(line 389)*

### class `PitchCommand`(BaseProsodyCommand)  — `ALL`  *(line 353)*

  Change the pitch of the voice.

### class `RateCommand`(BaseProsodyCommand)  — `ALL`  *(line 365)*

  Change the rate of the voice.

### class `SpeechCommand`(object)  — `ALL`  *(line 43)*

  The base class for objects that can be inserted between strings of text to perform actions,
  change voice parameters, etc.
  
  Note: Some of these commands are processed by NVDA and are not directly passed to synth drivers.
  synth drivers will only receive commands derived from L{SynthCommand}.

### class `SuppressUnicodeNormalizationCommand`(SpeechCommand)  — `since 2024.4`  *(line 233)*

  Suppresses Unicode normalization at a point in a speech sequence.
  For any text after this, Unicode normalization will be suppressed when state is True.
  When state is False, original behavior of normalization will be restored.
  This command is a no-op when normalization is disabled.

### `SuppressUnicodeNormalizationCommand.__init__`(self, state: bool=True)  — `since 2024.4`  *(line 242)*

  :param state: Suppress normalization if True, don't suppress when False

### `SuppressUnicodeNormalizationCommand.__repr__`(self)  — `since 2024.4`  *(line 248)*

### class `SynthCommand`(SpeechCommand)  — `ALL`  *(line 112)*

  Commands that can be passed to synth drivers.

### class `SynthParamCommand`(SynthCommand)  — `ALL`  *(line 146)*

  A synth command which changes a parameter for subsequent speech.

### class `VolumeCommand`(BaseProsodyCommand)  — `ALL`  *(line 359)*

  Change the volume of the voice.

### class `WaveFileCommand`(BaseCallbackCommand)  — `ALL`  *(line 471)*

  Play a wave file.

### `WaveFileCommand.__init__`(self, fileName)  — `ALL`  *(line 474)*

### `WaveFileCommand.__repr__`(self)  — `ALL`  *(line 482)*

### `WaveFileCommand.run`(self)  — `ALL`  *(line 477)*

### class `_CancellableSpeechCommand`(SpeechCommand)  — `ALL`  *(line 52)*

  A command that allows cancelling the utterance that contains it.
  Support currently experimental and may be subject to change.

### `_CancellableSpeechCommand.__init__`(self, reportDevInfo=False)  — `ALL`  *(line 58)*

  @param reportDevInfo: If true, developer info is reported for repr implementation.

### `_CancellableSpeechCommand.__repr__`(self)  — `ALL`  *(line 103)*

### `_CancellableSpeechCommand._checkIfCancelled`(self)  — `ALL`  *(line 77)*

### `_CancellableSpeechCommand._checkIfValid`(self)  — `ALL`  *(line 70)*

### `_CancellableSpeechCommand._getDevInfo`(self)  — `ALL`  *(line 74)*

### `_CancellableSpeechCommand._getFormattedDevInfo`(self)  — `ALL`  *(line 91)*

### `_CancellableSpeechCommand.cancelUtterance`(self)  — `ALL`  *(line 88)*

### @property `_CancellableSpeechCommand.isCancelled`(self)  — `ALL`  *(line 85)*

---

## `speech/extensions.py`  — `NEW MODULE since 2024.4`

> Extension points for speech.

---

## `speech/languageHandling.py`  — `NEW MODULE since 2025.3`

### `getLangToReport`(lang: str) -> str  — `since 2025.3`  *(line 73)*

  Gets the language to report by NVDA, according to speech settings.
  
  :param lang: A language code corresponding to the text been read.
  :return: A language code corresponding to the language to be reported.

### `getSpeechSequenceWithLangs`(speechSequence: SpeechSequence) -> SpeechSequence  — `since 2025.3`  *(line 15)*

  Get a speech sequence with the language description for each non default language of the read text.
  
  :param speechSequence: The original speech sequence.
  :return: A speech sequence containing descriptions for each non default language, indicating if the language is not supported by the current synthesizer.

### `shouldMakeLangChangeCommand`() -> bool  — `since 2025.3`  *(line 60)*

  Determines if NVDA should get the language of the text been read.

### `shouldReportNotSupported`() -> bool  — `since 2025.3`  *(line 65)*

  Determines if NVDA should report if the language is not supported by the synthesizer.

### `shouldSwitchVoice`() -> bool  — `since 2025.3`  *(line 55)*

  Determines if the current synthesizer should switch to the voice corresponding to the language of the text been read.

---

## `speech/manager.py`

### class `ParamChangeTracker`(object)  — `ALL`  *(line 87)*

  Keeps track of commands which change parameters from their defaults.
  This is useful when an utterance needs to be split.
  As you are processing a sequence,
  you update the tracker with a parameter change using the L{update} method.
  When you split the utterance, you use the L{getChanged} method to get
  the parameters which have been changed from their defaults.

### `ParamChangeTracker.__init__`(self)  — `ALL`  *(line 96)*

### `ParamChangeTracker.getChanged`(self)  — `ALL`  *(line 111)*

  Get the commands for the parameters which have been changed from their defaults.
  @return: List of parameter change commands.
  @type: list of L{SynthParamCommand}

### `ParamChangeTracker.update`(self, command)  — `ALL`  *(line 99)*

  Update the tracker with a parameter change.
  @param command: The parameter change command.
  @type command: L{SynthParamCommand}

### class `SpeechManager`(object)  — `ALL`  *(line 140)*

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

### `SpeechManager.__init__`(self)  — `ALL`  *(line 196)*

### `SpeechManager._buildNextUtterance`(self)  — `ALL`  *(line 452)*

  Since an utterance might be split over several sequences,
  build a complete utterance to pass to the synth.

### `SpeechManager._checkForCancellations`(self, utterance: SpeechSequence) -> bool  — `ALL`  *(line 485)*

  Checks utterance to ensure it is not cancelled (via a _CancellableSpeechCommand).
  Because synthesizers do not expect CancellableSpeechCommands, they are removed from the utterance.
  :arg utterance: The utterance to check for cancellations. Modified in place, CancellableSpeechCommands are
  removed.
  :return True if sequence is still valid, else False

### `SpeechManager._doRemoveCancelledSpeechCommands`(self)  — `ALL`  *(line 569)*

### `SpeechManager._ensureEndUtterance`(self, seq: SpeechSequence, outSeqs, paramsToReplay, paramTracker)  — `ALL`  *(line 297)*

  We split at EndUtteranceCommands so the ends of utterances are easily found.
  This function ensures the given sequence ends with an EndUtterance command,
  Ensures that the sequence also includes an index command at the end,
  It places the complete sequence in outSeqs,
  It clears the given sequence list ready to build a new one,
  And clears the paramsToReplay list
  and refills it with any params that need to be repeated if a new sequence is going to be built.

### `SpeechManager._exitProfileTriggers`(self, triggers)  — `ALL`  *(line 756)*

### `SpeechManager._generateIndexes`(self) -> typing.Generator[_IndexT, None, None]  — `ALL`  *(line 206)*

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

### `SpeechManager._getMostRecentlyCancelledUtterance`(self) -> Optional[_IndexT]  — `ALL`  *(line 543)*

### `SpeechManager._getNextPriority`(self)  — `ALL`  *(line 442)*

  Get the highest priority queue containing pending speech.

### `SpeechManager._getUtteranceIndex`(self, utterance: SpeechSequence)  — `ALL`  *(line 586)*

### `SpeechManager._handleDoneSpeaking`(self)  — `ALL`  *(line 728)*

### `SpeechManager._handleIndex`(self, index: int)  — `ALL`  *(line 674)*

### `SpeechManager._hasNoMoreSpeech`(self)  — `ALL`  *(line 243)*

### @classmethod `SpeechManager._isIndexAAfterIndexB`(cls, indexA: _IndexT, indexB: _IndexT) -> bool  — `ALL`  *(line 540)*

### @classmethod `SpeechManager._isIndexABeforeIndexB`(cls, indexA: _IndexT, indexB: _IndexT) -> bool  — `ALL`  *(line 520)*

  Was indexB created before indexB
  Because indexes wrap after MAX_INDEX, custom logic is needed to compare relative positions.
  The boundary for considering a wrapped value as before another value is based on the distance
  between the indexes. If the distance is greater than half the available index space it is no longer
  before.
  @return True if indexA was created before indexB, else False

### `SpeechManager._onSynthDoneSpeaking`(self, synth: Optional[synthDriverHandler.SynthDriver]=None)  — `ALL`  *(line 721)*

### `SpeechManager._onSynthIndexReached`(self, synth=None, index=None)  — `ALL`  *(line 594)*

### `SpeechManager._processSpeechSequence`(self, inSeq: SpeechSequence)  — `ALL`  *(line 328)*

### `SpeechManager._pushNextSpeech`(self, doneSpeaking: bool)  — `ALL`  *(line 386)*

### `SpeechManager._queueSpeechSequence`(self, inSeq: SpeechSequence, priority: Spri) -> bool  — `ALL`  *(line 273)*

  @return: Whether to interrupt speech.

### `SpeechManager._removeCompletedFromQueue`(self, index: int) -> Tuple[bool, bool]  — `ALL`  *(line 603)*

  Removes completed speech sequences from the queue.
  @param index: The index just reached indicating a completed sequence.
  @return: Tuple of (valid, endOfUtterance),
          where valid indicates whether the index was valid and
          endOfUtterance indicates whether this sequence was the end of the current utterance.
  @rtype: (bool, bool)

### `SpeechManager._reset`(self)  — `ALL`  *(line 224)*

### `SpeechManager._restoreProfileTriggers`(self, triggers)  — `ALL`  *(line 764)*

### `SpeechManager._switchProfile`(self)  — `ALL`  *(line 736)*

### `SpeechManager._synthStillSpeaking`(self) -> bool  — `ALL`  *(line 240)*

### `SpeechManager.cancel`(self)  — `ALL`  *(line 772)*

### `SpeechManager.removeCancelledSpeechCommands`(self)  — `ALL`  *(line 565)*

### `SpeechManager.speak`(self, speechSequence: SpeechSequence, priority: Spri)  — `ALL`  *(line 246)*

### class `_ManagerPriorityQueue`(object)  — `ALL`  *(line 119)*

  A speech queue for a specific priority.
  This is intended for internal use by L{_SpeechManager} only.
  Each priority has a separate queue.
  It holds the pending speech sequences to be spoken,
  as well as other information necessary to restore state when this queue
  is preempted by a higher priority queue.

### `_ManagerPriorityQueue.__init__`(self, priority: Spri)  — `ALL`  *(line 128)*

### `_shouldCancelExpiredFocusEvents`()  — `ALL`  *(line 38)*

### `_shouldDoSpeechManagerLogging`()  — `ALL`  *(line 43)*

### `_speechManagerDebug`(msg, *args, **kwargs) -> None  — `ALL`  *(line 47)*

  Log 'msg % args' with severity 'DEBUG' if speech manager logging is enabled.
  'SpeechManager-' is prefixed to all messages to make searching the log easier.

### `_speechManagerUnitTest`(msg, *args, **kwargs) -> None  — `ALL`  *(line 61)*

  Log 'msg % args' with severity 'DEBUG' if .
  'SpeechManUnitTest-' is prefixed to all messages to make searching the log easier.
  When

---

## `speech/priorities.py`

> Speech priority enumeration.

### class `SpeechPriority`(IntEnum)  — `ALL`  *(line 12)*

  Facilitates the ability to prioritize speech.
  Note: This enum has its counterpart in the NVDAController RPC interface (nvdaController.idl).
  Additions to this enum should also be reflected in nvdaController.idl.

---

## `speech/sayAll.py`

### class `CURSOR`(IntEnum)  — `ALL`  *(line 38)*

### class `SayAllProfileTrigger`(config.ProfileTrigger)  — `ALL`  *(line 465)*

  A configuration profile trigger for when say all is in progress.

### class `_CaretTextReader`(_TextReader)  — `ALL`  *(line 409)*

### `_CaretTextReader.getInitialTextInfo`(self) -> textInfos.TextInfo  — `ALL`  *(line 410)*

### `_CaretTextReader.updateCaret`(self, updater: textInfos.TextInfo) -> None  — `ALL`  *(line 416)*

### class `_ObjectsReader`(_Reader)  — `ALL`  *(line 162)*

  Manages continuous reading of objects.

### `_ObjectsReader.__init__`(self, handler: _SayAllHandler, root: 'NVDAObjects.NVDAObject')  — `ALL`  *(line 165)*

### `_ObjectsReader.next`(self)  — `ALL`  *(line 178)*

### `_ObjectsReader.stop`(self)  — `ALL`  *(line 201)*

### `_ObjectsReader.walk`(self, obj: 'NVDAObjects.NVDAObject')  — `ALL`  *(line 170)*

### class `_Reader`(garbageHandler.TrackedObject)  — `since 2025.3`  *(line 143)*

  Base class for readers in say all.

### `_Reader.__del__`(self)  — `since 2025.3`  *(line 158)*

### `_Reader.__init__`(self, handler: _SayAllHandler)  — `since 2025.3`  *(line 146)*

### `_Reader.next`(self)  — `since 2025.3`  *(line 151)*

### `_Reader.stop`(self)  — `since 2025.3`  *(line 154)*

  Stops the reader.

### class `_ReviewTextReader`(_TextReader)  — `ALL`  *(line 422)*

### `_ReviewTextReader.getInitialTextInfo`(self) -> textInfos.TextInfo  — `ALL`  *(line 423)*

### `_ReviewTextReader.updateCaret`(self, updater: textInfos.TextInfo) -> None  — `ALL`  *(line 426)*

### class `_SayAllHandler`  — `ALL`  *(line 63)*

### `_SayAllHandler.__init__`(self, speechWithoutPausesInstance: SpeechWithoutPauses, speakObject: 'speakObject', getTextInfoSpeech: 'getTextInfoSpeech', SpeakTextInfoState: 'SpeakTextInfoState')  — `ALL`  *(line 64)*

### `_SayAllHandler.isRunning`(self)  — `ALL`  *(line 89)*

  Determine whether say all is currently running.
  @return: C{True} if say all is currently running, C{False} if not.
  @rtype: bool

### `_SayAllHandler.readObjects`(self, obj: 'NVDAObjects.NVDAObject', startedFromScript: bool | None=False)  — `changed 2024.4`  *(line 96)*

  **Signature history:**
  - **2023.3:** `_SayAllHandler.readObjects(self, obj: 'NVDAObjects.NVDAObject')`
  - **2024.4:** `_SayAllHandler.readObjects(self, obj: 'NVDAObjects.NVDAObject', startedFromScript: bool | None=False)`

  Start or restarts the object reader.
  :param obj: the object to be read
  :param startedFromScript: whether the current say all action was initially started from a script; use None to keep
          the last value unmodified, e.g. when the say all action is resumed during skim reading.

### `_SayAllHandler.readText`(self, cursor: CURSOR, startPos: Optional[textInfos.TextInfo]=None, nextLineFunc: Optional[Callable[[textInfos.TextInfo], textInfos.TextInfo]]=None, shouldUpdateCaret: bool=True, startedFromScript: bool | None=False) -> None  — `changed 2024.4`  *(line 108)*

  **Signature history:**
  - **2023.3:** `_SayAllHandler.readText(self, cursor: CURSOR, startPos: Optional[textInfos.TextInfo]=None, nextLineFunc: Optional[Callable[[textInfos.TextInfo], textInfos.TextInfo]]=None, shouldUpdateCaret: bool=True) -> None`
  - **2024.4:** `_SayAllHandler.readText(self, cursor: CURSOR, startPos: Optional[textInfos.TextInfo]=None, nextLineFunc: Optional[Callable[[textInfos.TextInfo], textInfos.TextInfo]]=None, shouldUpdateCaret: bool=True, startedFromScript: bool | None=False) -> None`

  Start or restarts the reader
  :param cursor: the type of cursor used for say all
  :param startPos: start position (only used for table say all)
  :param nextLineFunc: function called to read the next line (only used for table say all)
  :param shouldUpdateCaret: whether the caret should be updated during say all (only used for table say all)
  :param startedFromScript: whether the current say all action was initially started from a script; use None to keep
          the last value unmodified, e.g. when the say all action is resumed during skim reading.

### `_SayAllHandler.stop`(self)  — `ALL`  *(line 80)*

  Stops any active objects reader and resets the SayAllHandler's SpeechWithoutPauses instance

### class `_TableTextReader`(_CaretTextReader)  — `ALL`  *(line 430)*

### `_TableTextReader.__init__`(self, handler: _SayAllHandler, startPos: Optional[textInfos.TextInfo]=None, nextLineFunc: Optional[Callable[[textInfos.TextInfo], textInfos.TextInfo]]=None, shouldUpdateCaret: bool=True)  — `ALL`  *(line 431)*

### `_TableTextReader.collapseLineImpl`(self) -> bool  — `ALL`  *(line 454)*

### `_TableTextReader.getInitialTextInfo`(self) -> textInfos.TextInfo  — `ALL`  *(line 443)*

### `_TableTextReader.nextLineImpl`(self) -> bool  — `ALL`  *(line 446)*

### `_TableTextReader.shouldReadInitialPosition`(self) -> bool  — `ALL`  *(line 457)*

### `_TableTextReader.updateCaret`(self, updater: textInfos.TextInfo) -> None  — `ALL`  *(line 460)*

### class `_TextReader`(_Reader)  — `ALL`  *(line 208)*

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

### `_TextReader.__del__`(self)  — `until 2024.4`  *(line 382)*

### `_TextReader.__init__`(self, handler: _SayAllHandler)  — `ALL`  *(line 231)*

### `_TextReader.collapseLineImpl`(self) -> bool  — `ALL`  *(line 274)*

  Collapses to the end of this line, ready to read the next.
  @return: C{True} if collapsed successfully, C{False} otherwise.

### `_TextReader.finish`(self)  — `ALL`  *(line 385)*

### `_TextReader.getInitialTextInfo`(self) -> textInfos.TextInfo  — `ALL`  *(line 243)*

### `_TextReader.lineReached`(self, obj, bookmark, state)  — `ALL`  *(line 363)*

### `_TextReader.next`(self)  — `since 2025.3`  *(line 289)*

### `_TextReader.nextLine`(self)  — `ALL`  *(line 292)*

### `_TextReader.nextLineImpl`(self) -> bool  — `ALL`  *(line 251)*

  Advances cursor to the next reading chunk (e.g. paragraph).
  @return: C{True} if advanced successfully, C{False} otherwise.

### `_TextReader.shouldReadInitialPosition`(self) -> bool  — `ALL`  *(line 248)*

### `_TextReader.stop`(self)  — `ALL`  *(line 400)*

### `_TextReader.turnPage`(self)  — `ALL`  *(line 374)*

### `_TextReader.updateCaret`(self, updater: textInfos.TextInfo) -> None  — `ALL`  *(line 246)*

### `initialize`(speakFunc: Callable[[SpeechSequence], None], speakObject: 'speakObject', getTextInfoSpeech: 'getTextInfoSpeech', SpeakTextInfoState: 'SpeakTextInfoState')  — `ALL`  *(line 47)*

---

## `speech/speech.py`

> High-level functions to speak information.

### class `SpeakTextInfoState`(object)  — `ALL`  *(line 1452)*

  Caches the state of speakTextInfo such as the current controlField stack, current formatfield and indentation.

### `SpeakTextInfoState.__init__`(self, obj)  — `ALL`  *(line 1462)*

### `SpeakTextInfoState.copy`(self)  — `ALL`  *(line 1478)*

### `SpeakTextInfoState.updateObj`(self)  — `ALL`  *(line 1473)*

### class `SpeechMode`(DisplayStringIntEnum)  — `ALL`  *(line 96)*

### @property `SpeechMode._displayStringLabels`(self) -> dict[Self, str]  — `since 2024.4`  *(line 103)*

### class `SpeechState`  — `ALL`  *(line 118)*

### `_columnCountText`(count: int) -> str  — `since 2024.4`  *(line 2204)*

### `_extendSpeechSequence_addMathForTextInfo`(speechSequence: SpeechSequence, info: textInfos.TextInfo, field: textInfos.Field) -> None  — `ALL`  *(line 1482)*

### `_getPlaceholderSpeechIfTextEmpty`(obj, reason: OutputReason) -> Tuple[bool, SpeechSequence]  — `ALL`  *(line 816)*

  Attempt to get speech for placeholder attribute if text for 'obj' is empty. Don't report the placeholder
   value unless the text is empty, because it is confusing to hear the current value (presumably typed by the
   user) *and* the placeholder. The placeholder should "disappear" once the user types a value.
  :return: `(True, SpeechSequence)` if text for obj was considered empty and we attempted to get speech for the
          placeholder value. `(False, [])` if text for obj was not considered empty.

### `_getSelectionMessageSpeech`(message: str, text: str) -> SpeechSequence  — `ALL`  *(line 1282)*

### `_getSpeakMessageSpeech`(text: str) -> SpeechSequence  — `ALL`  *(line 248)*

  Gets the speech sequence for a given message.
  @param text: the message to speak

### `_getSpeakSsmlSpeech`(ssml: str, markCallback: 'MarkCallbackT | None'=None, _prefixSpeechCommand: SpeechCommand | None=None) -> SpeechSequence  — `since 2024.4`  *(line 277)*

  Gets the speech sequence for given SSML.
  :param ssml: The SSML data to speak
  :param markCallback: An optional callback called for every mark command in the SSML.
  :param _prefixSpeechCommand: A SpeechCommand to append before the sequence.

### `_getSpellingCharAddCapNotification`(speakCharAs: str, sayCapForCapitals: bool, capPitchChange: int, beepForCapitals: bool, reportNormalized: bool=False) -> Generator[SequenceItemT, None, None]  — `changed 2024.4`  *(line 392)*

  **Signature history:**
  - **2023.3:** `_getSpellingCharAddCapNotification(speakCharAs: str, sayCapForCapitals: bool, capPitchChange: int, beepForCapitals: bool) -> Generator[SequenceItemT, None, None]`
  - **2024.4:** `_getSpellingCharAddCapNotification(speakCharAs: str, sayCapForCapitals: bool, capPitchChange: int, beepForCapitals: bool, reportNormalized: bool=False) -> Generator[SequenceItemT, None, None]`

  This function produces a speech sequence containing a character to be spelt as well as commands
  to indicate that this character is uppercase and/or normalized, if applicable.
  :param speakCharAs: The character as it will be spoken by the synthesizer.
  :param sayCapForCapitals: indicates if 'cap' should be reported along with the currently spelled character.
  :param capPitchChange: pitch offset to apply while spelling the currently spelled character.
  :param beepForCapitals: indicates if a cap notification beep should be produced while spelling the currently
  spelled character.
  :param reportNormalized: Indicates if 'normalized' should be reported
  along with the currently spelled character.

### `_getSpellingSpeechAddCharMode`(seq: Generator[SequenceItemT, None, None]) -> Generator[SequenceItemT, None, None]  — `ALL`  *(line 372)*

  Inserts CharacterMode commands in a speech sequence generator to ensure any single character
  is spelled by the synthesizer.
  @param seq: The speech sequence to be spelt.

### `_getSpellingSpeechWithoutCharMode`(text: str, locale: str, useCharacterDescriptions: bool, sayCapForCapitals: bool, capPitchChange: int, beepForCapitals: bool, fallbackToCharIfNoDescription: bool=True, unicodeNormalization: bool=False, reportNormalizedForCharacterNavigation: bool=False) -> Generator[SequenceItemT, None, None]  — `changed 2024.4`  *(line 440)*

  **Signature history:**
  - **2023.3:** `_getSpellingSpeechWithoutCharMode(text: str, locale: str, useCharacterDescriptions: bool, sayCapForCapitals: bool, capPitchChange: int, beepForCapitals: bool, fallbackToCharIfNoDescription: bool=True) -> Generator[SequenceItemT, None, None]`
  - **2024.4:** `_getSpellingSpeechWithoutCharMode(text: str, locale: str, useCharacterDescriptions: bool, sayCapForCapitals: bool, capPitchChange: int, beepForCapitals: bool, fallbackToCharIfNoDescription: bool=True, unicodeNormalization: bool=False, reportNormalizedForCharacterNavigation: bool=False) -> Generator[SequenceItemT, None, None]`

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

### `_getTextInfoSpeech_considerSpelling`(unit: Optional[textInfos.TextInfo], onlyInitialFields: bool, textWithFields: textInfos.TextInfo.TextWithFieldsT, reason: OutputReason, speechSequence: SpeechSequence, language: str) -> Generator[SpeechSequence, None, None]  — `ALL`  *(line 1924)*

### `_getTextInfoSpeech_updateCache`(useCache: Union[bool, SpeakTextInfoState], speakTextInfoState: SpeakTextInfoState, newControlFieldStack: List[textInfos.ControlField], formatFieldAttributesCache: textInfos.Field)  — `ALL`  *(line 1957)*

### `_isControlEndFieldCommand`(command: Union[str, textInfos.FieldCommand])  — `ALL`  *(line 1920)*

### `_objectSpeech_calculateAllowedProps`(reason: OutputReason, shouldReportTextContent: bool, objRole: controlTypes.Role) -> dict[str, bool]  — `changed 2024.4`  *(line 919)*

  **Signature history:**
  - **2023.3:** `_objectSpeech_calculateAllowedProps(reason, shouldReportTextContent)`
  - **2024.4:** `_objectSpeech_calculateAllowedProps(reason: OutputReason, shouldReportTextContent: bool, objRole: controlTypes.Role) -> dict[str, bool]`

### `_rowAndColumnCountText`(rowCount: int, columnCount: int) -> Optional[str]  — `since 2024.4`  *(line 2174)*

### `_rowCountText`(count: int) -> str  — `since 2024.4`  *(line 2194)*

### `_setLastSpeechString`(speechSequence: SpeechSequence, symbolLevel: characterProcessing.SymbolLevel | None, priority: Spri)  — `since 2026.1+`  *(line 148)*

### `_shouldSpeakContentFirst`(reason: OutputReason, role: int, presCat: str, attrs: textInfos.ControlField, tableID: Any, states: Iterable[int]) -> bool  — `ALL`  *(line 2214)*

  Determines whether or not to speak the content before the controlField information.
  Helper function for getControlFieldSpeech.

### `_suppressSpeakTypedCharacters`(number: int)  — `ALL`  *(line 1378)*

  Suppress speaking of typed characters.
  This should be used when sending a string of characters to the system
  and those characters should not be spoken individually as if the user were typing them.
  @param number: The number of characters to suppress.

### `cancelSpeech`()  — `ALL`  *(line 222)*

  Interupts the synthesizer from currently speaking

### `clearTypedWordBuffer`() -> None  — `ALL`  *(line 3129)*

  Forgets any word currently being built up with typed characters for speaking.
  This should be called when the user's context changes such that they could no longer
  complete the word (such as a focus change or choosing to move the caret).

### `getCharDescListFromText`(text, locale)  — `ALL`  *(line 634)*

  This method prepares a list, which contains character and its description for all characters the text is made up of, by checking the presence of character descriptions in characterDescriptions.dic of that locale for all possible combination of consecutive characters in the text.
  This is done to take care of conjunct characters present in several languages such as Hindi, Urdu, etc.

### `getControlFieldSpeech`(attrs: textInfos.ControlField, ancestorAttrs: List[textInfos.Field], fieldType: str, formatConfig: Optional[Dict[str, bool]]=None, extraDetail: bool=False, reason: Optional[OutputReason]=None) -> SpeechSequence  — `ALL`  *(line 2249)*

### `getCurrentLanguage`() -> str  — `ALL`  *(line 318)*

### `getFormatFieldSpeech`(attrs: textInfos.Field, attrsCache: Optional[textInfos.Field]=None, formatConfig: Optional[Dict[str, bool]]=None, reason: Optional[OutputReason]=None, unit: Optional[str]=None, extraDetail: bool=False, initialFormat: bool=False) -> SpeechSequence  — `ALL`  *(line 2581)*

### `getIndentToneDuration`() -> int  — `since 2026.1+`  *(line 1042)*

### `getIndentationSpeech`(indentation: str, formatConfig: Dict[str, bool]) -> SpeechSequence  — `ALL`  *(line 1046)*

  Retrieves the indentation speech sequence for a given string of indentation.
  @param indentation: The string of indentation.
  @param formatConfig: The configuration to use.

### `getObjectPropertiesSpeech`(obj: 'NVDAObjects.NVDAObject', reason: OutputReason=OutputReason.QUERY, _prefixSpeechCommand: Optional[SpeechCommand]=None, **allowedProperties) -> SpeechSequence  — `ALL`  *(line 678)*

### `getObjectSpeech`(obj: 'NVDAObjects.NVDAObject', reason: OutputReason=OutputReason.QUERY, _prefixSpeechCommand: Optional[SpeechCommand]=None) -> SpeechSequence  — `ALL`  *(line 847)*

### `getPreselectedTextSpeech`(text: str) -> SpeechSequence  — `ALL`  *(line 1230)*

  Helper method to get the speech sequence to announce a newly focused control already has
  text selected.
  This method will speak the word "selected" with the provided text appended.
  The announcement order is different from L{speakTextSelected} in order to
  inform a user that the newly focused control has content that is selected,
  which they may unintentionally overwrite.
  
  @remarks: Implemented using L{_getSelectionMessageSpeech}, which allows for
          creating a speech sequence with an arbitrary attached message.

### `getPropertiesSpeech`(reason: OutputReason=OutputReason.QUERY, **propertyValues) -> SpeechSequence  — `ALL`  *(line 1972)*

### `getSingleCharDescription`(text: str, locale: Optional[str]=None) -> Generator[SequenceItemT, None, None]  — `ALL`  *(line 558)*

  Returns a speech sequence:
  a pause, the length determined by getSingleCharDescriptionDelayMS,
  followed by the character description.

### `getSingleCharDescriptionDelayMS`() -> int  — `ALL`  *(line 549)*

  @returns: 1 second, a default delay for delayed character descriptions.
  In the future, this should fetch its value from a user defined NVDA idle time.
  Blocked by: https://github.com/nvaccess/nvda/issues/13915

### `getSpellingSpeech`(text: str, locale: Optional[str]=None, useCharacterDescriptions: bool=False) -> Generator[SequenceItemT, None, None]  — `ALL`  *(line 597)*

### `getState`()  — `ALL`  *(line 140)*

### `getTableInfoSpeech`(tableInfo: Optional[Dict[str, Any]], oldTableInfo: Optional[Dict[str, Any]], extraDetail: bool=False) -> SpeechSequence  — `ALL`  *(line 3082)*

### `getTextInfoSpeech`(info: textInfos.TextInfo, useCache: Union[bool, SpeakTextInfoState]=True, formatConfig: dict[str, bool | int] | None=None, unit: Optional[str]=None, reason: OutputReason=OutputReason.QUERY, _prefixSpeechCommand: Optional[SpeechCommand]=None, onlyInitialFields: bool=False, suppressBlanks: bool=False) -> Generator[SpeechSequence, None, bool]  — `changed 2026.1+`  *(line 1528)*

  **Signature history:**
  - **2023.3:** `getTextInfoSpeech(info: textInfos.TextInfo, useCache: Union[bool, SpeakTextInfoState]=True, formatConfig: Dict[str, bool]=None, unit: Optional[str]=None, reason: OutputReason=OutputReason.QUERY, _prefixSpeechCommand: Optional[SpeechCommand]=None, onlyInitialFields: bool=False, suppressBlanks: bool=False) -> Generator[SpeechSequence, None, bool]`
  - **2026.1+:** `getTextInfoSpeech(info: textInfos.TextInfo, useCache: Union[bool, SpeakTextInfoState]=True, formatConfig: dict[str, bool | int] | None=None, unit: Optional[str]=None, reason: OutputReason=OutputReason.QUERY, _prefixSpeechCommand: Optional[SpeechCommand]=None, onlyInitialFields: bool=False, suppressBlanks: bool=False) -> Generator[SpeechSequence, None, bool]`

### `initialize`()  — `ALL`  *(line 166)*

### `isBlank`(text)  — `ALL`  *(line 184)*

  Determine whether text should be reported as blank.
  @param text: The text in question.
  @type text: str
  @return: C{True} if the text is blank, C{False} if not.
  @rtype: bool

### `isFocusEditable`() -> bool  — `since 2025.3`  *(line 1396)*

  Check if the currently focused object is editable.
  :return: ``True`` if the focused object is editable, ``False`` otherwise.

### `pauseSpeech`(switch)  — `ALL`  *(line 241)*

### `processText`(locale: str, text: str, symbolLevel: characterProcessing.SymbolLevel, normalize: bool=False) -> str  — `changed 2024.4`  *(line 197)*

  **Signature history:**
  - **2023.3:** `processText(locale, text, symbolLevel)`
  - **2024.4:** `processText(locale: str, text: str, symbolLevel: characterProcessing.SymbolLevel, normalize: bool=False) -> str`

  Processes text for symbol pronunciation, speech dictionaries and Unicode normalization.
  :param locale: The language the given text is in, passed for symbol pronunciation.
  :param text: The text to process.
  :param symbolLevel: The verbosity level used for symbol pronunciation.
  :param normalize: Whether to apply Unicode normalization to the text
          after it has been processed for symbol pronunciation and speech dictionaries.
  :returns: The processed text

### `setSpeechMode`(newMode: SpeechMode)  — `ALL`  *(line 144)*

### `speak`(speechSequence: SpeechSequence, symbolLevel: characterProcessing.SymbolLevel | None=None, priority: Spri=Spri.NORMAL)  — `changed 2024.4`  *(line 1108)*

  **Signature history:**
  - **2023.3:** `speak(speechSequence: SpeechSequence, symbolLevel: Optional[int]=None, priority: Spri=Spri.NORMAL)`
  - **2024.4:** `speak(speechSequence: SpeechSequence, symbolLevel: characterProcessing.SymbolLevel | None=None, priority: Spri=Spri.NORMAL)`

  Speaks a sequence of text and speech commands
  @param speechSequence: the sequence of text and L{SpeechCommand} objects to speak
  @param symbolLevel: The symbol verbosity level; C{None} (default) to use the user's configuration.
  @param priority: The speech priority.

### `speakMessage`(text: str, priority: Optional[Spri]=None) -> None  — `ALL`  *(line 264)*

  Speaks a given message.
  @param text: the message to speak
  @param priority: The speech priority.

### `speakObject`(obj, reason: OutputReason=OutputReason.QUERY, _prefixSpeechCommand: Optional[SpeechCommand]=None, priority: Optional[Spri]=None)  — `ALL`  *(line 832)*

### `speakObjectProperties`(obj: 'NVDAObjects.NVDAObject', reason: OutputReason=OutputReason.QUERY, _prefixSpeechCommand: Optional[SpeechCommand]=None, priority: Optional[Spri]=None, **allowedProperties)  — `ALL`  *(line 658)*

### `speakPreselectedText`(text: str, priority: Optional[Spri]=None)  — `ALL`  *(line 1212)*

  Helper method to announce that a newly focused control already has
  text selected. This method is in contrast with L{speakTextSelected}.
  The method will speak the word "selected" with the provided text appended.
  The announcement order is different from L{speakTextSelected} in order to
  inform a user that the newly focused control has content that is selected,
  which they may unintentionally overwrite.
  
  @remarks: Implemented using L{getPreselectedTextSpeech}

### `speakSelectionChange`(oldInfo: textInfos.TextInfo, newInfo: textInfos.TextInfo, speakSelected: bool=True, speakUnselected: bool=True, generalize: bool=False, priority: Optional[Spri]=None)  — `ALL`  *(line 1298)*

  Speaks a change in selection, either selected or unselected text.
  @param oldInfo: a TextInfo instance representing what the selection was before
  @param newInfo: a TextInfo instance representing what the selection is now
  @param generalize: if True, then this function knows that the text may have changed between the creation of the oldInfo and newInfo objects, meaning that changes need to be spoken more generally, rather than speaking the specific text, as the bounds may be all wrong.
  @param priority: The speech priority.

### `speakSelectionMessage`(message: str, text: str, priority: Optional[Spri]=None)  — `ALL`  *(line 1269)*

### `speakSpelling`(text: str, locale: Optional[str]=None, useCharacterDescriptions: bool=False, priority: Optional[Spri]=None) -> None  — `ALL`  *(line 355)*

### `speakSsml`(ssml: str, markCallback: 'MarkCallbackT | None'=None, symbolLevel: characterProcessing.SymbolLevel | None=None, _prefixSpeechCommand: SpeechCommand | None=None, priority: Spri | None=None) -> None  — `since 2024.4`  *(line 299)*

  Speaks a given speech sequence provided as ssml.
  :param ssml: The SSML data to speak.
  :param markCallback: An optional callback called for every mark command in the SSML.
  :param symbolLevel: The symbol verbosity level.
  :param _prefixSpeechCommand: A SpeechCommand to append before the sequence.
  :param priority: The speech priority.

### `speakText`(text: str, reason: OutputReason=OutputReason.MESSAGE, symbolLevel: characterProcessing.SymbolLevel | None=None, priority: Spri | None=None)  — `changed 2024.4`  *(line 1007)*

  **Signature history:**
  - **2023.3:** `speakText(text: str, reason: OutputReason=OutputReason.MESSAGE, symbolLevel: Optional[int]=None, priority: Optional[Spri]=None)`
  - **2024.4:** `speakText(text: str, reason: OutputReason=OutputReason.MESSAGE, symbolLevel: characterProcessing.SymbolLevel | None=None, priority: Spri | None=None)`

  Speaks some text.
  @param text: The text to speak.
  @param reason: Unused
  @param symbolLevel: The symbol verbosity level; C{None} (default) to use the user's configuration.
  @param priority: The speech priority.

### `speakTextInfo`(info: textInfos.TextInfo, useCache: Union[bool, SpeakTextInfoState]=True, formatConfig: Dict[str, bool]=None, unit: Optional[str]=None, reason: OutputReason=OutputReason.QUERY, _prefixSpeechCommand: Optional[SpeechCommand]=None, onlyInitialFields: bool=False, suppressBlanks: bool=False, priority: Optional[Spri]=None) -> bool  — `ALL`  *(line 1497)*

### `speakTextSelected`(text: str, priority: Optional[Spri]=None)  — `ALL`  *(line 1252)*

  Helper method to announce that the user has caused text to be selected.
  This method is in contrast with L{speakPreselectedText}.
  The method will speak the provided text with the word "selected" appended.
  
  @remarks: Implemented using L{speakSelectionMessage}, which allows for
          speaking text with an arbitrary attached message.

### `speakTypedCharacters`(ch: str)  — `ALL`  *(line 1407)*

### `spellTextInfo`(info: textInfos.TextInfo, useCharacterDescriptions: bool=False, priority: Optional[Spri]=None) -> None  — `ALL`  *(line 333)*

  Spells the text from the given TextInfo, honouring any LangChangeCommand objects it finds if autoLanguageSwitching is enabled.

### `splitTextIndentation`(text)  — `ALL`  *(line 1027)*

  Splits indentation from the rest of the text.
  @param text: The text to split.
  @type text: str
  @return: Tuple of indentation and content.
  @rtype: (str, str)

---

## `speech/speechWithoutPauses.py`

### class `SpeechWithoutPauses`  — `ALL`  *(line 34)*

### `SpeechWithoutPauses.__init__`(self, speakFunc: Callable[[SpeechSequence], None])  — `ALL`  *(line 41)*

  :param speakFunc: Function used by L{speakWithoutPauses} to speak. This will likely be speech.speak.

### `SpeechWithoutPauses._detectBreaksAndGetSpeech`(self, speechSequence: SpeechSequence) -> Generator[SpeechSequence, None, bool]  — `ALL`  *(line 108)*

### `SpeechWithoutPauses._flushPendingSpeech`(self) -> SpeechSequence  — `ALL`  *(line 134)*

  @return: may be empty sequence

### `SpeechWithoutPauses._getSpeech`(self, speechSequence: SpeechSequence) -> SpeechSequence  — `ALL`  *(line 143)*

  @return: May be an empty sequence

### `SpeechWithoutPauses.getSpeechWithoutPauses`(self, speechSequence: Optional[SpeechSequence], detectBreaks: bool=True) -> Generator[SpeechSequence, None, bool]  — `ALL`  *(line 76)*

  Generate speech sequences over multiple calls,
  only returning a speech sequence at acceptable phrase or sentence boundaries,
  or when given None for the speech sequence.
  @return: The speech sequence that can be spoken without pauses. The 'return' for this generator function,
  is a bool which indicates whether this sequence should be considered valid speech. Use
  L{GeneratorWithReturn} to retain the return value. A generator is used because the previous
  implementation had several calls to speech, this approach replicates that.

### `SpeechWithoutPauses.reset`(self)  — `ALL`  *(line 51)*

### `SpeechWithoutPauses.speakWithoutPauses`(self, speechSequence: Optional[SpeechSequence], detectBreaks: bool=True) -> bool  — `ALL`  *(line 54)*

  Speaks the speech sequences given over multiple calls,
  only sending to the synth at acceptable phrase or sentence boundaries,
  or when given None for the speech sequence.
  @return: C{True} if something was actually spoken,
          C{False} if only buffering occurred.

### `_yieldIfNonEmpty`(seq: SpeechSequence)  — `ALL`  *(line 28)*

  Helper method to yield the sequence if it is not None or empty.

---

## `speech/types.py`

> Types used by speech package.
> Kept here so they can be re-used without having to worry about circular imports.

### class `GeneratorWithReturn`(Iterable)  — `ALL`  *(line 36)*

  Helper class, used with generator functions to access the 'return' value after there are no more values
  to iterate over.

### `GeneratorWithReturn.__init__`(self, gen: Iterable, defaultReturnValue=None)  — `ALL`  *(line 41)*

### `GeneratorWithReturn.__iter__`(self)  — `ALL`  *(line 46)*

### `_flattenNestedSequences`(nestedSequences: Union[Iterable[SpeechSequence], GeneratorWithReturn]) -> Generator[SequenceItemT, Any, Optional[bool]]  — `ALL`  *(line 51)*

  Turns [[a,b,c],[d,e]] into [a,b,c,d,e]

### `_isDebugForSpeech`() -> bool  — `ALL`  *(line 31)*

  Check if debug logging for speech is enabled.

### `logBadSequenceTypes`(sequence: SpeechIterable, raiseExceptionOnError=False) -> bool  — `ALL`  *(line 61)*

  Check if the provided sequence is valid, otherwise log an error (only if speech is
  checked in the "log categories" setting of the advanced settings panel.
  @param sequence: the sequence to check
  @param raiseExceptionOnError: if True, and exception is raised. Useful to help track down the introduction
          of erroneous speechSequence data.
  @return: True if the sequence is valid.

---

## `synthDriverHandler.py`

### class `LanguageInfo`(StringParameterInfo)  — `ALL`  *(line 33)*

  Holds information for a particular language

### `LanguageInfo.__init__`(self, id)  — `ALL`  *(line 36)*

  Given a language ID (locale name) the description is automatically calculated.

### class `SynthDriver`(driverHandler.Driver)  — `ALL`  *(line 54)*

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

### @classmethod `SynthDriver.InflectionSetting`(cls, minStep=1)  — `ALL`  *(line 206)*

  Factory function for creating inflection setting.

### @classmethod `SynthDriver.LanguageSetting`(cls)  — `ALL`  *(line 117)*

  Factory function for creating a language setting.

### @classmethod `SynthDriver.PitchSetting`(cls, minStep=1)  — `ALL`  *(line 193)*

  Factory function for creating pitch setting.

### @classmethod `SynthDriver.RateBoostSetting`(cls)  — `ALL`  *(line 166)*

  Factory function for creating rate boost setting.

### @classmethod `SynthDriver.RateSetting`(cls, minStep=1)  — `ALL`  *(line 153)*

  Factory function for creating rate setting.

### @classmethod `SynthDriver.UseWasapiSetting`(cls) -> BooleanDriverSetting  — `since 2025.3`  *(line 219)*

  Factory function for creating 'Use WASAPI' setting.

### @classmethod `SynthDriver.VariantSetting`(cls)  — `ALL`  *(line 141)*

  Factory function for creating variant setting.

### @classmethod `SynthDriver.VoiceSetting`(cls)  — `ALL`  *(line 129)*

  Factory function for creating voice setting.

### @classmethod `SynthDriver.VolumeSetting`(cls, minStep=1)  — `ALL`  *(line 179)*

  Factory function for creating volume setting.

### `SynthDriver._getAvailableVariants`(self)  — `ALL`  *(line 304)*

  fetches an ordered dictionary of variants that the synth supports, keyed by ID
  @returns: an ordered dictionary of L{VoiceInfo} instances representing the available variants
  @rtype: OrderedDict

### `SynthDriver._getAvailableVoices`(self) -> OrderedDict[str, VoiceInfo]  — `ALL`  *(line 257)*

  fetches an ordered dictionary of voices that the synth supports.
  @returns: an OrderedDict of L{VoiceInfo} instances representing the available voices, keyed by ID

### `SynthDriver._get_availableLanguages`(self) -> set[str | None]  — `changed 2026.1+`  *(line 248)*

  **Signature history:**
  - **2023.3:** `SynthDriver._get_availableLanguages(self) -> Set[Optional[str]]`
  - **2026.1+:** `SynthDriver._get_availableLanguages(self) -> set[str | None]`

### `SynthDriver._get_availableVariants`(self)  — `ALL`  *(line 311)*

### `SynthDriver._get_availableVoices`(self) -> OrderedDict[str, VoiceInfo]  — `ALL`  *(line 263)*

### `SynthDriver._get_inflection`(self)  — `ALL`  *(line 316)*

### `SynthDriver._get_initialSettingsRingSetting`(self)  — `ALL`  *(line 401)*

### `SynthDriver._get_language`(self) -> str | None  — `changed 2026.1+`  *(line 242)*

  **Signature history:**
  - **2023.3:** `SynthDriver._get_language(self) -> Optional[str]`
  - **2026.1+:** `SynthDriver._get_language(self) -> str | None`

### `SynthDriver._get_pitch`(self) -> int  — `changed 2024.4`  *(line 282)*

  **Signature history:**
  - **2023.3:** `SynthDriver._get_pitch(self)`
  - **2024.4:** `SynthDriver._get_pitch(self) -> int`

### `SynthDriver._get_rate`(self) -> int  — `changed 2024.4`  *(line 272)*

  **Signature history:**
  - **2023.3:** `SynthDriver._get_rate(self)`
  - **2024.4:** `SynthDriver._get_rate(self) -> int`

### `SynthDriver._get_variant`(self)  — `ALL`  *(line 298)*

### `SynthDriver._get_voice`(self)  — `ALL`  *(line 251)*

### `SynthDriver._get_volume`(self) -> int  — `changed 2024.4`  *(line 292)*

  **Signature history:**
  - **2023.3:** `SynthDriver._get_volume(self)`
  - **2024.4:** `SynthDriver._get_volume(self) -> int`

### `SynthDriver._set_inflection`(self, value)  — `ALL`  *(line 319)*

### `SynthDriver._set_language`(self, language)  — `ALL`  *(line 245)*

### `SynthDriver._set_pitch`(self, value: int)  — `changed 2024.4`  *(line 285)*

  **Signature history:**
  - **2023.3:** `SynthDriver._set_pitch(self, value)`
  - **2024.4:** `SynthDriver._set_pitch(self, value: int)`

### `SynthDriver._set_rate`(self, value: int)  — `changed 2024.4`  *(line 275)*

  **Signature history:**
  - **2023.3:** `SynthDriver._set_rate(self, value)`
  - **2024.4:** `SynthDriver._set_rate(self, value: int)`

### `SynthDriver._set_variant`(self, value)  — `ALL`  *(line 301)*

### `SynthDriver._set_voice`(self, value)  — `ALL`  *(line 254)*

### `SynthDriver._set_volume`(self, value: int)  — `changed 2024.4`  *(line 295)*

  **Signature history:**
  - **2023.3:** `SynthDriver._set_volume(self, value)`
  - **2024.4:** `SynthDriver._set_volume(self, value: int)`

### `SynthDriver.cancel`(self)  — `ALL`  *(line 239)*

  Silence speech immediately.

### `SynthDriver.initSettings`(self)  — `ALL`  *(line 348)*

### `SynthDriver.languageIsSupported`(self, lang: str | None) -> bool  — `since 2025.3`  *(line 329)*

  Determines if the specified language is supported.
  :param lang: A language code or None.
  :return: ``True`` if the language is supported, ``False`` otherwise.

### `SynthDriver.loadSettings`(self, onlyChanged=False)  — `ALL`  *(line 370)*

### `SynthDriver.pause`(self, switch)  — `ALL`  *(line 322)*

  Pause or resume speech output.
  @param switch: C{True} to pause, C{False} to resume (unpause).
  @type switch: bool

### `SynthDriver.speak`(self, speechSequence)  — `ALL`  *(line 231)*

  Speaks the given sequence of text and speech commands.
  @param speechSequence: a list of text strings and SynthCommand objects (such as index and parameter changes).
  @type speechSequence: list of string and L{SynthCommand}

### class `VoiceInfo`(StringParameterInfo)  — `ALL`  *(line 42)*

  Provides information about a single synthesizer voice.

### `VoiceInfo.__init__`(self, id, displayName, language: str | None=None)  — `changed 2026.1+`  *(line 45)*

  **Signature history:**
  - **2023.3:** `VoiceInfo.__init__(self, id, displayName, language: Optional[str]=None)`
  - **2026.1+:** `VoiceInfo.__init__(self, id, displayName, language: str | None=None)`

  @param language: The ID of the language this voice speaks,
          C{None} if not known or the synth implements language separate from voices.

### `_getSynthDriver`(name: str) -> type[SynthDriver]  — `changed 2026.1+`  *(line 436)*

  **Signature history:**
  - **2023.3:** `_getSynthDriver(name) -> SynthDriver`
  - **2026.1+:** `_getSynthDriver(name: str) -> type[SynthDriver]`

### `changeVoice`(synth, voice)  — `ALL`  *(line 423)*

### `findAndSetNextSynth`(currentSynthName: str, *, _leftToTry: list[str] | None=None) -> bool  — `changed 2026.1+`  *(line 546)*

  **Signature history:**
  - **2023.3:** `findAndSetNextSynth(currentSynthName: str) -> bool`
  - **2026.1+:** `findAndSetNextSynth(currentSynthName: str, *, _leftToTry: list[str] | None=None) -> bool`

  Finds the first untried synth in ``defaultSynthPriorityList`` and switches to it.
  
  :param currentSynthName: The name of the synth driver that was just tried.
          Only used if ``_leftToTry`` is ``None``.
  :param _leftToTry: A list of synth drivers that haven't been tried yet, in reverse order of priority. Defaults to ``None``.
          If ``None``, the list of synths left to try will be calculated automatically.
  :return: ``True`` if an attempt was made to switch to a synth driver; ``False`` if there are no more drivers in ``defaultSynthPriorityList `` to try.

### `getSynth`() -> SynthDriver | None  — `changed 2026.1+`  *(line 470)*

  **Signature history:**
  - **2023.3:** `getSynth() -> Optional[SynthDriver]`
  - **2026.1+:** `getSynth() -> SynthDriver | None`

### `getSynthInstance`(name: str, asDefault: bool=False)  — `changed 2026.1+`  *(line 474)*

  **Signature history:**
  - **2023.3:** `getSynthInstance(name, asDefault=False)`
  - **2026.1+:** `getSynthInstance(name: str, asDefault: bool=False)`

### `getSynthList`() -> list[tuple[str, str]]  — `changed 2026.1+`  *(line 440)*

  **Signature history:**
  - **2023.3:** `getSynthList() -> List[Tuple[str, str]]`
  - **2026.1+:** `getSynthList() -> list[tuple[str, str]]`

### `handlePostConfigProfileSwitch`(resetSpeechIfNeeded=True)  — `ALL`  *(line 566)*

  Switches synthesizers and or applies new voice settings to the synth due to a config profile switch.
  @var resetSpeechIfNeeded: if true and a new synth will be loaded, speech queues are fully reset first.
  This is what happens by default.
  However, Speech itself may call this with false internally if this is a config profile switch within a
  currently processing speech sequence.
  @type resetSpeechIfNeeded: bool

### `initialize`()  — `ALL`  *(line 419)*

### `isDebugForSynthDriver`()  — `ALL`  *(line 587)*

### `setSynth`(name: str | None, isFallback: bool=False, *, _leftToTry: list[str] | None=None) -> bool  — `changed 2026.1+`  *(line 488)*

  **Signature history:**
  - **2023.3:** `setSynth(name: Optional[str], isFallback: bool=False)`
  - **2026.1+:** `setSynth(name: str | None, isFallback: bool=False, *, _leftToTry: list[str] | None=None) -> bool`

  Set the currently active speech synth by name.
  
  If the chosen synth cannot be used, this function will attempt to fall back to another synth.
  Fallback synths are tried in the order given in :var:`defaultSynthPriorityList `.
  
  :param name: The name of the synth driver to use.
  :param isFallback: Whether this synth is a fallback, i.e. it isn't the synth that the user wants. Defaults to ``False``.
  :param _leftToTry: List of synth names to try falling back to, in reverse order of priority. Defaults to ``None``.
          If ``None``, the list will be calculated automatically.
  :return: ``True`` if switching to the named synthesizer succeeds; ``False`` otherwise.

---

## `synthDrivers/_espeak.py`

### class `BgThread`(threading.Thread)  — `ALL`  *(line 199)*

### `BgThread.__init__`(self)  — `ALL`  *(line 200)*

### `BgThread.run`(self)  — `ALL`  *(line 204)*

### `_execWhenDone`(func, *args, mustBeAsync=False, **kwargs)  — `ALL`  *(line 217)*

### `_setVoiceAndVariant`(voice=None, variant=None)  — `ALL`  *(line 308)*

### `_setVoiceByLanguage`(lang)  — `ALL`  *(line 331)*

### `_speak`(text)  — `ALL`  *(line 227)*

### `callback`(wav, numsamples, event)  — `ALL`  *(line 150)*

### `decodeEspeakString`(data)  — `ALL`  *(line 142)*

### `encodeEspeakString`(text)  — `ALL`  *(line 138)*

### class `espeak_EVENT`(Structure)  — `ALL`  *(line 98)*

### class `espeak_EVENT_id`(Union)  — `ALL`  *(line 90)*

### class `espeak_VOICE`(Structure)  — `ALL`  *(line 111)*

### `espeak_VOICE.__eq__`(self, other)  — `ALL`  *(line 124)*

### `espeak_VOICE.__hash__`(self)  — `ALL`  *(line 129)*

### `espeak_errcheck`(res, func, args)  — `ALL`  *(line 346)*

### `getCurrentVoice`()  — `ALL`  *(line 291)*

### `getParameter`(param, current)  — `ALL`  *(line 277)*

### `getVariantDict`()  — `ALL`  *(line 413)*

### `getVoiceList`()  — `ALL`  *(line 281)*

### `info`()  — `ALL`  *(line 408)*

### `initialize`(indexCallback=None)  — `ALL`  *(line 352)*

  @param indexCallback: A function which is called when eSpeak reaches an index.
          It is called with one argument:
          the number of the index or C{None} when speech stops.

### `pause`(switch)  — `ALL`  *(line 268)*

### `setParameter`(param, value, relative)  — `ALL`  *(line 273)*

### `setVoice`(voice)  — `ALL`  *(line 299)*

### `setVoiceAndVariant`(voice=None, variant=None)  — `ALL`  *(line 327)*

### `setVoiceByLanguage`(lang)  — `ALL`  *(line 342)*

### `setVoiceByName`(name)  — `ALL`  *(line 304)*

### `speak`(text)  — `ALL`  *(line 243)*

### `stop`()  — `ALL`  *(line 248)*

### `terminate`()  — `ALL`  *(line 394)*

---

## `synthDrivers/espeak.py`

### class `SynthDriver`(SynthDriver)  — `ALL`  *(line 37)*

### `SynthDriver.__init__`(self)  — `ALL`  *(line 214)*

### `SynthDriver._getAvailableVariants`(self)  — `ALL`  *(line 488)*

### `SynthDriver._getAvailableVoices`(self)  — `ALL`  *(line 436)*

### `SynthDriver._get_inflection`(self)  — `ALL`  *(line 422)*

### `SynthDriver._get_language`(self)  — `ALL`  *(line 226)*

### `SynthDriver._get_pitch`(self)  — `ALL`  *(line 414)*

### `SynthDriver._get_rate`(self)  — `ALL`  *(line 402)*

### `SynthDriver._get_rateBoost`(self)  — `ALL`  *(line 392)*

### `SynthDriver._get_variant`(self)  — `ALL`  *(line 481)*

### `SynthDriver._get_voice`(self)  — `ALL`  *(line 447)*

### `SynthDriver._get_volume`(self) -> int  — `ALL`  *(line 430)*

### `SynthDriver._handleLangChangeCommand`(self, langChangeCommand: LangChangeCommand, langChanged: bool) -> str  — `ALL`  *(line 296)*

  Get language xml tags needed to handle a lang change command.
  - if a language change has already been handled for this speech,
  close the open voice tag.
  - if the language is supported by eSpeak, switch to that language.
  - otherwise, switch to the default synthesizer language.

### `SynthDriver._normalizeLangCommand`(self, command: LangChangeCommand) -> LangChangeCommand  — `ALL`  *(line 252)*

  Checks if a LangChangeCommand language is compatible with eSpeak.
  If not, find a default mapping occurs in L{_defaultLangToLocale}.
  Otherwise, finds a language of a different dialect exists (e.g. ru-ru to ru).
  Returns an eSpeak compatible LangChangeCommand.

### `SynthDriver._onIndexReached`(self, index)  — `ALL`  *(line 472)*

### `SynthDriver._processText`(self, text)  — `ALL`  *(line 241)*

### `SynthDriver._set_inflection`(self, val)  — `ALL`  *(line 426)*

### `SynthDriver._set_pitch`(self, pitch)  — `ALL`  *(line 418)*

### `SynthDriver._set_rate`(self, rate)  — `ALL`  *(line 408)*

### `SynthDriver._set_rateBoost`(self, enable)  — `ALL`  *(line 395)*

### `SynthDriver._set_variant`(self, val)  — `ALL`  *(line 484)*

### `SynthDriver._set_voice`(self, identifier)  — `ALL`  *(line 457)*

### `SynthDriver._set_volume`(self, volume: int)  — `ALL`  *(line 433)*

### `SynthDriver.cancel`(self)  — `ALL`  *(line 383)*

### @classmethod `SynthDriver.check`(cls)  — `ALL`  *(line 211)*

### `SynthDriver.pause`(self, switch)  — `ALL`  *(line 386)*

### `SynthDriver.speak`(self, speechSequence: SpeechSequence)  — `ALL`  *(line 323)*

### `SynthDriver.terminate`(self)  — `ALL`  *(line 478)*

---

## `synthDrivers/oneCore.py`

> Synth driver for Windows OneCore voices.

### class `OneCoreSynthDriver`(SynthDriver)  — `ALL`  *(line 172)*

### `OneCoreSynthDriver.__init__`(self)  — `ALL`  *(line 219)*

### `OneCoreSynthDriver._callback`(self, bytes, len, markers)  — `ALL`  *(line 444)*

### `OneCoreSynthDriver._getAvailableVoices`(self)  — `ALL`  *(line 503)*

### `OneCoreSynthDriver._getDefaultVoice`(self, pickAny: bool=True) -> str  — `ALL`  *(line 590)*

  Finds the best available voice that can be used as a default.
  It first tries finding a voice with the same language as the user's configured NVDA language
  else one that matches the system language.
  else any voice if pickAny is True.
  Uses the Windows locale (eg en_AU) to provide country information for the voice where possible.
  @returns: the ID of the voice, suitable for passing to self.voice for setting.

### `OneCoreSynthDriver._getVoiceInfoFromOnecoreVoiceString`(self, voiceStr)  — `ALL`  *(line 494)*

  Produces an NVDA VoiceInfo object representing the given voice string from Onecore speech.

### `OneCoreSynthDriver._get_pitch`(self)  — `ALL`  *(line 330)*

### `OneCoreSynthDriver._get_rate`(self)  — `ALL`  *(line 356)*

### `OneCoreSynthDriver._get_rateBoost`(self)  — `ALL`  *(line 373)*

### `OneCoreSynthDriver._get_supportedSettings`(self)  — `ALL`  *(line 204)*

### `OneCoreSynthDriver._get_supportsProsodyOptions`(self)  — `ALL`  *(line 200)*

### `OneCoreSynthDriver._get_voice`(self)  — `ALL`  *(line 578)*

### `OneCoreSynthDriver._get_volume`(self) -> int  — `ALL`  *(line 343)*

### `OneCoreSynthDriver._handleSpeechFailure`(self)  — `ALL`  *(line 432)*

### `OneCoreSynthDriver._isVoiceValid`(self, ID: str) -> bool  — `ALL`  *(line 518)*

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

### `OneCoreSynthDriver._maybeInitPlayer`(self, wav)  — `ALL`  *(line 254)*

  Initialize audio playback based on the wave header provided by the synthesizer.
  If the sampling rate has not changed, the existing player is used.
  Otherwise, a new one is created with the appropriate parameters.

### @classmethod `OneCoreSynthDriver._percentToParam`(cls, percent, min, max)  — `changed 2025.3`  *(line 326)*

  **Signature history:**
  - **2023.3:** `OneCoreSynthDriver._percentToParam(self, percent, min, max)`
  - **2025.3:** `OneCoreSynthDriver._percentToParam(cls, percent, min, max)`

  Overrides SynthDriver._percentToParam to return floating point parameter values.

### `OneCoreSynthDriver._processQueue`(self)  — `ALL`  *(line 385)*

### `OneCoreSynthDriver._queueSpeech`(self, item: str) -> None  — `ALL`  *(line 319)*

### `OneCoreSynthDriver._set_pitch`(self, pitch)  — `ALL`  *(line 336)*

### `OneCoreSynthDriver._set_rate`(self, rate)  — `ALL`  *(line 363)*

### `OneCoreSynthDriver._set_rateBoost`(self, enable)  — `ALL`  *(line 376)*

### `OneCoreSynthDriver._set_voice`(self, id)  — `ALL`  *(line 581)*

### `OneCoreSynthDriver._set_volume`(self, volume: int)  — `ALL`  *(line 349)*

### `OneCoreSynthDriver.cancel`(self)  — `ALL`  *(line 285)*

### @classmethod `OneCoreSynthDriver.check`(cls)  — `ALL`  *(line 196)*

### `OneCoreSynthDriver.pause`(self, switch)  — `ALL`  *(line 626)*

### `OneCoreSynthDriver.speak`(self, speechSequence: SpeechSequence) -> None  — `ALL`  *(line 299)*

### `OneCoreSynthDriver.terminate`(self)  — `ALL`  *(line 274)*

### class `VoiceUnsupportedError`(RuntimeError)  — `ALL`  *(line 635)*

### class `_OcPreAPI5SsmlConverter`(_OcSsmlConverter)  — `ALL`  *(line 124)*

### `_OcPreAPI5SsmlConverter.__init__`(self, defaultLanguage: str, availableLanguages: Set[str], rate: float, pitch: float, volume: float)  — `ALL`  *(line 125)*

  Used for older OneCore installations (OneCore API < 5),
  where supportsProsodyOptions is False.
  This means we must initially set a good default for rate, volume and pitch,
  as this can't be changed after initialization.
  
  @param defaultLanguage: language with locale, installed by OneCore (e.g. 'en_US')
  @param availableLanguages: languages with locale, installed by OneCore (e.g. 'zh_HK', 'en_US')
  @param rate: from 0-100
  @param pitch: from 0-100
  @param volume: from 0-100

### `_OcPreAPI5SsmlConverter.convertPitchCommand`(self, command)  — `ALL`  *(line 165)*

### `_OcPreAPI5SsmlConverter.convertRateCommand`(self, command)  — `ALL`  *(line 162)*

### `_OcPreAPI5SsmlConverter.convertVolumeCommand`(self, command)  — `ALL`  *(line 168)*

### `_OcPreAPI5SsmlConverter.generateBalancerCommands`(self, speechSequence: SpeechSequence) -> Generator[Any, None, None]  — `ALL`  *(line 150)*

### class `_OcSsmlConverter`(speechXml.SsmlConverter)  — `ALL`  *(line 60)*

### `_OcSsmlConverter.__init__`(self, defaultLanguage: str, availableLanguages: Set[str])  — `ALL`  *(line 61)*

  Used for newer OneCore installations (OneCore API > 5)
  where supportsProsodyOptions is True.
  This allows for changing rate, volume and pitch after initialization.
  
  @param defaultLanguage: language with locale, installed by OneCore (e.g. 'en_US')
  @param availableLanguages: languages with locale, installed by OneCore (e.g. 'zh_HK', 'en_US')

### `_OcSsmlConverter._convertProsody`(self, command, attr, default, base=None)  — `ALL`  *(line 80)*

### `_OcSsmlConverter.convertCharacterModeCommand`(self, command)  — `ALL`  *(line 101)*

### `_OcSsmlConverter.convertLangChangeCommand`(self, command: LangChangeCommand) -> Optional[speechXml.SetAttrCommand]  — `ALL`  *(line 106)*

### `_OcSsmlConverter.convertPitchCommand`(self, command)  — `ALL`  *(line 95)*

### `_OcSsmlConverter.convertRateCommand`(self, command)  — `ALL`  *(line 92)*

### `_OcSsmlConverter.convertVolumeCommand`(self, command)  — `ALL`  *(line 98)*

---

## `synthDrivers/sapi4_32.py`  — `NEW MODULE since 2026.1+`

### class `SynthDriver`(SynthDriverProxy32)  — `since 2026.1+`  *(line 15)*

### @classmethod `SynthDriver.check`(cls)  — `since 2026.1+`  *(line 23)*

---

## `synthDrivers/sapi5.py`

### class `SPAudioState`(IntEnum)  — `until 2024.4`  *(line 34)*

### class `SapiSink`(COMObject)  — `ALL`  *(line 445)*

  Implements ISpNotifySink to handle SAPI event notifications.
  Should be passed to ISpNotifySource::SetNotifySink().
  Notifications will be sent on the original thread,
  instead of being routed to the main thread.

### `SapiSink.Bookmark`(self, streamNum: int, pos: int, bookmark: str, bookmarkId: int)  — `changed 2025.3`  *(line 489)*

  **Signature history:**
  - **2023.3:** `SapiSink.Bookmark(self, streamNum, pos, bookmark, bookmarkId)`
  - **2025.3:** `SapiSink.Bookmark(self, streamNum: int, pos: int, bookmark: str, bookmarkId: int)`

### `SapiSink.EndStream`(self, streamNum: int, pos: int)  — `changed 2025.3`  *(line 501)*

  **Signature history:**
  - **2023.3:** `SapiSink.EndStream(self, streamNum, pos)`
  - **2025.3:** `SapiSink.EndStream(self, streamNum: int, pos: int)`

### `SapiSink.ISpNotifySink_Notify`(self)  — `since 2025.3`  *(line 458)*

  This is called when there's a new event notification.
  Queued events will be retrieved.

### `SapiSink.StartStream`(self, streamNum: int, pos: int)  — `changed 2025.3`  *(line 480)*

  **Signature history:**
  - **2023.3:** `SapiSink.StartStream(self, streamNum, pos)`
  - **2025.3:** `SapiSink.StartStream(self, streamNum: int, pos: int)`

### `SapiSink.__init__`(self, synthRef: weakref.ReferenceType['SynthDriver'])  — `changed 2025.3`  *(line 455)*

  **Signature history:**
  - **2023.3:** `SapiSink.__init__(self, synthRef: weakref.ReferenceType)`
  - **2025.3:** `SapiSink.__init__(self, synthRef: weakref.ReferenceType['SynthDriver'])`

### `SapiSink.onIndexReached`(self, streamNum: int, index: int)  — `since 2025.3`  *(line 518)*

### class `SpeechVoiceEvents`(IntEnum)  — `ALL`  *(line 84)*

### class `SpeechVoiceSpeakFlags`(IntEnum)  — `ALL`  *(line 77)*

### class `SynthDriver`(SynthDriver)  — `ALL`  *(line 532)*

### `SynthDriver.__getattr__`(self, attrName: str) -> Any  — `since 2025.3`  *(line 1088)*

  This is used to reserve backward compatibility.

### `SynthDriver.__init__`(self, _defaultVoiceToken=None)  — `ALL`  *(line 574)*

  @param _defaultVoiceToken: an optional sapi voice token which should be used as the default voice (only useful for subclasses)
  @type _defaultVoiceToken: ISpeechObjectToken

### `SynthDriver._convertPhoneme`(self, ipa)  — `ALL`  *(line 794)*

### `SynthDriver._getAvailableVoices`(self)  — `ALL`  *(line 620)*

### `SynthDriver._getVoiceTokens`(self)  — `ALL`  *(line 638)*

  Provides a collection of sapi5 voice tokens. Can be overridden by subclasses if tokens should be looked for in some other registry location.

### `SynthDriver._get_lastIndex`(self)  — `ALL`  *(line 660)*

### `SynthDriver._get_pitch`(self)  — `ALL`  *(line 648)*

### `SynthDriver._get_rate`(self)  — `ALL`  *(line 642)*

### `SynthDriver._get_rateBoost`(self)  — `since 2025.3`  *(line 645)*

### `SynthDriver._get_useWasapi`(self) -> bool  — `since 2025.3`  *(line 657)*

### `SynthDriver._get_voice`(self)  — `ALL`  *(line 654)*

### `SynthDriver._get_volume`(self) -> int  — `ALL`  *(line 651)*

### `SynthDriver._initLegacyAudio`(self)  — `since 2025.3`  *(line 724)*

### `SynthDriver._initTts`(self, voice: str | None=None)  — `changed 2025.3`  *(line 735)*

  **Signature history:**
  - **2023.3:** `SynthDriver._initTts(self, voice=None)`
  - **2025.3:** `SynthDriver._initTts(self, voice: str | None=None)`

### `SynthDriver._initWasapiAudio`(self)  — `since 2025.3`  *(line 705)*

### `SynthDriver._onEndStream`(self) -> None  — `since 2025.3`  *(line 820)*

  Common handling when a speech stream ends.

### @classmethod `SynthDriver._percentToParam`(cls, percent, min, max) -> float  — `since 2025.3`  *(line 668)*

  Overrides SynthDriver._percentToParam to return floating point parameter values.

### `SynthDriver._percentToPitch`(self, percent)  — `ALL`  *(line 786)*

### `SynthDriver._percentToRate`(self, percent)  — `ALL`  *(line 672)*

### `SynthDriver._requestCompleted`(self) -> bool  — `since 2025.3`  *(line 817)*

### `SynthDriver._requestsAvailable`(self) -> bool  — `since 2025.3`  *(line 814)*

### `SynthDriver._set_pitch`(self, value)  — `ALL`  *(line 697)*

### `SynthDriver._set_rate`(self, rate)  — `ALL`  *(line 675)*

### `SynthDriver._set_rateBoost`(self, enable: bool)  — `since 2025.3`  *(line 690)*

### `SynthDriver._set_useWasapi`(self, value: bool)  — `since 2025.3`  *(line 780)*

### `SynthDriver._set_voice`(self, value)  — `ALL`  *(line 764)*

### `SynthDriver._set_volume`(self, value)  — `ALL`  *(line 701)*

### `SynthDriver._speakThread`(self)  — `since 2025.3`  *(line 834)*

  Thread that processes speech when WASAPI is enabled.

### `SynthDriver._speak_legacy`(self, text: str, flags: int) -> int  — `since 2025.3`  *(line 1002)*

  Legacy way of calling SpVoice.Speak that uses a temporary audio ducker.

### `SynthDriver._stopThread`(self) -> None  — `since 2025.3`  *(line 600)*

  Stops the WASAPI speak thread (if it's running) and waits for the thread to quit.

### `SynthDriver.cancel`(self)  — `ALL`  *(line 1044)*

### @classmethod `SynthDriver.check`(cls)  — `ALL`  *(line 561)*

### `SynthDriver.pause`(self, switch: bool)  — `ALL`  *(line 1064)*

### `SynthDriver.speak`(self, speechSequence)  — `ALL`  *(line 885)*

### `SynthDriver.terminate`(self)  — `ALL`  *(line 613)*

### class `SynthDriverAudioStream`(COMObject)  — `since 2025.3`  *(line 231)*

  Implements ISpAudio, ISpEventSource, and ISpEventSink.
  ISpAudio extends IStream which is used to stream in audio data,
  and also has `SetFormat` to tell the audio object what wave format is preferred.
  Should be set as the audio output via `ISpAudio::SetOutput`.
  ISpEventSource and ISpEventSink are also required for `SetOutput` to work,
  although we only need to pass the event from the sink to the source,
  and leave most functions unimplemented.

### `SynthDriverAudioStream.ISequentialStream_RemoteWrite`(self, this: int, pv: _Pointer[c_ubyte], cb: int, pcbWritten: _Pointer[c_ulong]) -> int  — `since 2025.3`  *(line 252)*

  This is called when SAPI wants to write (output) a wave data chunk.
  
  :param pv: A pointer to the first wave data byte.
  :param cb: The number of bytes to write.
  :param pcbWritten: A pointer to a variable where the actual number of bytes written will be stored.
          Can be null.
  :returns: HRESULT code.

### `SynthDriverAudioStream.ISpAudio_EventHandle`(self) -> int  — `since 2025.3`  *(line 386)*

### `SynthDriverAudioStream.ISpAudio_GetDefaultFormat`(self) -> tuple[GUID, _Pointer[WAVEFORMATEX]]  — `since 2025.3`  *(line 375)*

  Returns the default format that is guaranteed to work on this audio object.
  
  :returns: A tuple of a GUID, which should always be SPDFID_WaveFormatEx,
          and a pointer to a WAVEFORMATEX structure, allocated by CoTaskMemAlloc.

### `SynthDriverAudioStream.ISpAudio_SetFormat`(self, rguidFmtId: _Pointer[GUID], pWaveFormatEx: _Pointer[WAVEFORMATEX])  — `since 2025.3`  *(line 343)*

  This is called when SAPI wants to tell us what wave format we should use.
  We can get the best format for the specific voice here.
  
  :param rguidFmtId: Format GUID. Should be SPDFID_WaveFormatEx.
  :param pWaveFormatEx: Pointer to a WAVEFORMATEX structure.
          We should copy the data to our own structure to keep the format data.

### `SynthDriverAudioStream.ISpAudio_SetState`(self, NewState: SPAUDIOSTATE, ullReserved: int) -> None  — `since 2025.3`  *(line 339)*

  This is called when the audio state changes, for example, when the audio stream is paused or closed.

### `SynthDriverAudioStream.ISpEventSink_AddEvents`(self, pEventArray: _Pointer[SPEVENT], ulCount: int) -> None  — `since 2025.3`  *(line 427)*

  SAPI will send all events to our ISpAudio implementation,
  such as StartStream events and Bookmark events.
  To let the ISpVoice client get notified as well, we should store those events,
  then pass the events to the ISpNotifySink we got earlier.
  
  :param pEventArray: Pointer to an array of SPEVENT structures.
  :param ulCount: Number of events.

### `SynthDriverAudioStream.ISpEventSource_GetEvents`(self, this: int, ulCount: int, pEventArray: _Pointer[SPEVENT], pulFetched: _Pointer[c_ulong]) -> None  — `since 2025.3`  *(line 406)*

  Send the events that was passed in via AddEvents back to the event sink.
  Events that has been retrieved will be removed.
  
  :param ulCount: The maximum number of events pEventArray can hold.
  :param pEventArray: Pointer to an array of SPEVENT structures
          that is used to receive the event data.
  :param pulFetched: Used to store the actual number of events fetched.
          This pointer can be NULL when ulCount is 1.

### `SynthDriverAudioStream.ISpEventSource_SetInterest`(self, ullEventInterest: int, ullQueuedInterest: int) -> None  — `since 2025.3`  *(line 397)*

  SAPI uses this to tell us the types of events it is interested in.
  We just ignore this and assume that it's interested in everything.
  
  :param ullEventInterest: Types of events that should cause ISpNotifySink::Notify() to be called.
  :param ullQueuedInterest: Types of events than should be stored in the event queue
          and can be retrieved later with ISpEventSource::GetEvents().

### `SynthDriverAudioStream.ISpNotifySource_GetNotifyEventHandle`(self) -> int  — `since 2025.3`  *(line 394)*

### `SynthDriverAudioStream.ISpNotifySource_SetNotifySink`(self, pNotifySink: _Pointer[ISpNotifySink]) -> None  — `since 2025.3`  *(line 389)*

  SAPI will pass in an ISpNotifySink pointer to be notified of events.
  We just need to pass the events we have received back to the sink.

### `SynthDriverAudioStream.ISpStreamFormat_GetFormat`(self, pguidFormatId: _Pointer[GUID]) -> _Pointer[WAVEFORMATEX]  — `since 2025.3`  *(line 322)*

  This is called when SAPI wants to get the current wave format.
  
  :param pguidFormatId: Receives the current format GUID.
          Should be SPDFID_WaveFormatEx for WAVEFORMATEX formats.
          This parameter is incorrectly marked as "in" by comtypes,
          but is actually an out parameter.
  :returns: Pointer to a WAVEFORMATEX structure that is allocated by CoTaskMemAlloc.

### `SynthDriverAudioStream.IStream_Commit`(self, grfCommitFlags: int)  — `since 2025.3`  *(line 317)*

  This is called when MSSP wants to flush the written data.
  Does nothing.

### `SynthDriverAudioStream.IStream_RemoteSeek`(self, this: int, dlibMove: _LARGE_INTEGER, dwOrigin: int, plibNewPosition: _Pointer[_ULARGE_INTEGER]) -> int  — `since 2025.3`  *(line 292)*

  This is called when SAPI wants to get the current stream position.
  Seeking to another position is not supported.
  
  :param dlibMove: The displacement to be added to the location indicated by the dwOrigin parameter.
          Only 0 is supported.
  :param dwOrigin: The origin for the displacement specified in dlibMove.
          Only 1 (STREAM_SEEK_CUR) is supported.
  :param plibNewPosition: A pointer to a ULARGE_INTEGER where the current stream position will be stored.
          Can be null.
  :returns: HRESULT code.

### `SynthDriverAudioStream.__init__`(self, synthRef: weakref.ReferenceType['SynthDriver'])  — `since 2025.3`  *(line 244)*

### @staticmethod `SynthDriverAudioStream._writeDefaultFormat`(wfx: WAVEFORMATEX) -> None  — `since 2025.3`  *(line 365)*

  Put the default format into wfx. The default format is 48kHz 16-bit stereo.

### class `_SPAudioState`(IntEnum)  — `since 2025.3`  *(line 69)*

### class `_SPEventEnum`(IntEnum)  — `since 2025.3`  *(line 140)*

### class `_SPEventLParamType`(IntEnum)  — `since 2025.3`  *(line 131)*

### class `_SapiEvent`(SPEVENT)  — `since 2025.3`  *(line 165)*

  Enhanced version of the SPEVENT structure that supports freeing lParam data automatically.

### `_SapiEvent.__del__`(self)  — `since 2025.3`  *(line 176)*

### `_SapiEvent.clear`(self) -> None  — `since 2025.3`  *(line 168)*

  Clear and free related data.

### @staticmethod `_SapiEvent.copy`(dst: SPEVENT, src: SPEVENT) -> None  — `since 2025.3`  *(line 180)*

### `_SapiEvent.copyFrom`(self, src: SPEVENT) -> None  — `since 2025.3`  *(line 202)*

### `_SapiEvent.copyTo`(self, dst: SPEVENT) -> None  — `since 2025.3`  *(line 199)*

### @staticmethod `_SapiEvent.enumerateFrom`(eventSource: _Pointer[ISpEventSource]) -> Generator['_SapiEvent', None, None]  — `since 2025.3`  *(line 216)*

  Enumerate all events in the event source.

### `_SapiEvent.getFrom`(self, eventSource: _Pointer[ISpEventSource]) -> bool  — `since 2025.3`  *(line 205)*

  Get one event from the event source and store it in this object.
  Return False if there is no event.

### `_SapiEvent.getString`(self) -> str  — `since 2025.3`  *(line 224)*

  Get the string parameter stored in lParam.

### class `_SpeakRequest`(NamedTuple)  — `since 2025.3`  *(line 91)*

### `__getattr__`(attrName: str) -> Any  — `since 2025.3`  *(line 120)*

  Module level `__getattr__` used to preserve backward compatibility.

---

## `synthDrivers/sapi5_32.py`  — `NEW MODULE since 2026.1+`

### class `SynthDriver`(SynthDriverProxy32)  — `since 2026.1+`  *(line 12)*

### @classmethod `SynthDriver.check`(cls)  — `since 2026.1+`  *(line 20)*

---

## `winVersion.py`

> A module used to record Windows versions.
> It is also used to define feature checks such as
> making sure NVDA can run on a minimum supported version of Windows.
> 
> When working on this file, consider moving to winAPI.

### class `WinVersion`(object)  — `ALL`  *(line 76)*

  Represents a Windows release.
  Includes version major, minor, build, service pack information, machine architecture,
  as well as tools such as checking for specific Windows 10 releases.

### `WinVersion.__eq__`(self, other)  — `ALL`  *(line 139)*

### `WinVersion.__ge__`(self, other)  — `ALL`  *(line 142)*

### `WinVersion.__init__`(self, major: int=0, minor: int=0, build: int=0, revision: int=0, releaseName: str | None=None, servicePack: str='', productType: str='', processorArchitecture: str='')  — `changed 2024.4, 2025.3`  *(line 83)*

  **Signature history:**
  - **2023.3:** `WinVersion.__init__(self, major: int=0, minor: int=0, build: int=0, releaseName: Optional[str]=None, servicePack: str='', productType: str='', processorArchitecture: str='')`
  - **2024.4:** `WinVersion.__init__(self, major: int=0, minor: int=0, build: int=0, releaseName: str | None=None, servicePack: str='', productType: str='', processorArchitecture: str='')`
  - **2025.3:** `WinVersion.__init__(self, major: int=0, minor: int=0, build: int=0, revision: int=0, releaseName: str | None=None, servicePack: str='', productType: str='', processorArchitecture: str='')`

### `WinVersion.__repr__`(self)  — `ALL`  *(line 128)*

### `WinVersion._getWindowsReleaseName`(self) -> str  — `ALL`  *(line 106)*

  Returns the public release name for a given Windows release based on major, minor, and build.
  This also includes feature update release name.
  This is useful if release names are not defined when constructing this class.
  On server systems, unless noted otherwise, client release names will be returned.
  For example, 'Windows 11 24H2' will be returned on Server 2025 systems.

### `__getattr__`(attrName: str) -> Any  — `since 2024.4`  *(line 226)*

  Module level `__getattr__` used to preserve backward compatibility.

### `_getRunningVersionNameFromWinReg`() -> str  — `ALL`  *(line 52)*

  Returns the Windows release name defined in Windows Registry.
  This is applicable on Windows 10 Version 1511 (build 10586) and later.

### `getWinVer`() -> WinVersion  — `changed 2026.1+`  *(line 170)*

  **Signature history:**
  - **2023.3:** `getWinVer()`
  - **2026.1+:** `getWinVer() -> WinVersion`

  Returns a record of current Windows version NVDA is running on.

### `isFullScreenMagnificationAvailable`() -> bool  — `until 2023.3`  *(line 214)*

  Technically this is always False. The Magnification API has been marked by MS as unsupported for
  WOW64 applications such as NVDA. For our usages, support has been added since Windows 8, relying on our
  testing our specific usage of the API with each Windows version since Windows 8

### `isSupportedOS`() -> bool  — `changed 2026.1+`  *(line 214)*

  **Signature history:**
  - **2023.3:** `isSupportedOS()`
  - **2026.1+:** `isSupportedOS() -> bool`

### `isUwpOcrAvailable`() -> bool  — `changed 2026.1+`  *(line 222)*

  **Signature history:**
  - **2023.3:** `isUwpOcrAvailable()`
  - **2026.1+:** `isUwpOcrAvailable() -> bool`

---

## Summary

| Metric | Count |
|--------|-------|
| Stable across all versions | 807 |
| Added in newer version | 265 |
| Removed in newer version | 54 |
| Signature changed | 54 |
| **Total entries** | **1180** |
