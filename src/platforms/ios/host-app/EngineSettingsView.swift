/*
 * EngineSettingsView.swift — Voice quality sliders + volume.
 *
 * 13 engine sliders (8 VoicingTone + 5 FrameEx), pitch mode picker,
 * inflection scale slider, and volume — all stored in AppGroup
 * UserDefaults so the AU extension picks them up for VoiceOver.
 *
 * Settings are stored per-voice (Adam, Benjamin, etc.) so each voice
 * can be tuned independently. Output settings (sample rate, pause mode,
 * volume) are global.
 */

import SwiftUI

private let kVoiceNames = ["Adam", "Benjamin", "Caleb", "David", "Robert", "Beth", "Bobby"]

private let kPitchModes = [
    ("espeak_style",   "eSpeak Style"),
    ("legacy",         "Legacy"),
    ("fujisaki_style", "Fujisaki Style"),
    ("impulse_style",  "Impulse Style"),
    ("klatt_style",    "Klatt Style"),
    ("arato_style",    "Arató (BraiLab)"),
]

private let kPauseModes = [
    (0, "Off"),
    (1, "Short"),
    (2, "Long"),
]

private let kSampleRates: [(value: Int, label: String)] = [
    (11025, "11,025 Hz"),
    (16000, "16,000 Hz"),
    (22050, "22,050 Hz"),
    (44100, "44,100 Hz"),
]

struct EngineSettingsView: View {
    @ObservedObject var engine: TgsbEngine
    @Binding var engineStarted: Bool

    // Which voice we're editing
    @State private var selectedVoice: String

    // VoicingTone sliders (0–100)
    @State private var voiceTilt: Double
    @State private var speedQuotient: Double
    @State private var aspirationTilt: Double
    @State private var cascadeBwScale: Double
    @State private var noiseGlottalMod: Double
    @State private var pitchSyncF1: Double
    @State private var pitchSyncB1: Double
    @State private var voiceTremor: Double
    @State private var headSize: Double
    @State private var chorusDepth: Double
    @State private var chorusDetuneHz: Double

    // FrameEx sliders (0–100)
    @State private var creakiness: Double
    @State private var breathiness: Double
    @State private var jitter: Double
    @State private var shimmer: Double
    @State private var glottalSharpness: Double

    // Pitch mode + inflection
    @State private var pitchMode: String
    @State private var inflectionScale: Double
    @State private var inflection: Double

    // Sample rate (slider index into kSampleRates: 0–3)
    @State private var sampleRateIndex: Double

    // Pause mode (0=off, 1=short, 2=long)
    @State private var pauseMode: Int

    // Volume
    @State private var systemVolume: Double

    // Rate override + boost (global)
    @State private var overrideRate: Bool
    @State private var globalRate: Double
    @State private var rateBoostEnabled: Bool

    // Lock language: force AU extension to use Speak tab's language
    @State private var lockLanguage: Bool

    private var defaults: UserDefaults? { UserDefaults(suiteName: kAppGroupId) }

    @State private var showResetAlert = false
    @State private var resetAll = false

