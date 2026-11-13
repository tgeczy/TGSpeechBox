// Drives the real TGSpeechSapi.dll the way a SAPI host does: ISpTTSEngine::Speak
// with a host site that records the audio and can abort part way through, as
// NVDA and Narrator do when the user moves on before an item has finished.
//
// The DLL is loaded from a staging folder laid out like an install
// (x64/TGSpeechSapi.dll next to ../packs and ../espeak-ng-data), through
// DllGetClassObject, so no registration is needed and the build under test is
// the one in this tree.  The voice token is a throwaway key under HKCU.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <windows.h>
#include <sapi.h>
#include <sapiddk.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#ifndef TGSB_SAPI_STAGE_DLL
#error "TGSB_SAPI_STAGE_DLL must name the staged TGSpeechSapi.dll"
#endif

namespace {

// {70E56986-4B3C-4CE1-B1F1-C861EE906FFD}, ISpTTSEngineImpl in src/sapi.
const CLSID kEngineClsid = {0x70e56986, 0x4b3c, 0x4ce1, {0xb1, 0xf1, 0xc8, 0x61, 0xee, 0x90, 0x6f, 0xfd}};

const wchar_t kTokenKey[] = L"Software\\TGSpeechBoxTests\\SapiEngineVoice";

// The engine reads %APPDATA%\TGSpeechSapi\settings.ini and logs to the temp
// folder.  Both are pointed into the staging folder before the DLL is loaded,
// so the tests neither read the settings of whoever runs them nor leave
// anything behind, and a test can write the settings.ini it needs.
std::wstring stageDir() {
    std::wstring dll = TGSB_SAPI_STAGE_DLL;   // <stage>/x64/TGSpeechSapi.dll
    for (wchar_t& c : dll) if (c == L'/') c = L'\\';
    dll.resize(dll.find_last_of(L'\\'));      // <stage>/x64
    dll.resize(dll.find_last_of(L'\\'));      // <stage>
    return dll;
}
std::wstring settingsPath() { return stageDir() + L"\\appdata\\TGSpeechSapi\\settings.ini"; }
std::wstring tempDir() { return stageDir() + L"\\temp"; }

// Replace the settings.ini with these "[Section]\nkey=value" lines; an empty
// text removes the file (a fresh install has none).
void writeSettings(const std::string& ini) {
    const std::wstring path = settingsPath();
    DeleteFileW(path.c_str());
    if (ini.empty()) return;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    REQUIRE(h != INVALID_HANDLE_VALUE);
    DWORD written = 0;
    WriteFile(h, ini.data(), static_cast<DWORD>(ini.size()), &written, nullptr);
    CloseHandle(h);
}

const struct PrivateProfile {
    PrivateProfile() {
        const std::wstring appdata = stageDir() + L"\\appdata";
        CreateDirectoryW(appdata.c_str(), nullptr);
        CreateDirectoryW((appdata + L"\\TGSpeechSapi").c_str(), nullptr);
        CreateDirectoryW(tempDir().c_str(), nullptr);
        SetEnvironmentVariableW(L"APPDATA", appdata.c_str());
        SetEnvironmentVariableW(L"TMP", tempDir().c_str());
        SetEnvironmentVariableW(L"TEMP", tempDir().c_str());
        DeleteFileW(settingsPath().c_str());
    }
} kPrivateProfile;

// A SAPI host, as the engine sees it.
class HostSite : public ISpTTSEngineSite {
public:
    std::vector<int16_t> audio;
    // Answer SPVES_ABORT once this much audio has been written (bytes).
    size_t abortAfterBytes = SIZE_MAX;
    long rate = 0;
    USHORT volume = 100;
    size_t bytes = 0;
    // The bookmarks the engine reported: the mark and its byte offset.
    struct Mark { std::wstring name; ULONGLONG offset; };
    std::vector<Mark> marks;
    // Ask to skip skipCount sentences (ISpVoice::Skip) once this much audio
    // has been written (bytes).  As in SAPI, SPVES_SKIP stays set until the
    // engine answers with CompleteSkip.
    size_t skipAfterBytes = SIZE_MAX;
    long skipCount = 0;
    bool skipRaised = false;
    bool skipPending = false;
    std::vector<long> skipsCompleted;   // what the engine reported
    size_t bytesAtSkip = 0;             // audio written when it reported
    // When the first audio arrived (QueryPerformanceCounter ticks, 0 = none).
    LONGLONG firstWriteTicks = 0;

    // IUnknown (stack object: reference counting is a formality)
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == __uuidof(ISpEventSink) || riid == __uuidof(ISpTTSEngineSite)) {
            *ppv = static_cast<ISpTTSEngineSite*>(this);
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return 2; }
    STDMETHODIMP_(ULONG) Release() override { return 1; }

    // ISpEventSink
    STDMETHODIMP AddEvents(const SPEVENT* events, ULONG count) override {
        for (ULONG i = 0; events && i < count; ++i) {
            if (events[i].eEventId == SPEI_TTS_BOOKMARK && events[i].lParam)
                marks.push_back({reinterpret_cast<const wchar_t*>(events[i].lParam), events[i].ullAudioStreamOffset});
        }
        return S_OK;
    }
    STDMETHODIMP GetEventInterest(ULONGLONG* p) override {
        if (p) *p = SPFEI_ALL_TTS_EVENTS;
        return S_OK;
    }

    // ISpTTSEngineSite
    STDMETHODIMP_(DWORD) GetActions() override {
        if (bytes >= abortAfterBytes) return SPVES_ABORT;
        if (!skipRaised && bytes >= skipAfterBytes) skipRaised = skipPending = true;
        return skipPending ? SPVES_SKIP : SPVES_CONTINUE;
    }
    STDMETHODIMP Write(const void* buf, ULONG cb, ULONG* written) override {
        if (bytes >= abortAfterBytes) {  // a stopped host takes nothing more
            if (written) *written = 0;
            return S_OK;
        }
        const auto* s = static_cast<const int16_t*>(buf);
        if (!firstWriteTicks && cb) {
            LARGE_INTEGER now;
            QueryPerformanceCounter(&now);
            firstWriteTicks = now.QuadPart;
        }
        audio.insert(audio.end(), s, s + cb / sizeof(int16_t));
        bytes += cb;
        if (written) *written = cb;
        return S_OK;
    }
    STDMETHODIMP GetRate(long* p) override { if (p) *p = rate; return S_OK; }
    STDMETHODIMP GetVolume(USHORT* p) override { if (p) *p = volume; return S_OK; }
    STDMETHODIMP GetSkipInfo(SPVSKIPTYPE* type, long* count) override {
        if (type) *type = SPVST_SENTENCE;
        if (count) *count = skipCount;
        return S_OK;
    }
    STDMETHODIMP CompleteSkip(long skipped) override {
        skipPending = false;
        skipsCompleted.push_back(skipped);
        bytesAtSkip = bytes;
        return S_OK;
    }
};

