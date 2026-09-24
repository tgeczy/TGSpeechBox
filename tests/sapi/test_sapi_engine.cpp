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

// A SAPI host, as the engine sees it.
class HostSite : public ISpTTSEngineSite {
public:
    std::vector<int16_t> audio;
    // Answer SPVES_ABORT once this much audio has been written (bytes).
    size_t abortAfterBytes = SIZE_MAX;
    long rate = 0;
    size_t bytes = 0;
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
    STDMETHODIMP AddEvents(const SPEVENT*, ULONG) override { return S_OK; }
    STDMETHODIMP GetEventInterest(ULONGLONG* p) override {
        if (p) *p = SPFEI_ALL_TTS_EVENTS;
        return S_OK;
    }

    // ISpTTSEngineSite
    STDMETHODIMP_(DWORD) GetActions() override {
        return bytes >= abortAfterBytes ? SPVES_ABORT : SPVES_CONTINUE;
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
    STDMETHODIMP GetVolume(USHORT* p) override { if (p) *p = 100; return S_OK; }
    STDMETHODIMP GetSkipInfo(SPVSKIPTYPE*, long*) override { return E_NOTIMPL; }
    STDMETHODIMP CompleteSkip(long) override { return S_OK; }
};

struct Engine {
    HMODULE dll = nullptr;
    ISpTTSEngine* tts = nullptr;
    ISpObjectWithToken* withToken = nullptr;
    WAVEFORMATEX fmt{};

    explicit Engine(const wchar_t* langTag) {
        // The token: Attributes\TGSpeech_LangTag + TGSpeech_Preset.
        HKEY attrs = nullptr;
        const std::wstring attrsKey = std::wstring(kTokenKey) + L"\\Attributes";
        REQUIRE(RegCreateKeyExW(HKEY_CURRENT_USER, attrsKey.c_str(), 0, nullptr, 0, KEY_WRITE, nullptr, &attrs, nullptr) == ERROR_SUCCESS);
        auto setStr = [&](const wchar_t* name, const wchar_t* value) {
            RegSetValueExW(attrs, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value),
                           static_cast<DWORD>((wcslen(value) + 1) * sizeof(wchar_t)));
        };
        setStr(L"TGSpeech_LangTag", langTag);
        setStr(L"TGSpeech_Preset", L"Adam");
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

    size_t msToBytes(double ms) const {
        return static_cast<size_t>(ms * fmt.nSamplesPerSec / 1000.0) * sizeof(int16_t);
    }
};

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