    init(engine: TgsbEngine, engineStarted: Binding<Bool>) {
        _engine = ObservedObject(wrappedValue: engine)
        _engineStarted = engineStarted

        let d = UserDefaults(suiteName: kAppGroupId)

        let voice = d?.string(forKey: "adv_selectedVoice") ?? "adam"
        _selectedVoice = State(initialValue: voice)

        _voiceTilt       = State(initialValue: Self.loadV(d, "voiceTilt", 50, voice))
        _speedQuotient   = State(initialValue: Self.loadV(d, "speedQuotient", 50, voice))
        _aspirationTilt  = State(initialValue: Self.loadV(d, "aspirationTilt", 50, voice))
        _cascadeBwScale  = State(initialValue: Self.loadV(d, "cascadeBwScale", 50, voice))
        _noiseGlottalMod = State(initialValue: Self.loadV(d, "noiseGlottalMod", 0, voice))
        _pitchSyncF1     = State(initialValue: Self.loadV(d, "pitchSyncF1", 50, voice))
        _pitchSyncB1     = State(initialValue: Self.loadV(d, "pitchSyncB1", 50, voice))
        _voiceTremor     = State(initialValue: Self.loadV(d, "voiceTremor", 0, voice))
        _headSize        = State(initialValue: Self.loadV(d, "headSize", voice == "david" ? 100 : 50, voice))
        _chorusDepth     = State(initialValue: Self.loadV(d, "chorusDepth", 0, voice))
        _chorusDetuneHz  = State(initialValue: Self.loadV(d, "chorusDetuneHz", 33, voice))

        _creakiness       = State(initialValue: Self.loadV(d, "creakiness", 0, voice))
        _breathiness      = State(initialValue: Self.loadV(d, "breathiness", 0, voice))
        _jitter           = State(initialValue: Self.loadV(d, "jitter", 0, voice))
        _shimmer          = State(initialValue: Self.loadV(d, "shimmer", 0, voice))
        _glottalSharpness = State(initialValue: Self.loadV(d, "glottalSharpness", 50, voice))

        let modeKey = "adv_pitchMode.\(voice)"
        let savedMode = d?.string(forKey: modeKey)
            ?? d?.string(forKey: "adv_pitchMode")
            ?? "espeak_style"
        _pitchMode = State(initialValue: savedMode)

        _inflectionScale = State(initialValue: Self.loadV(d, "inflectionScale", 58, voice))
        _inflection = State(initialValue: Self.loadV(d, "inflection", 50, voice))

        let savedPause = d?.object(forKey: "adv_pauseMode") != nil
            ? d!.integer(forKey: "adv_pauseMode") : 1  // default: short
        _pauseMode = State(initialValue: savedPause)

        let savedRate = d?.object(forKey: "adv_sampleRate") != nil
            ? d!.integer(forKey: "adv_sampleRate") : 22050
        let idx = kSampleRates.firstIndex { $0.value == savedRate } ?? 2  // default 22050 = index 2
        _sampleRateIndex = State(initialValue: Double(idx))

        let vol = d?.object(forKey: "systemVolume") != nil
            ? d!.double(forKey: "systemVolume") : 0.8
        _systemVolume = State(initialValue: vol > 0.0 ? vol : 0.8)

        _overrideRate = State(initialValue: d?.bool(forKey: "adv_overrideRate") ?? false)
        let savedGlobalRate = d?.object(forKey: "adv_globalRate") != nil
            ? d!.double(forKey: "adv_globalRate") : 1.0
        _globalRate = State(initialValue: savedGlobalRate > 0.0 ? savedGlobalRate : 1.0)
        _rateBoostEnabled = State(initialValue: d?.bool(forKey: "rateBoost") ?? false)
        _lockLanguage = State(initialValue: d?.bool(forKey: "adv_lockLanguage") ?? false)
    }