struct Engine {
    HMODULE dll = nullptr;
    ISpTTSEngine* tts = nullptr;
    ISpObjectWithToken* withToken = nullptr;
    WAVEFORMATEX fmt{};

    explicit Engine(const wchar_t* langTag, const wchar_t* preset = L"Adam") {
        // The token: Attributes\TGSpeech_LangTag + TGSpeech_Preset.
        HKEY attrs = nullptr;
        const std::wstring attrsKey = std::wstring(kTokenKey) + L"\\Attributes";
        REQUIRE(RegCreateKeyExW(HKEY_CURRENT_USER, attrsKey.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &attrs, nullptr) == ERROR_SUCCESS);
        auto setStr = [&](const wchar_t* name, const wchar_t* value) {
            RegSetValueExW(attrs, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value),
                           static_cast<DWORD>((wcslen(value) + 1) * sizeof(wchar_t)));
        };
        setStr(L"TGSpeech_LangTag", langTag);
        setStr(L"TGSpeech_Preset", preset);
        RegCloseKey(attrs);

        ISpObjectToken* token = nullptr;
        REQUIRE(SUCCEEDED(CoCreateInstance(CLSID_SpObjectToken, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&token))));
        const std::wstring id = std::wstring(L"HKEY_CURRENT_USER\\") + kTokenKey;
        REQUIRE(SUCCEEDED(token->SetId(nullptr, id.c_str(), FALSE)));

        dll = LoadLibraryW(TGSB_SAPI_STAGE_DLL);
        REQUIRE_MESSAGE(dll != nullptr, "could not load the staged TGSpeechSapi.dll");
        using GetClassObjectFn = HRESULT(STDAPICALLTYPE*)(REFCLSID, REFIID, void**);
        auto getClassObject = reinterpret_cast<GetClassObjectFn>(GetProcAddress(dll, "DllGetClassObject"));
        REQUIRE(getClassObject != nullptr);
        IClassFactory* factory = nullptr;
        REQUIRE(SUCCEEDED(getClassObject(kEngineClsid, IID_IClassFactory, reinterpret_cast<void**>(&factory))));
        REQUIRE(SUCCEEDED(factory->CreateInstance(nullptr, __uuidof(ISpTTSEngine), reinterpret_cast<void**>(&tts))));
        factory->Release();
        REQUIRE(SUCCEEDED(tts->QueryInterface(IID_PPV_ARGS(&withToken))));
        REQUIRE(SUCCEEDED(withToken->SetObjectToken(token)));
        token->Release();

        GUID fmtId{};
        WAVEFORMATEX* wfx = nullptr;
        REQUIRE(SUCCEEDED(tts->GetOutputFormat(&SPDFID_WaveFormatEx, nullptr, &fmtId, &wfx)));
        fmt = *wfx;
        CoTaskMemFree(wfx);
    }

    ~Engine() {
        if (withToken) withToken->Release();
        if (tts) tts->Release();
        // The DLL stays loaded: it caches its runtime in a static, as it
        // does inside a real host process.
    }

    void speak(const std::wstring& text, HostSite& site) {
        SPVTEXTFRAG frag{};
        frag.State.eAction = SPVA_Speak;
        frag.State.Volume = 100;
        frag.pTextStart = text.c_str();
        frag.ulTextLen = static_cast<ULONG>(text.size());
        frag.ulTextSrcOffset = 0;
        REQUIRE(SUCCEEDED(tts->Speak(0, SPDFID_WaveFormatEx, &fmt, &frag, &site)));
    }

    // One Speak() call with a fragment list, as SAPI builds it from XML
    // markup: text, bookmarks (the text is the mark), spelled text, and the
    // rate and volume a fragment carries from <rate> and <volume>.
    struct Frag {
        SPVACTIONS action;
        std::wstring text;
        long rateAdj = 0;
        ULONG volume = 100;
    };
    void speak(const std::vector<Frag>& frags, HostSite& site) {
        std::vector<SPVTEXTFRAG> list(frags.size());
        ULONG offset = 0;
        for (size_t i = 0; i < frags.size(); ++i) {
            list[i].pNext = i + 1 < frags.size() ? &list[i + 1] : nullptr;
            list[i].State.eAction = frags[i].action;
            list[i].State.RateAdj = frags[i].rateAdj;
            list[i].State.Volume = frags[i].volume;
            list[i].pTextStart = frags[i].text.c_str();
            list[i].ulTextLen = static_cast<ULONG>(frags[i].text.size());
            list[i].ulTextSrcOffset = offset;
            offset += list[i].ulTextLen + 1;
        }
        REQUIRE(SUCCEEDED(tts->Speak(0, SPDFID_WaveFormatEx, &fmt, list.data(), &site)));
    }

    size_t msToBytes(double ms) const {
        return static_cast<size_t>(ms * fmt.nSamplesPerSec / 1000.0) * sizeof(int16_t);
    }
};

// The pitch of `a` every 10 ms, in Hz, 0 where it is not voiced: the shortest
// period with a strong autocorrelation over 40 ms, between 60 and 400 Hz.
std::vector<double> pitchTrack(const std::vector<int16_t>& a, int sampleRate) {
    std::vector<double> f0;
    const int win = sampleRate * 40 / 1000, hop = sampleRate / 100;
    const int minLag = sampleRate / 400, maxLag = sampleRate / 60;
    std::vector<double> r(maxLag + 1);
    for (size_t start = 0; start + win + maxLag < a.size(); start += hop) {
        const int16_t* x = a.data() + start;
        double energy = 0.0;
        for (int i = 0; i < win; ++i) energy += static_cast<double>(x[i]) * x[i];
        double best = 0.0;
        if (energy / win > 150.0 * 150.0) {
            for (int lag = minLag; lag <= maxLag; ++lag) {
                double num = 0.0, den = 0.0;
                for (int i = 0; i < win; ++i) {
                    num += static_cast<double>(x[i]) * x[i + lag];
                    den += static_cast<double>(x[i + lag]) * x[i + lag];
                }
                r[lag] = num / std::sqrt(energy * den + 1.0);
                best = std::max(best, r[lag]);
            }
        }
        double hz = 0.0;
        if (best > 0.7) {
            for (int lag = minLag + 1; lag < maxLag; ++lag) {
                if (r[lag] >= 0.92 * best && r[lag] >= r[lag - 1] && r[lag] >= r[lag + 1]) {
                    hz = static_cast<double>(sampleRate) / lag;
                    break;
                }
            }
        }
        f0.push_back(hz);
    }
    return f0;
}

// How far two renders of one text are apart in pitch: the upper quartile of
// the difference in Hz over the steps voiced in both (not the mean, so that
// a step or two where the tracker lands an octave off does not count).
// Renders with the same settings are within a fraction of a hertz of each
// other.
double pitchDistance(const std::vector<double>& a, const std::vector<double>& b) {
    std::vector<double> diff;
    for (size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
        if (a[i] > 0.0 && b[i] > 0.0) diff.push_back(std::fabs(a[i] - b[i]));
    }
    REQUIRE(diff.size() >= 50);
    std::sort(diff.begin(), diff.end());
    return diff[diff.size() * 3 / 4];
}

// How bright `a` is: the energy of its sample-to-sample differences over its
// energy.  The voice source's spectral tilt moves it a long way.
double brightness(const std::vector<int16_t>& a) {
    double diff = 0.0, total = 1.0;
    for (size_t i = 1; i < a.size(); ++i) {
        const double d = static_cast<double>(a[i]) - a[i - 1];
        diff += d * d;
        total += static_cast<double>(a[i]) * a[i];
    }
    return diff / total;
}

// First sample whose magnitude reaches `level`, or size() when none does.
size_t onset(const std::vector<int16_t>& a, int level) {
    for (size_t i = 0; i < a.size(); ++i)
        if (std::abs(static_cast<int>(a[i])) >= level) return i;
    return a.size();
}

int peakIn(const std::vector<int16_t>& a, size_t from, size_t to) {
    int peak = 0;
    for (size_t i = from; i < std::min(to, a.size()); ++i)
        peak = std::max(peak, std::abs(static_cast<int>(a[i])));
    return peak;
}

struct ComScope {
    ComScope() { CoInitializeEx(nullptr, COINIT_MULTITHREADED); }
    ~ComScope() {
        RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\TGSpeechBoxTests");
        CoUninitialize();
    }
};

const std::wstring kLong =
    L"Recycle Bin, this is a long description that keeps going so that the screen "
    L"reader can cut it off part way through, the way tabbing past an item does";

}  // namespace

// #128 (29-Bloo, Edu): "the longer the text, the longer the synthesizer takes
// to respond".  The engine renders and hands audio over as it goes, so the
// first audio of a long post arrives as soon as a short item's does.
TEST_CASE("a long text starts speaking as soon as a short one (#128)") {
    ComScope com;
    Engine engine(L"en-us");
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    auto firstAudioMs = [&](const std::wstring& text) {
        std::vector<double> ms;
        for (int i = 0; i < 5; ++i) {
            HostSite site;
            LARGE_INTEGER t0;
            QueryPerformanceCounter(&t0);
            engine.speak(text, site);
            REQUIRE(site.firstWriteTicks != 0);
            ms.push_back((site.firstWriteTicks - t0.QuadPart) * 1000.0 / freq.QuadPart);
        }
        std::sort(ms.begin(), ms.end());
        return ms[ms.size() / 2];
    };
    std::wstring post;
    for (int i = 0; i < 12; ++i)
        post += L"This is one of the long posts people read with a screen reader every day, and it goes on "
                L"without much punctuation so that a whole clause is long enough to matter ";
    const double shortMs = firstAudioMs(L"Documents");
    const double longMs = firstAudioMs(post);
    MESSAGE("first audio: short item " << shortMs << " ms, " << post.size() << "-character post " << longMs << " ms");
    CHECK(longMs <= shortMs + 15.0);
}

// Each Speak() call is an utterance of its own (#127): the frontend used to
// carry its stream state from one call into the next, so an item that starts
// with a vowel began 20 ms later after a word ending in a consonant than
// after one ending in a vowel.
TEST_CASE("an item starts the same whatever was spoken before it") {
    ComScope com;
    Engine engine(L"en-us");
    const int level = 400;
    auto onsetAfter = [&](const wchar_t* before) {
        HostSite prev;
        engine.speak(before, prev);
        HostSite item;
        engine.speak(L"Applications", item);
        return onset(item.audio, level);
    };
    const size_t afterVowel = onsetAfter(L"idea");
    const size_t afterConsonant = onsetAfter(L"desktop");
    const size_t afterNothing = onsetAfter(L"Applications");
    const double rate = engine.fmt.nSamplesPerSec / 1000.0;
    MESSAGE("onset after idea " << afterVowel / rate << " ms, after desktop " << afterConsonant / rate
                                << " ms, after itself " << afterNothing / rate << " ms");
    CHECK(std::fabs(static_cast<double>(afterConsonant) - static_cast<double>(afterVowel)) / rate <= 1.0);
    CHECK(std::fabs(static_cast<double>(afterNothing) - static_cast<double>(afterVowel)) / rate <= 1.0);
}

// A dash between words, parentheses around words, and Spanish ¿ and ¡ pause
// like a comma (#133): the same clauses and the same pause as the text
// written with commas.  Before the shared splitter SAPI did not split there
// at all, and eSpeak's own break at the mark ran the words either side
// together ("wait — what" as "wˈe͡ɪtwˌʌt").
TEST_CASE("a dash, parentheses, ¿ or ¡ pause like a comma (#133)") {
    ComScope com;
    struct Case { const wchar_t* lang; const wchar_t* marked; const wchar_t* commas; const wchar_t* plain; };
    const Case cases[] = {
        {L"en-us", L"wait — what now", L"wait, what now", L"wait what now"},
        {L"en-us", L"wait – what now", L"wait, what now", L"wait what now"},
        {L"en-us", L"The file (about two megabytes) is ready", L"The file, about two megabytes, is ready",
         L"The file about two megabytes is ready"},
        {L"es", L"hola ¿qué tal", L"hola, qué tal", L"hola qué tal"},
        {L"es", L"dijo ¡basta", L"dijo, basta", L"dijo basta"},
    };
    for (const Case& c : cases) {
        Engine engine(c.lang);
        HostSite marked, commas, plain;
        engine.speak(c.marked, marked);
        engine.speak(c.commas, commas);
        engine.speak(c.plain, plain);
        REQUIRE(plain.audio.size() > 0);
        auto ms = [&](const HostSite& a, const HostSite& b) {
            return (static_cast<double>(a.audio.size()) - static_cast<double>(b.audio.size())) * 1000.0 /
                   engine.fmt.nSamplesPerSec;
        };
        MESSAGE("marked " << marked.audio.size() << " commas " << commas.audio.size() << " plain "
                          << plain.audio.size() << " samples");
        CHECK_MESSAGE(std::fabs(ms(marked, commas)) <= 5.0, "the mark does not sound like a comma: "
                                                                << ms(marked, commas) << " ms apart");
        CHECK_MESSAGE(ms(marked, plain) >= 20.0, "no pause at the mark: only " << ms(marked, plain)
                                                                              << " ms longer than without it");
    }
}