    var body: some View {
        VStack(spacing: 0) {
        Text("Applies to VoiceOver and Speak")
            .font(.subheadline)
            .foregroundColor(.secondary)
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding(.horizontal, 20)
            .padding(.top, 10)
        Button("Reset to Defaults") { showResetAlert = true }
            .padding(.horizontal, 20)
            .padding(.vertical, 10)
            .frame(maxWidth: .infinity, alignment: .leading)
            .sheet(isPresented: $showResetAlert, onDismiss: { resetAll = false }) {
                VStack(spacing: 16) {
                    Text("Reset to Defaults")
                        .font(.headline)
                        .padding(.top, 8)

                    Toggle("Reset all voices and global settings", isOn: $resetAll)

                    Text(resetAll
                        ? "This will reset engine settings for all voices to their default values."
                        : "This will reset engine settings for \(selectedVoice.capitalized) to their default values.")
                        .font(.callout)
                        .foregroundColor(.secondary)
                        .frame(maxWidth: .infinity, alignment: .leading)

                    HStack(spacing: 16) {
                        Button("Cancel") {
                            showResetAlert = false
                        }
                        .frame(maxWidth: .infinity)

                        Button("Reset", role: .destructive) {
                            resetToDefaults()
                            showResetAlert = false
                        }
                        .frame(maxWidth: .infinity)
                    }
                    .padding(.top, 8)
                }
                .padding(20)
                .presentationDetents([.height(230)])
            }

        ScrollView {
            VStack(alignment: .leading, spacing: 24) {

                // ── Voice Picker ───────────────────────────
                Text("Voice")
                    .font(.headline)
                    .accessibilityAddTraits(.isHeader)

                Picker("Editing voice", selection: $selectedVoice) {
                    ForEach(kVoiceNames, id: \.self) { name in
                        Text(name).tag(name.lowercased())
                    }
                }
                .pickerStyle(.segmented)
                .accessibilityLabel("Editing voice")
                .accessibilityValue(selectedVoice.capitalized)
                .onChange(of: selectedVoice) { voice in
                    defaults?.set(voice, forKey: "adv_selectedVoice")
                    loadSettingsForVoice(voice)
                }

                // ── Pitch ───────────────────────────────────
                Text("Pitch")
                    .font(.headline)
                    .accessibilityAddTraits(.isHeader)

                Picker("Pitch Mode", selection: $pitchMode) {
                    ForEach(kPitchModes, id: \.0) { mode in
                        Text(mode.1).tag(mode.0)
                    }
                }
                .accessibilityLabel("Pitch mode")
                .accessibilityValue(pitchModeDisplayName)
                .onChange(of: pitchMode) { val in
                    defaults?.set(val, forKey: "adv_pitchMode.\(selectedVoice)")
                    engine.setPitchMode(val)
                    bumpVersion()
                }

                toneSlider("Inflection Scale", $inflectionScale, "inflectionScale")

                Divider()

                // ── Voice Tone ──────────────────────────────
                Text("Voice Tone")
                    .font(.headline)
                    .accessibilityAddTraits(.isHeader)

                toneSlider("Voice Tilt", $voiceTilt, "voiceTilt")
                toneSlider("Speed Quotient", $speedQuotient, "speedQuotient")
                toneSlider("Aspiration Tilt", $aspirationTilt, "aspirationTilt")
                toneSlider("Formant Sharpness", $cascadeBwScale, "cascadeBwScale")
                toneSlider("Noise Modulation", $noiseGlottalMod, "noiseGlottalMod")
                toneSlider("Pitch-Sync F1", $pitchSyncF1, "pitchSyncF1")
                toneSlider("Pitch-Sync B1", $pitchSyncB1, "pitchSyncB1")
                toneSlider("Voice Tremor", $voiceTremor, "voiceTremor")
                toneSlider("Head Size", $headSize, "headSize")
                toneSlider("Chorus Depth", $chorusDepth, "chorusDepth")
                toneSlider("Chorus Variation (needs depth)", $chorusDetuneHz, "chorusDetuneHz")

                Divider()

                // ── Voice Character ─────────────────────────
                Text("Voice Character")
                    .font(.headline)
                    .accessibilityAddTraits(.isHeader)

                toneSlider("Inflection", $inflection, "inflection")
                toneSlider("Creakiness", $creakiness, "creakiness")
                toneSlider("Breathiness", $breathiness, "breathiness")
                toneSlider("Jitter", $jitter, "jitter")
                toneSlider("Shimmer", $shimmer, "shimmer")
                toneSlider("Glottal Sharpness", $glottalSharpness, "glottalSharpness")

                Divider()

                // ── Output ──────────────────────────────────
                Text("Output")
                    .font(.headline)
                    .accessibilityAddTraits(.isHeader)

                Toggle("Override speech rate", isOn: $overrideRate)
                    .onChange(of: overrideRate) { val in
                        defaults?.set(val, forKey: "adv_overrideRate")
                        bumpVersion()
                    }

                if overrideRate {
                    HStack {
                        Text("Rate: \(globalRate, specifier: "%.1f")x")
                            .frame(width: 120, alignment: .leading)
                            .accessibilityHidden(true)
                        Slider(value: $globalRate, in: 0.3...4.0, step: 0.1)
                            .onChange(of: globalRate) { val in
                                defaults?.set(val, forKey: "adv_globalRate")
                                bumpVersion()
                            }
                    }
                    .accessibilityElement(children: .combine)
                    .accessibilityLabel("Override rate")
                    .accessibilityValue("\(globalRate, specifier: "%.1f") times")
                }

                Toggle("Rate boost", isOn: $rateBoostEnabled)
                    .onChange(of: rateBoostEnabled) { val in
                        defaults?.set(val, forKey: "rateBoost")
                        engine.rateBoost = val
                        bumpVersion()
                    }

                HStack {
                    Text("Sample Rate: \(kSampleRates[Int(sampleRateIndex)].label)")
                        .frame(width: 220, alignment: .leading)
                    Slider(value: $sampleRateIndex, in: 0...Double(kSampleRates.count - 1), step: 1)
                        .onChange(of: sampleRateIndex) { val in
                            let rate = kSampleRates[Int(val)].value
                            defaults?.set(rate, forKey: "adv_sampleRate")
                            engine.changeSampleRate(rate)
                            bumpVersion()
                        }
                }
                .accessibilityElement(children: .combine)
                .accessibilityLabel("Sample rate")
                .accessibilityValue(kSampleRates[Int(sampleRateIndex)].label)

                Picker("Pause Mode", selection: $pauseMode) {
                    ForEach(kPauseModes, id: \.0) { mode in
                        Text(mode.1).tag(mode.0)
                    }
                }
                .accessibilityLabel("Pause mode")
                .accessibilityValue(kPauseModes.first { $0.0 == pauseMode }?.1 ?? "Short")
                .onChange(of: pauseMode) { val in
                    defaults?.set(val, forKey: "adv_pauseMode")
                    engine.setPauseMode(val)
                    bumpVersion()
                }

                HStack {
                    Text("Volume: \(Int(systemVolume * 100))%")
                        .frame(width: 180, alignment: .leading)
                    Slider(value: $systemVolume, in: 0.1...1.0, step: 0.05)
                        .onChange(of: systemVolume) { val in
                            defaults?.set(val, forKey: "systemVolume")
                            defaults?.synchronize()
                        }
                }
                .accessibilityElement(children: .combine)
                .accessibilityLabel("System voice volume")
                .accessibilityValue("\(Int(systemVolume * 100)) percent")

                Toggle("Lock language", isOn: $lockLanguage)
                    .onChange(of: lockLanguage) { val in
                        defaults?.set(val, forKey: "adv_lockLanguage")
                        bumpVersion()
                    }
                    .accessibilityLabel(lockLanguage
                        ? "Lock language, \(engine.selectedLanguage.displayName), change in Speak tab"
                        : "Lock language")

                if lockLanguage {
                    Text(engine.selectedLanguage.displayName)
                        .font(.footnote)
                        .foregroundColor(.secondary)
                        .accessibilityHidden(true)
                }
            }
            .padding(20)
        }
        .accessibilityLabel("Engine settings")
        } // VStack
    }