// #135 (Edu): tabbing fast under NVDA or Narrator, "a small residue of the
// previous word or phrase at the start of the next one", easier to hear at
// slow rates.  A host abort must leave nothing of the old utterance behind:
// the next item starts exactly as it does on an engine that was never
// interrupted.
TEST_CASE("an aborted utterance leaves nothing behind for the next one (#135)") {
    ComScope com;
    for (long rate : {-5L, 0L, 5L}) {
        CAPTURE(rate);
        Engine engine(L"en-us");

        // The reference is an item spoken after another one that finished
        // normally, which is what "never interrupted" means to a user.
        HostSite warm;
        warm.rate = rate;
        engine.speak(L"Desktop", warm);
        HostSite reference;
        reference.rate = rate;
        engine.speak(L"Documents", reference);
        REQUIRE(reference.audio.size() > 0);
        const int level = 400;  // about -38 dBFS
        const size_t refOnset = onset(reference.audio, level);
        REQUIRE(refOnset < reference.audio.size());

        for (double cutMs : {60.0, 150.0, 400.0}) {
            CAPTURE(cutMs);
            HostSite cut;
            cut.rate = rate;
            cut.abortAfterBytes = engine.msToBytes(cutMs);
            engine.speak(kLong, cut);
            REQUIRE(cut.bytes >= cut.abortAfterBytes);  // it really was cut mid-speech

            HostSite next;
            next.rate = rate;
            engine.speak(L"Documents", next);
            const size_t nextOnset = onset(next.audio, level);
            // Before "Documents" begins there is only what an uninterrupted
            // engine has there: its own lead-in, no tail of the long item.
            const size_t twoMs = engine.msToBytes(2.0) / sizeof(int16_t);
            const size_t leadIn = refOnset > twoMs ? refOnset - twoMs : 0;
            MESSAGE("onset ref " << refOnset << " next " << nextOnset << " samples; cut wrote " << cut.bytes / 2);
            CHECK_MESSAGE(peakIn(next.audio, 0, leadIn) < level,
                          "audio from the aborted utterance leaks into the next one");
            // And it starts when an uninterrupted one starts (within 2 ms).
            const double onsetShiftMs =
                (static_cast<double>(nextOnset) - static_cast<double>(refOnset)) * 1000.0 / engine.fmt.nSamplesPerSec;
            CHECK_MESSAGE(std::fabs(onsetShiftMs) <= 2.0,
                          "the next utterance starts " << onsetShiftMs << " ms off where it should");
        }
    }
}

// #202: the host process crashed after two engine instances had been released.
// eSpeak NG is one per process, but every runtime initialized it and every
// runtime's destructor terminated it.  With two engines alive, the first one
// released goes into the runtime cache and the second is destroyed, taking
// eSpeak away from under the cached one, which still believed it was up: the
// next engine picked that runtime up and crashed on its first word.  A host
// does this whenever it holds two TGSpeechBox voices at once (a voice list
// with a preview next to the speaking voice) and lets both go.
TEST_CASE("an engine speaks after two others were released (#202)") {
    ComScope com;
    {
        Engine first(L"en-us");
        Engine second(L"en-gb");
        HostSite a, b;
        first.speak(L"Desktop", a);
        second.speak(L"Documents", b);
        REQUIRE(a.audio.size() > 0);
        REQUIRE(b.audio.size() > 0);
    }   // both released: one runtime is cached, the other destroyed
    for (int round = 0; round < 3; ++round) {
        CAPTURE(round);
        Engine third(L"en-us");
        HostSite c;
        third.speak(L"Recycle Bin", c);
        CHECK(c.audio.size() > 0);
        CHECK(peakIn(c.audio, 0, c.audio.size()) > 1000);
    }
}

// One engine released while another is still speaking must not take eSpeak
// from it either.
TEST_CASE("releasing one engine leaves the other one speaking (#202)") {
    ComScope com;
    Engine keeper(L"en-us");
    HostSite warm;   // an engine's very first item is not the reference (see #135's test)
    keeper.speak(L"Documents", warm);
    HostSite before;
    keeper.speak(L"Desktop", before);
    REQUIRE(before.audio.size() > 0);
    for (int round = 0; round < 2; ++round) {
        CAPTURE(round);
        {
            Engine visitor(L"hu");
            HostSite v;
            visitor.speak(L"asztal", v);
            CHECK(v.audio.size() > 0);
        }   // the visitor goes; in round 0 it is cached, in round 1 an older cached one exists
        {
            Engine other(L"es");
            Engine another(L"en-gb");
            HostSite o;
            other.speak(L"mesa", o);
            CHECK(o.audio.size() > 0);
        }   // two go at once: one of them is destroyed outright
        HostSite after;
        keeper.speak(L"Desktop", after);
        CHECK(after.audio.size() > 0);
        // The same word, as long as before and starting where it did.  (Not
        // sample for sample: the noise source does not repeat itself.)
        CHECK(after.audio.size() == before.audio.size());
        const double shiftMs = (static_cast<double>(onset(after.audio, 400)) - static_cast<double>(onset(before.audio, 400)))
                               * 1000.0 / keeper.fmt.nSamplesPerSec;
        CHECK(std::fabs(shiftMs) <= 2.0);
        const double level = static_cast<double>(peakIn(after.audio, 0, after.audio.size()))
                             / std::max(1, peakIn(before.audio, 0, before.audio.size()));
        CHECK(level > 0.7);
        CHECK(level < 1.4);
    }
}

// #204: a bookmark is a promise to the host.  NVDA's SAPI5 driver moves the
// caret and keeps Say All going on them, JAWS puts one between words.  When
// the text they came with made no sound (a line that is only a dash rule, an
// ellipsis, a no-break space), the engine dropped them: a clause that
// rendered nothing skipped its own flush, and a batch that rendered nothing
// skipped the end-of-batch one.
TEST_CASE("bookmarks fire when their text makes no sound (#204)") {
    ComScope com;
    Engine engine(L"en-us");
    struct Case { const wchar_t* text; bool silent; };
    const Case cases[] = {
        {L"x", false},            // the control: a text that speaks
        {L"-", true},
        {L"-----", true},
        {L"...", true},
        {L"—", true},        // em dash
        {L" ", true},        // no-break space
        {L"​", true},        // zero-width space
        {L" ", true},
    };
    const size_t pad = engine.msToBytes(50.0);  // the silence that ends every Speak()
    for (const Case& c : cases) {
        CAPTURE(static_cast<unsigned>(c.text[0]));
        CAPTURE(wcslen(c.text));
        HostSite site;
        engine.speak({{SPVA_Bookmark, L"7"}, {SPVA_Speak, c.text}, {SPVA_Bookmark, L"8"}}, site);
        if (c.silent) CHECK(site.bytes == pad);   // the premise: nothing was spoken
        else CHECK(site.bytes > pad);
        CHECK(site.marks.size() == 2);
        if (site.marks.size() != 2) continue;
        CHECK(site.marks[0].name == L"7");
        CHECK(site.marks[1].name == L"8");
        CHECK(site.marks[0].offset <= site.marks[1].offset);
        CHECK(site.marks[1].offset <= site.bytes);
        CHECK(site.marks[0].offset % sizeof(int16_t) == 0);
        CHECK(site.marks[1].offset % sizeof(int16_t) == 0);
    }

    // Silent text between spoken text, in one Speak() call: every mark, in
    // order, none later than the audio.
    HostSite mixed;
    engine.speak({{SPVA_Bookmark, L"1"}, {SPVA_Speak, L"Desktop."}, {SPVA_Bookmark, L"2"}, {SPVA_Speak, L"-----"},
                  {SPVA_Bookmark, L"3"}, {SPVA_Speak, L"Documents."}, {SPVA_Bookmark, L"4"}}, mixed);
    REQUIRE(mixed.marks.size() == 4);
    for (size_t i = 0; i < 4; ++i) {
        CHECK(mixed.marks[i].name == std::to_wstring(i + 1));
        if (i) CHECK(mixed.marks[i - 1].offset <= mixed.marks[i].offset);
        CHECK(mixed.marks[i].offset <= mixed.bytes);
    }
    // The mark after "Desktop." comes after its audio, not at the start.
    CHECK(mixed.marks[1].offset > mixed.marks[0].offset);
}

// #206: the volume and rate a host sets (SetVolume, SetRate) and the ones the
// text carries (<volume level>, <rate speed|absspeed>) combine.  SAPI's TTS
// Engine Vendor Porting Guide: "the engine should combine these values with
// values obtained through ISpTTSEngineSite::GetRate calls to arrive at a
// final value", and the same for volume.  The markup used to replace the
// host's value unless it was exactly 100 or 0, the two values a fragment
// without markup also carries: at host volume 50 "level 100" played at 50
// and "level 80" at 80, and at host rate +5 "speed 2" was slower than no
// markup at all.
TEST_CASE("volume and rate markup combine with the host's volume and rate (#206)") {
    ComScope com;
    Engine engine(L"en-us");
    const std::wstring text = L"The quick brown fox jumps over the lazy dog.";
    auto render = [&](USHORT siteVolume, ULONG markVolume, long siteRate, long markRate) {
        HostSite site;
        site.volume = siteVolume;
        site.rate = siteRate;
        engine.speak({{SPVA_Speak, text, markRate, markVolume}}, site);
        REQUIRE(site.audio.size() > 0);
        return site.audio;
    };
    auto rms = [](const std::vector<int16_t>& a) {
        double sum = 0.0;
        for (int16_t s : a) sum += static_cast<double>(s) * s;
        return std::sqrt(sum / a.size());
    };

    SUBCASE("volume") {
        const double plain50 = rms(render(50, 100, 0, 0));
        const double mark100at50 = rms(render(50, 100, 0, 0));
        const double mark80at50 = rms(render(50, 80, 0, 0));
        const double mark50at100 = rms(render(100, 50, 0, 0));
        const double mark80at100 = rms(render(100, 80, 0, 0));
        const double plain100 = rms(render(100, 100, 0, 0));
        const double plain40 = rms(render(40, 100, 0, 0));
        MESSAGE("rms: host 50 " << plain50 << ", host 50 level 80 " << mark80at50 << ", host 100 level 50 "
                                << mark50at100 << ", host 100 level 80 " << mark80at100 << ", host 100 " << plain100
                                << ", host 40 " << plain40);
        // The issue's case: at host volume 50, level 100 is not quieter than level 80.
        CHECK(mark100at50 > mark80at50 * 1.05);
        // Level 80 of host volume 50 is volume 40.
        CHECK(mark80at50 == doctest::Approx(plain40).epsilon(0.03));
        // The two multiply, whichever side carries which.
        CHECK(mark50at100 == doctest::Approx(plain50).epsilon(0.03));
        // Markup alone works as it did: quieter than none, louder than half.
        CHECK(mark80at100 < plain100 * 0.95);
        CHECK(mark80at100 > mark50at100 * 1.05);
    }

    SUBCASE("rate") {
        const size_t plain5 = render(100, 100, 5, 0).size();
        const size_t mark2at5 = render(100, 100, 5, 2).size();
        const size_t markMinus2at5 = render(100, 100, 5, -2).size();
        const size_t plain7 = render(100, 100, 7, 0).size();
        const size_t plain3 = render(100, 100, 3, 0).size();
        const size_t mark5at0 = render(100, 100, 0, 5).size();
        const size_t plain0 = render(100, 100, 0, 0).size();
        const size_t plain10 = render(100, 100, 10, 0).size();
        const size_t mark5at10 = render(100, 100, 10, 5).size();
        const size_t plainMinus10 = render(100, 100, -10, 0).size();
        const size_t markMinus5atMinus10 = render(100, 100, -10, -5).size();
        MESSAGE("samples: host +5 " << plain5 << ", host +5 speed 2 " << mark2at5 << ", host +5 speed -2 "
                                    << markMinus2at5 << ", host +7 " << plain7 << ", host +3 " << plain3
                                    << ", host 0 speed 5 " << mark5at0 << ", host 0 " << plain0);
        // The issue's case: at host rate +5, markup that speeds up is not slower than none.
        CHECK(mark2at5 < plain5);
        // +5 and +2 are +7, +5 and -2 are +3.
        CHECK(mark2at5 == plain7);
        CHECK(markMinus2at5 == plain3);
        // Markup alone works as it did.
        CHECK(mark5at0 == plain5);
        CHECK(mark5at0 < plain0);
        // The sum stays inside SAPI's -10..+10.
        CHECK(mark5at10 == plain10);
        CHECK(markMinus5atMinus10 == plainMinus10);
    }
}