    // MARK: - Load settings for voice switch

    private func loadSettingsForVoice(_ voice: String) {
        let d = defaults

        voiceTilt       = Self.loadV(d, "voiceTilt", 50, voice)
        speedQuotient   = Self.loadV(d, "speedQuotient", 50, voice)
        aspirationTilt  = Self.loadV(d, "aspirationTilt", 50, voice)
        cascadeBwScale  = Self.loadV(d, "cascadeBwScale", 50, voice)
        noiseGlottalMod = Self.loadV(d, "noiseGlottalMod", 0, voice)
        pitchSyncF1     = Self.loadV(d, "pitchSyncF1", 50, voice)
        pitchSyncB1     = Self.loadV(d, "pitchSyncB1", 50, voice)
        voiceTremor     = Self.loadV(d, "voiceTremor", 0, voice)
        headSize        = Self.loadV(d, "headSize", voice == "david" ? 100 : 50, voice)
        chorusDepth     = Self.loadV(d, "chorusDepth", 0, voice)
        chorusDetuneHz  = Self.loadV(d, "chorusDetuneHz", 33, voice)

        creakiness       = Self.loadV(d, "creakiness", 0, voice)
        breathiness      = Self.loadV(d, "breathiness", 0, voice)
        jitter           = Self.loadV(d, "jitter", 0, voice)
        shimmer          = Self.loadV(d, "shimmer", 0, voice)
        glottalSharpness = Self.loadV(d, "glottalSharpness", 50, voice)

        pitchMode = d?.string(forKey: "adv_pitchMode.\(voice)")
            ?? d?.string(forKey: "adv_pitchMode")
            ?? "espeak_style"

        inflectionScale = Self.loadV(d, "inflectionScale", 58, voice)
        inflection      = Self.loadV(d, "inflection", 50, voice)
    }

    // MARK: - Reset