// #207: <spell>hello</spell> reaches the engine as one SPVA_SpellOut fragment
// holding "hello", and "the engine should spell out the associated text
// letter by letter" (SAPI 5.4, TTS Engine Vendor Porting Guide).  It was
// spoken as the word.  Each character is now read as a text of its own, the
// way a lone character is in character navigation: by its name from the
// language's letter dictionary, or as the phonemizer names it.
TEST_CASE("text in a spell tag is read letter by letter (#207)") {
    ComScope com;
    // The length of one Speak() call without the 50 ms that ends it.
    auto spoken = [](Engine& engine, SPVACTIONS action, const std::wstring& text, long rate = 0) {
        HostSite site;
        site.rate = rate;
        engine.speak({{action, text}}, site);
        const size_t pad = engine.msToBytes(50.0) / sizeof(int16_t);
        REQUIRE(site.audio.size() >= pad);
        return static_cast<double>(site.audio.size() - pad);
    };
    auto lettersOf = [&](Engine& engine, const std::vector<std::wstring>& letters, long rate = 0) {
        double sum = 0.0;
        for (const std::wstring& l : letters) sum += spoken(engine, SPVA_Speak, l, rate);
        return sum;
    };

    SUBCASE("English, at every rate") {
        Engine engine(L"en-us");
        for (long rate : {-5L, 0L, 3L, 5L, 8L}) {   // speeds 0.5, 1.0, 1.5, 2.0 and 3.0
            CAPTURE(rate);
            const double word = spoken(engine, SPVA_Speak, L"hello", rate);
            const double spelled = spoken(engine, SPVA_SpellOut, L"hello", rate);
            const double letters = lettersOf(engine, {L"h", L"e", L"l", L"l", L"o"}, rate);
            MESSAGE("hello: word " << word << ", spelled " << spelled << ", its five letters " << letters);
            CHECK(spelled > word * 1.5);
            // As long as its letters spoken one by one, give or take the
            // short gap between them.
            CHECK(spelled >= letters * 0.95);
            CHECK(spelled <= letters * 1.25);
        }
    }

    SUBCASE("one character is read as it always was") {
        Engine engine(L"en-us");
        CHECK(spoken(engine, SPVA_SpellOut, L"h") == spoken(engine, SPVA_Speak, L"h"));
        CHECK(spoken(engine, SPVA_SpellOut, L"7") == spoken(engine, SPVA_Speak, L"7"));
    }

    SUBCASE("capitals, digits, spaces and characters outside the BMP") {
        Engine engine(L"en-us");
        const double ab = spoken(engine, SPVA_SpellOut, L"AB");
        CHECK(ab > spoken(engine, SPVA_Speak, L"AB") * 0.9);   // "AB" is read as letters anyway
        CHECK(ab >= lettersOf(engine, {L"A", L"B"}) * 0.95);
        const double digits = spoken(engine, SPVA_SpellOut, L"42");
        CHECK(digits >= lettersOf(engine, {L"4", L"2"}) * 0.95);   // "four two", not "forty-two"
        CHECK(digits <= lettersOf(engine, {L"4", L"2"}) * 1.25);
        // White space is no character to spell.
        CHECK(spoken(engine, SPVA_SpellOut, L"a b") == spoken(engine, SPVA_SpellOut, L"ab"));
        // A character outside the BMP (mathematical bold A) is one
        // character, not its two UTF-16 halves.
        const std::wstring boldA = L"\U0001D400";
        const double boldAlone = spoken(engine, SPVA_Speak, boldA);
        REQUIRE(boldAlone > 0.0);
        CHECK(spoken(engine, SPVA_SpellOut, boldA) == boldAlone);
        CHECK(spoken(engine, SPVA_SpellOut, L"x" + boldA) >= lettersOf(engine, {L"x", boldA}) * 0.95);
        // A combining accent stays with its letter: "e" + U+0301 is é.
        CHECK(spoken(engine, SPVA_SpellOut, L"é") == spoken(engine, SPVA_Speak, L"é"));
        // A symbol the phonemizer names is spelled like a letter.
        CHECK(spoken(engine, SPVA_SpellOut, L"a@") >= lettersOf(engine, {L"a", L"@"}) * 0.95);
    }

    SUBCASE("the names come from the language's letter dictionary") {
        // Spanish "y" alone is "i griega" (packs/dict/es-letters.tsv); inside a
        // word it is the vowel.  Spelled, it is the letter.
        Engine engine(L"es");
        const double yName = spoken(engine, SPVA_Speak, L"y");
        const double word = spoken(engine, SPVA_Speak, L"soy");
        const double spelled = spoken(engine, SPVA_SpellOut, L"soy");
        const double letters = lettersOf(engine, {L"s", L"o", L"y"});
        MESSAGE("soy: word " << word << ", spelled " << spelled << ", its letters " << letters << ", y alone " << yName);
        CHECK(spelled >= letters * 0.95);
        CHECK(spelled <= letters * 1.25);
        CHECK(spelled > word + yName * 0.9);
    }

    SUBCASE("spelled text between spoken text, with bookmarks") {
        Engine engine(L"en-us");
        HostSite site;
        engine.speak({{SPVA_Bookmark, L"1"}, {SPVA_Speak, L"Type"}, {SPVA_Bookmark, L"2"}, {SPVA_SpellOut, L"ok"},
                      {SPVA_Bookmark, L"3"}, {SPVA_Speak, L"now"}, {SPVA_Bookmark, L"4"}}, site);
        const double pad = static_cast<double>(engine.msToBytes(50.0) / sizeof(int16_t));
        const double parts = spoken(engine, SPVA_Speak, L"Type") + lettersOf(engine, {L"o", L"k"})
                             + spoken(engine, SPVA_Speak, L"now");
        CHECK(site.audio.size() - pad >= parts * 0.95);
        REQUIRE(site.marks.size() == 4);
        for (size_t i = 0; i < 4; ++i) {
            CHECK(site.marks[i].name == std::to_wstring(i + 1));
            if (i) CHECK(site.marks[i - 1].offset < site.marks[i].offset);
        }
    }
}

// #208: a reading app's "next sentence" is ISpVoice::Skip.  The engine sees
// SPVES_SKIP, asks GetSkipInfo what to skip and how many, moves, and reports
// with CompleteSkip how many it skipped.  "If it was able to successfully
// skip the entire number returned by GetSkipInfo, the engine should then
// continue rendering text at the appropriate point.  Otherwise, it should
// abort the current Speak call and return immediately" (SAPI 5.4, TTS Engine
// Vendor Porting Guide; its example: +1 while in sentence two continues with
// sentence three, -2 from sentence two reports -1 and aborts).  The engine
// answered every skip with CompleteSkip(0) and stopped speaking.
TEST_CASE("a skip moves by sentences and speech goes on (#208)") {
    ComScope com;
    Engine engine(L"en-us");
    // Three sentences that cannot be mistaken for one another by length.
    const std::wstring first = L"First sentence here.";
    const std::wstring second = L"The second sentence is a good deal longer than the other two are, so it cannot be mistaken.";
    const std::wstring third = L"Third one.";
    const std::wstring all = first + L" " + second + L" " + third;
    auto samples = [&](const std::wstring& text) {
        HostSite site;
        engine.speak(text, site);
        return static_cast<double>(site.audio.size());
    };
    const double lenAll = samples(all);
    const double lenFromSecond = samples(second + L" " + third);
    const double lenThird = samples(third);
    const double lenFirst = samples(first);
    REQUIRE(lenFromSecond > lenThird * 3);
    REQUIRE(lenAll > lenFromSecond * 1.1);
    const size_t inFirst = engine.msToBytes(20.0);
    // Well inside the second sentence: past the first one and the pause after it.
    const size_t inSecond = static_cast<size_t>(lenFirst) * sizeof(int16_t) + engine.msToBytes(1000.0);
    REQUIRE(inSecond < static_cast<size_t>(lenAll - lenThird) * sizeof(int16_t) - engine.msToBytes(1000.0));

    struct Case { const char* what; size_t at; long count; long reported; double rest; };
    const Case cases[] = {
        {"+1 in the first sentence: on with the second", inFirst, 1, 1, lenFromSecond},
        {"+2 in the first sentence: on with the third", inFirst, 2, 2, lenThird},
        {"+1 in the second sentence: on with the third", inSecond, 1, 1, lenThird},
        {"0 in the second sentence: the second again", inSecond, 0, 0, lenFromSecond},
        {"-1 in the second sentence: back to the first", inSecond, -1, -1, lenAll},
        {"0 in the first sentence: the first again", inFirst, 0, 0, lenAll},
        // Fewer sentences than asked for: say how many, then stop.
        {"+3 in the first sentence: only two follow", inFirst, 3, 2, 0.0},
        {"+2 in the second sentence: only one follows", inSecond, 2, 1, 0.0},
        {"-1 in the first sentence: none before it", inFirst, -1, 0, 0.0},
        {"-2 in the second sentence: only one before it", inSecond, -2, -1, 0.0},
    };
    SUBCASE("forward, backward, and further than the text goes") for (const Case& c : cases) {
        const std::string what = c.what;
        CAPTURE(what);
        HostSite site;
        site.skipAfterBytes = c.at;
        site.skipCount = c.count;
        engine.speak(all, site);
        REQUIRE(site.skipsCompleted.size() == 1);
        CHECK(site.skipsCompleted[0] == c.reported);
        const double rest = static_cast<double>(site.bytes - site.bytesAtSkip) / sizeof(int16_t);
        MESSAGE(what << ": reported " << site.skipsCompleted[0] << ", " << rest << " samples after the skip, expected "
                     << c.rest);
        // What is spoken after the skip is the text from the sentence it
        // landed on, with the silence that ends every Speak(); nothing at
        // all when the skip could not be met.
        CHECK(rest == doctest::Approx(c.rest).epsilon(0.02));
        // The engine is as good as new afterwards.
        HostSite next;
        engine.speak(third, next);
        CHECK(static_cast<double>(next.audio.size()) == doctest::Approx(lenThird).epsilon(0.01));
    }

    SUBCASE("bookmarks of skipped text are not reported, the others are") {
        HostSite site;
        site.skipAfterBytes = inFirst;
        site.skipCount = 1;
        engine.speak({{SPVA_Bookmark, L"1"}, {SPVA_Speak, first}, {SPVA_Bookmark, L"2"}, {SPVA_Speak, second},
                      {SPVA_Bookmark, L"3"}, {SPVA_Speak, third}, {SPVA_Bookmark, L"4"}}, site);
        REQUIRE(site.skipsCompleted.size() == 1);
        CHECK(site.skipsCompleted[0] == 1);
        // "1" was passed before the skip; "2" stands before the second
        // sentence, where speech goes on, and "3" and "4" follow.
        REQUIRE(site.marks.size() == 4);
        for (size_t i = 0; i < 4; ++i) {
            CHECK(site.marks[i].name == std::to_wstring(i + 1));
            if (i) CHECK(site.marks[i - 1].offset <= site.marks[i].offset);
            CHECK(site.marks[i].offset <= site.bytes);
        }
        CHECK(site.marks[1].offset >= site.bytesAtSkip);

        HostSite two;
        two.skipAfterBytes = inFirst;
        two.skipCount = 2;
        engine.speak({{SPVA_Bookmark, L"1"}, {SPVA_Speak, first}, {SPVA_Bookmark, L"2"}, {SPVA_Speak, second},
                      {SPVA_Bookmark, L"3"}, {SPVA_Speak, third}, {SPVA_Bookmark, L"4"}}, two);
        REQUIRE(two.marks.size() == 3);   // "2" belonged to the sentence skipped over
        CHECK(two.marks[0].name == L"1");
        CHECK(two.marks[1].name == L"3");
        CHECK(two.marks[2].name == L"4");
    }

    SUBCASE("sentences in separate fragments, with spelled text between") {
        HostSite plain;
        engine.speak({{SPVA_Speak, third}}, plain);
        HostSite site;
        site.skipAfterBytes = inFirst;
        site.skipCount = 2;   // over the rest of the first sentence and the spelled text
        engine.speak({{SPVA_Speak, first}, {SPVA_SpellOut, L"ok"}, {SPVA_Speak, third}}, site);
        REQUIRE(site.skipsCompleted.size() == 1);
        CHECK(site.skipsCompleted[0] == 2);
        CHECK(static_cast<double>(site.bytes - site.bytesAtSkip) / sizeof(int16_t)
              == doctest::Approx(static_cast<double>(plain.audio.size())).epsilon(0.02));
    }

    SUBCASE("a host that cannot say what to skip is answered as before") {
        // GetSkipInfo fails: nothing is skipped and the call ends.
        struct NoInfoSite : HostSite {
            STDMETHODIMP GetSkipInfo(SPVSKIPTYPE*, long*) override { return E_NOTIMPL; }
        } site;
        site.skipAfterBytes = inFirst;
        engine.speak(all, site);
        REQUIRE(site.skipsCompleted.size() == 1);
        CHECK(site.skipsCompleted[0] == 0);
        CHECK(site.bytes == site.bytesAtSkip);
    }
}