    private func resetToDefaults() {
        voiceTilt = 50;       speedQuotient = 50
        aspirationTilt = 50;  cascadeBwScale = 50
        noiseGlottalMod = 0;  pitchSyncF1 = 50
        pitchSyncB1 = 50;     voiceTremor = 0
        headSize = 50
        chorusDepth = 0;      chorusDetuneHz = 33

        creakiness = 0;       breathiness = 0
        jitter = 0;           shimmer = 0
        glottalSharpness = 50

        pitchMode = "espeak_style"
        inflectionScale = 58; inflection = 50

        pauseMode = 1         // short
        sampleRateIndex = 2   // 22050 Hz
        systemVolume = 0.8
        overrideRate = false
        globalRate = 1.0
        rateBoostEnabled = false
        lockLanguage = false

        // Persist per-voice defaults
        let d = defaults
        let voices = resetAll
            ? kVoiceNames.map { $0.lowercased() }
            : [selectedVoice]

        for v in voices {
            d?.set(50.0, forKey: "adv_voiceTilt.\(v)")
            d?.set(50.0, forKey: "adv_speedQuotient.\(v)")
            d?.set(50.0, forKey: "adv_aspirationTilt.\(v)")
            d?.set(50.0, forKey: "adv_cascadeBwScale.\(v)")
            d?.set(0.0,  forKey: "adv_noiseGlottalMod.\(v)")
            d?.set(50.0, forKey: "adv_pitchSyncF1.\(v)")
            d?.set(50.0, forKey: "adv_pitchSyncB1.\(v)")
            d?.set(0.0,  forKey: "adv_voiceTremor.\(v)")
            d?.set(50.0, forKey: "adv_headSize.\(v)")
            d?.set(0.0,  forKey: "adv_chorusDepth.\(v)")
            d?.set(33.0, forKey: "adv_chorusDetuneHz.\(v)")
            d?.set(0.0,  forKey: "adv_creakiness.\(v)")
            d?.set(0.0,  forKey: "adv_breathiness.\(v)")
            d?.set(0.0,  forKey: "adv_jitter.\(v)")
            d?.set(0.0,  forKey: "adv_shimmer.\(v)")
            d?.set(50.0, forKey: "adv_glottalSharpness.\(v)")
            d?.set("espeak_style", forKey: "adv_pitchMode.\(v)")
            d?.set(58.0, forKey: "adv_inflectionScale.\(v)")
            d?.set(50.0, forKey: "adv_inflection.\(v)")
        }

        // Global output settings
        d?.set(pauseMode,       forKey: "adv_pauseMode")
        d?.set(22050,           forKey: "adv_sampleRate")
        d?.set(systemVolume,    forKey: "systemVolume")
        d?.set(false,           forKey: "adv_overrideRate")
        d?.set(1.0,             forKey: "adv_globalRate")
        d?.set(false,           forKey: "rateBoost")
        d?.set(false,           forKey: "adv_lockLanguage")

        // Apply to engine
        engine.setPitchMode(pitchMode)
        engine.setPauseMode(pauseMode)
        engine.changeSampleRate(22050)
        applyAllSettings()
    }

    private var pitchModeDisplayName: String {
        kPitchModes.first { $0.0 == pitchMode }?.1 ?? pitchMode
    }

    // MARK: - Slider helper

    @ViewBuilder
    private func toneSlider(
        _ label: String,
        _ value: Binding<Double>,
        _ key: String
    ) -> some View {
        HStack {
            Text("\(label): \(Int(value.wrappedValue))")
                .frame(width: 180, alignment: .leading)
            Slider(value: value, in: 0...100, step: 1)
                .onChange(of: value.wrappedValue) { val in
                    defaults?.set(val, forKey: "adv_\(key).\(selectedVoice)")
                    applyAllSettings()
                }
        }
        .accessibilityElement(children: .combine)
        .accessibilityLabel(label)
        .accessibilityValue("\(Int(value.wrappedValue))")
    }

    // MARK: - Apply

    private func applyAllSettings() {
        engine.applyVoicingToneFromSliders(
            voiceTilt: voiceTilt,
            speedQuotient: speedQuotient,
            aspirationTilt: aspirationTilt,
            cascadeBwScale: cascadeBwScale,
            noiseGlottalMod: noiseGlottalMod,
            pitchSyncF1: pitchSyncF1,
            pitchSyncB1: pitchSyncB1,
            voiceTremor: voiceTremor,
            headSize: headSize,
            chorusDepth: chorusDepth,
            chorusDetuneHz: chorusDetuneHz)

        engine.applyFrameExFromSliders(
            creakiness: creakiness,
            breathiness: breathiness,
            jitter: jitter,
            shimmer: shimmer,
            glottalSharpness: glottalSharpness)

        engine.setInflectionScale(inflectionScale / 100.0)
        engine.setInflection(inflection / 100.0)

        bumpVersion()
    }

    private func bumpVersion() {
        let d = defaults
        let ver = (d?.integer(forKey: "adv_settingsVersion") ?? 0) + 1
        d?.set(ver, forKey: "adv_settingsVersion")
        // Flush to disk so the AU extension (separate process) sees the
        // updated values immediately via its own synchronize() call.
        d?.synchronize()
    }

    // MARK: - UserDefaults loader (per-voice with global fallback)

    /// Load a per-voice setting. Falls back to the old global key for
    /// migration, then to the provided default.
    private static func loadV(
        _ d: UserDefaults?, _ key: String, _ dflt: Double, _ voice: String
    ) -> Double {
        guard let d = d else { return dflt }
        let voiceKey = "adv_\(key).\(voice)"
        if d.object(forKey: voiceKey) != nil {
            return d.double(forKey: voiceKey)
        }
        // Fallback: old global key (pre-per-voice migration)
        let globalKey = "adv_\(key)"
        if d.object(forKey: globalKey) != nil {
            return d.double(forKey: globalKey)
        }
        return dflt
    }
}