// #210: the settings app's pitch mode and inflection scale are written into
// the loaded language pack before each utterance.  Taking them away again
// ("Use Language Default", Reset) sent nothing, so the pack kept the last
// mode and scale until the host was restarted or another language was
// spoken.  The pitch of the same sentence tells the modes apart: two renders
// with the same settings are within 0.1 Hz of each other, two modes several
// hertz apart.
TEST_CASE("a pitch mode set back to the language default is the language's own again (#210)") {
    ComScope com;
    struct Restore { ~Restore() { writeSettings(""); } } restore;
    const double same = 0.5, different = 3.0;   // Hz
    auto pitchOf = [](Engine& engine, const std::wstring& text, const std::string& ini) {
        writeSettings(ini);
        HostSite site;
        engine.speak(text, site);
        return pitchTrack(site.audio, engine.fmt.nSamplesPerSec);
    };
    const std::wstring english = L"We were away a year ago, and now we are home again.";

    SUBCASE("every mode the settings app offers, and back") {
        writeSettings("");
        Engine engine(L"en-us");
        const auto own = pitchOf(engine, english, "");
        CHECK(pitchDistance(own, pitchOf(engine, english, "")) < same);
        // en-us is espeak_style by its own pack, so that one changes nothing.
        for (const char* mode : {"fujisaki_style", "impulse_style", "klatt_style", "legacy", "arato_style", "espeak_style"}) {
            const std::string name = mode;
            CAPTURE(name);
            const auto chosen = pitchOf(engine, english, "[Audio]\npitchMode=" + name + "\n");
            if (name != "espeak_style") CHECK(pitchDistance(own, chosen) > different);   // the mode is heard
            // "Use Language Default", as the settings app saves it.
            const auto back = pitchOf(engine, english, "[Audio]\npitchMode=\n");
            CHECK(pitchDistance(own, back) < same);
        }
        // The settings file removed altogether.
        CHECK(pitchDistance(own, pitchOf(engine, english, "[Audio]\npitchMode=fujisaki_style\n")) > different);
        CHECK(pitchDistance(own, pitchOf(engine, english, "")) < same);
    }

    SUBCASE("the inflection scale, with the mode kept") {
        writeSettings("[Audio]\npitchMode=legacy\n");
        Engine engine(L"en-us");
        const auto legacy = pitchOf(engine, english, "[Audio]\npitchMode=legacy\n");
        const auto scaled = pitchOf(engine, english, "[Audio]\npitchMode=legacy\npitchInflectionScale=100\n");
        CHECK(pitchDistance(legacy, scaled) > different);
        const auto unset = pitchOf(engine, english, "[Audio]\npitchMode=legacy\npitchInflectionScale=-1\n");
        CHECK(pitchDistance(legacy, unset) < same);
        // And the key gone from the file.
        CHECK(pitchDistance(legacy, pitchOf(engine, english, "[Audio]\npitchMode=legacy\npitchInflectionScale=100\n")) > different);
        CHECK(pitchDistance(legacy, pitchOf(engine, english, "[Audio]\npitchMode=legacy\n")) < same);
    }

    SUBCASE("the mode, with the inflection scale kept") {
        writeSettings("");
        Engine engine(L"en-us");
        const auto own = pitchOf(engine, english, "");
        const auto scaled = pitchOf(engine, english, "[Audio]\npitchMode=legacy\npitchInflectionScale=100\n");
        CHECK(pitchDistance(own, scaled) > different);
        // The scale is still set, but en-us's own mode does not use it.
        const auto back = pitchOf(engine, english, "[Audio]\npitchMode=\npitchInflectionScale=100\n");
        CHECK(pitchDistance(own, back) < same);
        // Choosing the mode again finds the scale still there.
        const auto again = pitchOf(engine, english, "[Audio]\npitchMode=legacy\npitchInflectionScale=100\n");
        CHECK(pitchDistance(scaled, again) < same);
    }

    SUBCASE("a new voice object, which takes over the cached runtime") {
        writeSettings("");
        std::vector<double> own;
        {
            Engine engine(L"en-us");
            own = pitchOf(engine, english, "");
            CHECK(pitchDistance(own, pitchOf(engine, english, "[Audio]\npitchMode=fujisaki_style\n")) > different);
        }
        Engine engine(L"en-us");
        CHECK(pitchDistance(own, pitchOf(engine, english, "[Audio]\npitchMode=\n")) < same);
    }

    SUBCASE("Hungarian, and a language spoken in between") {
        const std::wstring hungarian = L"Tavaly ilyenkor nem voltunk itthon, de most megint itt vagyunk.";
        writeSettings("");
        Engine hu(L"hu");
        Engine en(L"en-us");
        const auto ownHu = pitchOf(hu, hungarian, "");
        const auto ownEn = pitchOf(en, english, "");
        CHECK(pitchDistance(ownHu, pitchOf(hu, hungarian, "")) < same);
        CHECK(pitchDistance(ownHu, pitchOf(hu, hungarian, "[Audio]\npitchMode=fujisaki_style\n")) > different);
        CHECK(pitchDistance(ownEn, pitchOf(en, english, "[Audio]\npitchMode=fujisaki_style\n")) > different);
        CHECK(pitchDistance(ownEn, pitchOf(en, english, "[Audio]\npitchMode=\n")) < same);
        CHECK(pitchDistance(ownHu, pitchOf(hu, hungarian, "[Audio]\npitchMode=\n")) < same);
    }
}

// #212: going from a profile voice to a built-in one, the first clause the
// built-in voice spoke still had the profile's voice source (its spectral
// tilt, speed quotient, chorus): the tone was sent to the DSP before the
// profile was cleared, and not again until the next clause.  It is what a
// host reads out right after the change, usually the new voice's name.
TEST_CASE("a built-in voice chosen after a profile voice speaks with its own voice source at once (#212)") {
    ComScope com;
    writeSettings("");
    const std::wstring text = L"We were away all year.";   // voiced throughout: no noise to blur the measure
    // The first two things a new voice object says.  The runtime of the
    // voice object before it is cached and taken over, as in a host that
    // switches voices.
    struct FirstTwo { double first, second; };
    auto firstTwo = [&](const wchar_t* voice) {
        Engine engine(L"en-us", voice);
        HostSite a, b;
        engine.speak(text, a);
        engine.speak(text, b);
        REQUIRE(a.audio.size() > 0);
        return FirstTwo{brightness(a.audio), brightness(b.audio)};
    };
    auto apart = [](double x, double y) { return std::fabs(x - y) / std::min(x, y); };

    const double adam = firstTwo(L"Adam").second;
    const double beth = firstTwo(L"profile:Beth").second;
    const double bobby = firstTwo(L"profile:Bobby").second;
    MESSAGE("brightness: Adam " << adam << ", Beth " << beth << ", Bobby " << bobby);
    // The premise: the profiles do not sound like Adam, by a wide margin.
    REQUIRE(apart(adam, beth) > 0.30);

    struct Case { const wchar_t* before; const wchar_t* voice; double expected; };
    const Case cases[] = {
        {L"profile:Beth", L"Adam", adam},            // the issue
        {L"profile:Bobby", L"Adam", adam},
        {L"profile:Beth", L"David", 0.0},            // another built-in voice: as its own second item
        {L"Adam", L"Adam", adam},                    // the switches that worked still do
        {L"Adam", L"profile:Beth", beth},
        {L"profile:Bobby", L"profile:Beth", beth},
        {L"profile:Beth", L"profile:Bobby", bobby},
    };
    for (const Case& c : cases) {
        const std::wstring names = std::wstring(c.before) + L" -> " + c.voice;
        const std::string switched(names.begin(), names.end());   // ASCII names
        CAPTURE(switched);
        firstTwo(c.before);
        const FirstTwo got = firstTwo(c.voice);
        MESSAGE("first " << got.first << ", second " << got.second);
        // The first thing the new voice says sounds like the second.
        CHECK(apart(got.first, got.second) < 0.08);
        if (c.expected > 0.0) CHECK(apart(got.first, c.expected) < 0.08);
    }

    // Two clauses in the first Speak() call: the first clause is not the
    // odd one out.
    firstTwo(L"profile:Beth");
    Engine engine(L"en-us", L"Adam");
    HostSite two, again;
    engine.speak(text + L" " + text, two);
    engine.speak(text + L" " + text, again);
    const std::vector<int16_t> firstHalf(two.audio.begin(), two.audio.begin() + two.audio.size() / 2);
    const std::vector<int16_t> firstHalfAgain(again.audio.begin(), again.audio.begin() + again.audio.size() / 2);
    CHECK(apart(brightness(firstHalf), brightness(firstHalfAgain)) < 0.08);
}
