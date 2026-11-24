// Meridian: unified audio engine for the Midnight flying saucer stereo fly-by
// and hidden Settings > About full-track stereo playback.
// Decodes embedded M4A/AAC resources purely in memory via dynamically loaded Windows Media Foundation
// (no temporary files on disk, no extra link libraries, graceful silence if MF is unavailable).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <shlwapi.h>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <vector>
#include <cmath>
#include <cstring>
#include <algorithm>
#include "ufo_audio.h"
#include "ufo_audio_codec.h"
#include "resource.h"

namespace ufoaudio {

namespace {

// Local copies of standard Media Foundation GUIDs so no -lmfuuid link dependency is required.
constexpr GUID kMFMediaType_Audio                 = {0x73647561, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
constexpr GUID kMFAudioFormat_PCM                 = {0x00000001, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
constexpr GUID kMF_MT_MAJOR_TYPE                  = {0x48eba18e, 0xf8c9, 0x4687, {0xbf, 0x11, 0x0a, 0x74, 0xc9, 0xf9, 0x6a, 0x8f}};
constexpr GUID kMF_MT_SUBTYPE                     = {0xf7e34c9a, 0x42e8, 0x4714, {0xb7, 0x4b, 0xcb, 0x29, 0xd7, 0x2c, 0x35, 0xe5}};
constexpr GUID kMF_MT_AUDIO_NUM_CHANNELS          = {0x37e48bf5, 0x645e, 0x4c5b, {0x89, 0xde, 0xad, 0xa9, 0xe2, 0x9b, 0x69, 0x6a}};
constexpr GUID kMF_MT_AUDIO_SAMPLES_PER_SECOND    = {0x5faeeae7, 0x0290, 0x4c31, {0x9e, 0x8a, 0xc5, 0x34, 0xf6, 0x8d, 0x9d, 0xba}};
constexpr GUID kMF_MT_AUDIO_BITS_PER_SAMPLE       = {0xf2deb57f, 0x40fa, 0x4764, {0xaa, 0x33, 0xed, 0x4f, 0x2d, 0x1f, 0xf6, 0x69}};
constexpr GUID kIID_IMFAttributes                 = {0x2cd2d921, 0xc447, 0x44a7, {0xa1, 0x3c, 0x4a, 0xda, 0xbf, 0xc2, 0x47, 0xe3}};
constexpr GUID kMF_BYTESTREAM_ORIGIN_NAME         = {0xfc358288, 0x3cb6, 0x460c, {0xa4, 0x24, 0xb6, 0x68, 0x12, 0x60, 0x37, 0x5a}};
constexpr GUID kMF_BYTESTREAM_CONTENT_TYPE        = {0xfc358289, 0x3cb6, 0x460c, {0xa4, 0x24, 0xb6, 0x68, 0x12, 0x60, 0x37, 0x5a}};

template <typename Fn>
Fn loadProc(HMODULE mod, const char* name) {
    return reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(mod, name)));
}

std::atomic<LibraryLoaderFn> gLibLoader{nullptr};
std::atomic<uint64_t> gInitAttempts{0};

struct MfRuntime {
    HMODULE hShlwapi = nullptr;
    HMODULE hMfplat = nullptr;
    HMODULE hMfread = nullptr;
    bool comInit = false;
    bool mfInit = false;

    using FnSHCreateMemStream = IStream* (WINAPI*)(const BYTE*, UINT);
    using FnMFStartup = HRESULT (STDAPICALLTYPE*)(ULONG, DWORD);
    using FnMFShutdown = HRESULT (STDAPICALLTYPE*)();
    using FnMFCreateMediaType = HRESULT (STDAPICALLTYPE*)(IMFMediaType**);
    using FnMFCreateMFByteStreamOnStream = HRESULT (STDAPICALLTYPE*)(IStream*, IMFByteStream**);
    using FnMFCreateAttributes = HRESULT (STDAPICALLTYPE*)(IMFAttributes**, UINT32);
    using FnMFCreateSourceReaderFromByteStream = HRESULT (STDAPICALLTYPE*)(IMFByteStream*, IMFAttributes*, IMFSourceReader**);

    FnSHCreateMemStream pSHCreateMemStream = nullptr;
    FnMFStartup pMFStartup = nullptr;
    FnMFShutdown pMFShutdown = nullptr;
    FnMFCreateMediaType pMFCreateMediaType = nullptr;
    FnMFCreateMFByteStreamOnStream pMFCreateMFByteStreamOnStream = nullptr;
    FnMFCreateAttributes pMFCreateAttributes = nullptr;
    FnMFCreateSourceReaderFromByteStream pMFCreateSourceReaderFromByteStream = nullptr;

    bool init() {
        gInitAttempts.fetch_add(1, std::memory_order_relaxed);
        auto loadSys = [](const wchar_t* name) -> HMODULE {
            if (LibraryLoaderFn fn = gLibLoader.load(std::memory_order_acquire))
                return static_cast<HMODULE>(fn(name));
            return LoadLibraryExW(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        };
        hShlwapi = loadSys(L"shlwapi.dll");
        hMfplat  = loadSys(L"mfplat.dll");
        hMfread  = loadSys(L"mfreadwrite.dll");
        if (!hShlwapi || !hMfplat || !hMfread) { shutdown(); return false; }

        pSHCreateMemStream = loadProc<FnSHCreateMemStream>(hShlwapi, "SHCreateMemStream");
        pMFStartup = loadProc<FnMFStartup>(hMfplat, "MFStartup");
        pMFShutdown = loadProc<FnMFShutdown>(hMfplat, "MFShutdown");
        pMFCreateMediaType = loadProc<FnMFCreateMediaType>(hMfplat, "MFCreateMediaType");
        pMFCreateMFByteStreamOnStream = loadProc<FnMFCreateMFByteStreamOnStream>(hMfplat, "MFCreateMFByteStreamOnStream");
        pMFCreateAttributes = loadProc<FnMFCreateAttributes>(hMfplat, "MFCreateAttributes");
        pMFCreateSourceReaderFromByteStream = loadProc<FnMFCreateSourceReaderFromByteStream>(hMfread, "MFCreateSourceReaderFromByteStream");
        if (!pSHCreateMemStream || !pMFStartup || !pMFShutdown || !pMFCreateMediaType ||
            !pMFCreateMFByteStreamOnStream || !pMFCreateSourceReaderFromByteStream) {
            shutdown();
            return false;
        }
        HRESULT hrCom = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        comInit = SUCCEEDED(hrCom);
        mfInit = SUCCEEDED(pMFStartup(MF_VERSION, MFSTARTUP_LITE));
        if (!mfInit) { shutdown(); return false; }
        return true;
    }

    void shutdown() {
        if (mfInit && pMFShutdown) { pMFShutdown(); mfInit = false; }
        if (comInit) { CoUninitialize(); comInit = false; }
        if (hMfread) { FreeLibrary(hMfread); hMfread = nullptr; }
        if (hMfplat) { FreeLibrary(hMfplat); hMfplat = nullptr; }
        if (hShlwapi) { FreeLibrary(hShlwapi); hShlwapi = nullptr; }
    }

    ~MfRuntime() { shutdown(); }
};

bool findResourceBlob(int resId, const uint8_t*& ptrOut, size_t& sizeOut) {
    ptrOut = nullptr;
    sizeOut = 0;
    HRSRC res = FindResourceW(nullptr, MAKEINTRESOURCEW(resId), RT_RCDATA);
    if (!res) return false;
    HGLOBAL hg = LoadResource(nullptr, res);
    if (!hg) return false;
    const uint8_t* ptr = (const uint8_t*)LockResource(hg);
    const DWORD sz = SizeofResource(nullptr, res);
    if (!ptr || sz < 16) return false;
    ptrOut = ptr;
    sizeOut = (size_t)sz;
    return true;
}

// Pure in-memory M4A/AAC streaming decoder over an embedded RT_RCDATA resource.
struct MfStreamReader {
    IStream* memStream = nullptr;
    IMFByteStream* byteStream = nullptr;
    IMFAttributes* attrs = nullptr;
    IMFSourceReader* reader = nullptr;
    uint32_t sampleRate = 44100;
    uint32_t channels = 2;
    bool eof = false;
    std::vector<int16_t> pending;
    size_t pendingPos = 0;

    bool openResource(const MfRuntime& mf, int resId) {
        close();
        const uint8_t* data = nullptr;
        size_t size = 0;
        if (!findResourceBlob(resId, data, size)) return false;

        memStream = mf.pSHCreateMemStream(data, (UINT)size);
        if (!memStream) return false;
        if (FAILED(mf.pMFCreateMFByteStreamOnStream(memStream, &byteStream)) || !byteStream) { close(); return false; }

        IMFAttributes* bsAttrs = nullptr;
        if (SUCCEEDED(byteStream->QueryInterface(kIID_IMFAttributes, reinterpret_cast<void**>(&bsAttrs))) && bsAttrs) {
            bsAttrs->SetString(kMF_BYTESTREAM_CONTENT_TYPE, L"audio/mp4");
            bsAttrs->SetString(kMF_BYTESTREAM_ORIGIN_NAME, L"track.m4a");
            bsAttrs->Release();
        }
        if (mf.pMFCreateAttributes) mf.pMFCreateAttributes(&attrs, 1);
        if (FAILED(mf.pMFCreateSourceReaderFromByteStream(byteStream, attrs, &reader)) || !reader) { close(); return false; }

        reader->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
        reader->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);

        IMFMediaType* partial = nullptr;
        IMFMediaType* actual = nullptr;
        bool ok = false;
        if (SUCCEEDED(mf.pMFCreateMediaType(&partial)) && partial) {
            partial->SetGUID(kMF_MT_MAJOR_TYPE, kMFMediaType_Audio);
            partial->SetGUID(kMF_MT_SUBTYPE, kMFAudioFormat_PCM);
            partial->SetUINT32(kMF_MT_AUDIO_BITS_PER_SAMPLE, 16);
            partial->SetUINT32(kMF_MT_AUDIO_NUM_CHANNELS, 2);
            partial->SetUINT32(kMF_MT_AUDIO_SAMPLES_PER_SECOND, 44100);
            if (SUCCEEDED(reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, nullptr, partial))) {
                if (SUCCEEDED(reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, &actual)) && actual) {
                    UINT32 sr = 44100, ch = 2;
                    actual->GetUINT32(kMF_MT_AUDIO_SAMPLES_PER_SECOND, &sr);
                    actual->GetUINT32(kMF_MT_AUDIO_NUM_CHANNELS, &ch);
                    if (sr >= 8000 && sr <= 192000 && ch == 2) {
                        sampleRate = sr;
                        channels = ch;
                        ok = true;
                    }
                }
            }
        }
        if (actual) actual->Release();
        if (partial) partial->Release();
        if (!ok) close();
        return ok;
    }

    // Reads up to `maxFrames` interleaved 2-channel 16-bit PCM frames into `dst`. Returns actual frames read.
    size_t readStereoFrames(int16_t* dst, size_t maxFrames) {
        if (!reader || !dst || maxFrames == 0) return 0;
        size_t written = 0;
        int emptyReads = 0;
        while (written < maxFrames) {
            if (pendingPos < pending.size()) {
                const size_t availFrames = (pending.size() - pendingPos) / 2;
                const size_t take = std::min(maxFrames - written, availFrames);
                std::memcpy(dst + written * 2, pending.data() + pendingPos, take * 2 * sizeof(int16_t));
                pendingPos += take * 2;
                written += take;
                if (pendingPos >= pending.size()) { pending.clear(); pendingPos = 0; }
                emptyReads = 0;
                continue;
            }
            if (eof) break;
            DWORD flags = 0;
            IMFSample* sample = nullptr;
            HRESULT hr = reader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, nullptr, &flags, nullptr, &sample);
            if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM)) {
                if (sample) sample->Release();
                eof = true;
                break;
            }
            if (sample) {
                IMFMediaBuffer* buf = nullptr;
                if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buf)) && buf) {
                    BYTE* pAudio = nullptr;
                    DWORD cbLen = 0;
                    if (SUCCEEDED(buf->Lock(&pAudio, nullptr, &cbLen)) && pAudio && cbLen >= 4) {
                        const size_t nSamp = (cbLen / 4) * 2;
                        const int16_t* s16 = reinterpret_cast<const int16_t*>(pAudio);
                        pending.assign(s16, s16 + nSamp);
                        pendingPos = 0;
                        buf->Unlock();
                    }
                    buf->Release();
                }
                sample->Release();
            }
            if (pending.empty() && ++emptyReads >= 64) {
                eof = true;
                break;
            }
        }
        return written;
    }

    void close() {
        if (reader) { reader->Release(); reader = nullptr; }
        if (attrs) { attrs->Release(); attrs = nullptr; }
        if (byteStream) { byteStream->Release(); byteStream = nullptr; }
        if (memStream) { memStream->Release(); memStream = nullptr; }
        pending.clear();
        pendingPos = 0;
        eof = false;
    }

    ~MfStreamReader() { close(); }
};

bool decodeUfoFlybyClip(const MfRuntime& mf, Clip& out) {
    MfStreamReader sr;
    if (!sr.openResource(mf, IDR_UFO_SONG)) return false;
    const size_t wantFrames = (size_t)sr.sampleRate * 11;          // 11 s > 9 s saucer flight + encoder padding
    std::vector<int16_t> stereo;
    stereo.reserve(wantFrames * 2);
    int16_t chunk[4096 * 2];
    while (stereo.size() < wantFrames * 2) {
        const size_t n = sr.readStereoFrames(chunk, std::min<size_t>(4096, wantFrames - stereo.size() / 2));
        if (n == 0) break;
        stereo.insert(stereo.end(), chunk, chunk + n * 2);
    }
    return buildFlybyMonoClip(stereo.data(), stereo.size(), sr.sampleRate, out);
}

// ---------------------------------------------------------------- shared engine state
std::atomic<bool> gEnabled{false};
std::atomic<bool> gSuppressed{false};

std::mutex gMu;
std::condition_variable gCv;
std::thread gThread;
bool gThreadStarted = false;
bool gWorkerExited = false;
bool gShuttingDown = false;
uint64_t gFailureSeq = 0;
std::string gLastError;
bool gPendingTrackFailure = false;

// UFO fly-by state
bool gActive = false;
bool gRestart = false;
uint64_t gFlightId = 0;
uint64_t gFailedFlightId = ~0ULL;
float gTargetPan = 1.f;
float gTargetGain = 0.f;
float gInitialPan = 1.f;
ULONGLONG gLastTick = 0;

// Full-track cheat jukebox state
Track gTrack = Track::None;
bool gJukebox = false;
uint64_t gTrackSeq = 0;
uint64_t gFailedTrackSeq = ~0ULL;

Track nextJukeboxTrack(Track cur) {
    static constexpr Track kPlaylist[] = {Track::HippyShit, Track::Researched};
    constexpr size_t N = sizeof(kPlaylist) / sizeof(kPlaylist[0]);
    for (size_t i = 0; i + 1 < N; ++i) {
        if (kPlaylist[i] == cur) return kPlaylist[i + 1];
    }
    return Track::None;
}

void playFullTrack(const MfRuntime& mf, Track track, uint64_t seq) {
    int resId = IDR_UFO_SONG;
    if (track == Track::Researched) resId = IDR_RESEARCHED_SONG;
    else if (track == Track::AudioTest) resId = IDR_AUDIOTEST_SONG;
    MfStreamReader reader;
    if (!reader.openResource(mf, resId)) {
        std::lock_guard<std::mutex> lk(gMu);
        if (gTrackSeq == seq) {
            gFailedTrackSeq = seq;
            gTrack = Track::None;
            gJukebox = false;
            gLastError = "Could not decode embedded audio track";
            gPendingTrackFailure = true;
            ++gFailureSeq;
            gCv.notify_all();
        }
        return;
    }

    WAVEFORMATEX fmt{};
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 2;
    fmt.nSamplesPerSec = (DWORD)reader.sampleRate;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = 4;
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;

    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HWAVEOUT wo = nullptr;
    if (!ev || waveOutOpen(&wo, WAVE_MAPPER, &fmt, (DWORD_PTR)ev, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        if (ev) CloseHandle(ev);
        std::lock_guard<std::mutex> lk(gMu);
        if (gTrackSeq == seq) {
            gFailedTrackSeq = seq;
            gTrack = Track::None;
            gJukebox = false;
            gLastError = "Audio output device is unavailable";
            gPendingTrackFailure = true;
            ++gFailureSeq;
            gCv.notify_all();
        }
        return;
    }
    {
        std::lock_guard<std::mutex> lk(gMu);
        gLastError.clear();
        gPendingTrackFailure = false;
    }

    constexpr int NB = 4;
    constexpr size_t FR = 2048;                                    // ~46 ms at 44.1 kHz: smooth glitch-free full-track streaming
    std::vector<int16_t> ring((size_t)NB * FR * 2, 0);
    WAVEHDR hdr[NB]{};
    bool queued[NB] = {};

    const float masterVol = 0.85f;
    const float fadeInStep = 1.f / (0.22f * (float)reader.sampleRate);
    const float fadeOutStep = 1.f / (0.16f * (float)reader.sampleRate);
    float gain = 0.f;
    bool stopping = false;
    bool draining = false;
    bool naturalEnd = false;

    auto shouldContinue = [&]() -> bool {
        std::lock_guard<std::mutex> lk(gMu);
        if (gShuttingDown || gSuppressed.load(std::memory_order_relaxed)) return false;
        return gTrack == track && gTrackSeq == seq;
    };

    auto submit = [&](int idx, bool wantPlay) -> bool {
        int16_t* dst = &ring[(size_t)idx * FR * 2];
        if (!wantPlay) stopping = true;
        const size_t got = reader.readStereoFrames(dst, FR);
        if (got < FR) {
            std::memset(dst + got * 2, 0, (FR - got) * 2 * sizeof(int16_t));
            draining = true;
            if (!stopping && reader.eof) naturalEnd = true;
        }
        if (got == 0) return false;
        for (size_t f = 0; f < got; ++f) {
            if (!stopping) gain = std::min(1.f, gain + fadeInStep);
            else gain = std::max(0.f, gain - fadeOutStep);
            const float k = gain * masterVol;
            const int l = (int)std::lround((float)dst[f * 2] * k);
            const int r = (int)std::lround((float)dst[f * 2 + 1] * k);
            dst[f * 2]     = (int16_t)(l > 32767 ? 32767 : (l < -32768 ? -32768 : l));
            dst[f * 2 + 1] = (int16_t)(r > 32767 ? 32767 : (r < -32768 ? -32768 : r));
        }
        if (stopping && gain <= 0.0005f) draining = true;
        hdr[idx].lpData = (LPSTR)dst;
        hdr[idx].dwBufferLength = (DWORD)(FR * 4);
        hdr[idx].dwFlags = 0;
        if (waveOutPrepareHeader(wo, &hdr[idx], sizeof(WAVEHDR)) != MMSYSERR_NOERROR) return false;
        if (waveOutWrite(wo, &hdr[idx], sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
            waveOutUnprepareHeader(wo, &hdr[idx], sizeof(WAVEHDR));
            return false;
        }
        queued[idx] = true;
        return true;
    };

    for (int i = 0; i < NB && !draining; ++i) {
        if (!submit(i, shouldContinue())) { draining = true; break; }
    }

    for (;;) {
        WaitForSingleObject(ev, 25);
        bool shut = false;
        {
            std::lock_guard<std::mutex> lk(gMu);
            shut = gShuttingDown;
        }
        if (shut) break;
        const bool keep = shouldContinue();
        bool anyQueued = false;
        for (int i = 0; i < NB; ++i) {
            if (queued[i] && (hdr[i].dwFlags & WHDR_DONE)) {
                waveOutUnprepareHeader(wo, &hdr[i], sizeof(WAVEHDR));
                queued[i] = false;
            }
            if (!queued[i] && !draining) {
                if (!submit(i, keep)) draining = true;
            }
            if (queued[i]) anyQueued = true;
        }
        if (!anyQueued && draining) break;
    }

    waveOutReset(wo);
    for (int i = 0; i < NB; ++i) {
        if (queued[i] || (hdr[i].dwFlags & WHDR_PREPARED))
            waveOutUnprepareHeader(wo, &hdr[i], sizeof(WAVEHDR));
    }
    waveOutClose(wo);
    CloseHandle(ev);

    // Advance to the next song in sequential jukebox mode, or clear active track if finished/cancelled
    std::lock_guard<std::mutex> lk(gMu);
    if (gTrack == track && gTrackSeq == seq) {
        if (gJukebox && naturalEnd && !gShuttingDown && !gSuppressed.load(std::memory_order_relaxed)) {
            const Track nxt = nextJukeboxTrack(track);
            if (nxt != Track::None) {
                gTrack = nxt;
                ++gTrackSeq;
            } else {
                gTrack = Track::None;
                gJukebox = false;
            }
        } else {
            gTrack = Track::None;
            gJukebox = false;
        }
    }
}

void playUfoFlyby(const Clip& clip, uint64_t curFlight, float initPan) {
    const float slewNormal = 1.f - std::exp(-1.f / (0.015f * (float)clip.sampleRate));
    const float slewFadeOut = 1.f - std::exp(-1.f / (0.030f * (float)clip.sampleRate));

    WAVEFORMATEX fmt{};
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 2;
    fmt.nSamplesPerSec = (DWORD)clip.sampleRate;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = 4;
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;

    HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HWAVEOUT wo = nullptr;
    if (!ev || waveOutOpen(&wo, WAVE_MAPPER, &fmt, (DWORD_PTR)ev, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        if (ev) CloseHandle(ev);
        std::lock_guard<std::mutex> lk(gMu);
        gFailedFlightId = curFlight;                               // do not spin on a headless PC with no audio device
        gActive = false;
        return;
    }

    constexpr int NB = 4;
    constexpr size_t FR = 1024;                                    // ~23 ms at 44.1 kHz: tight pan tracking & crisp fade-out
    std::vector<int16_t> ring((size_t)NB * FR * 2, 0);
    WAVEHDR hdr[NB]{};
    bool queued[NB] = {};

    StereoMixer mx;
    mx.clip = &clip;
    mx.reset(initPan);
    {
        std::lock_guard<std::mutex> lk(gMu);
        if (gFlightId == curFlight) gRestart = false;
    }

    bool draining = false;
    auto submit = [&](int idx, float tPan, float tGain, float slewG) -> bool {
        int16_t* dst = &ring[(size_t)idx * FR * 2];
        mx.fill(dst, FR, tPan, tGain, slewNormal, slewG);
        hdr[idx].lpData = (LPSTR)dst;
        hdr[idx].dwBufferLength = (DWORD)(FR * 4);
        hdr[idx].dwFlags = 0;
        if (waveOutPrepareHeader(wo, &hdr[idx], sizeof(WAVEHDR)) != MMSYSERR_NOERROR) return false;
        if (waveOutWrite(wo, &hdr[idx], sizeof(WAVEHDR)) != MMSYSERR_NOERROR) {
            waveOutUnprepareHeader(wo, &hdr[idx], sizeof(WAVEHDR));
            return false;
        }
        queued[idx] = true;
        return true;
    };

    auto snapshot = [&](bool& shut, bool& act, bool& rst, float& tPan, float& tGain, float& rPan) {
        std::lock_guard<std::mutex> lk(gMu);
        shut = gShuttingDown;
        act = gActive && gTrack == Track::None && !gSuppressed.load(std::memory_order_relaxed);
        if (act && (GetTickCount64() - gLastTick > 350ULL)) {
            gActive = false;                                       // UI paused / covered / minimized: fade out cleanly
            act = false;
        }
        rst = gRestart;
        if (rst) { gRestart = false; curFlight = gFlightId; }
        tPan = gTargetPan;
        tGain = act ? gTargetGain : 0.f;
        rPan = gInitialPan;
    };

    for (int i = 0; i < NB; ++i) {
        bool shut = false, act = false, rst = false;
        float tPan = 1.f, tGain = 0.f, rPan = 1.f;
        snapshot(shut, act, rst, tPan, tGain, rPan);
        if (shut) { draining = true; break; }
        if (rst) mx.reset(rPan);
        if (!submit(i, tPan, tGain, act ? slewNormal : slewFadeOut)) { draining = true; break; }
    }

    for (;;) {
        WaitForSingleObject(ev, 20);
        bool shut = false, act = false, rst = false;
        float tPan = 1.f, tGain = 0.f, rPan = 1.f;
        snapshot(shut, act, rst, tPan, tGain, rPan);
        if (shut) break;
        if (rst) { mx.reset(rPan); draining = false; }
        if (act) draining = false;
        else if (mx.curGain < 0.0005f) draining = true;

        bool anyQueued = false;
        for (int i = 0; i < NB; ++i) {
            if (queued[i] && (hdr[i].dwFlags & WHDR_DONE)) {
                waveOutUnprepareHeader(wo, &hdr[i], sizeof(WAVEHDR));
                queued[i] = false;
            }
            if (!queued[i] && !draining) {
                if (!submit(i, tPan, tGain, act ? slewNormal : slewFadeOut)) draining = true;
                if (!act && mx.curGain < 0.0005f) draining = true;
            }
            if (queued[i]) anyQueued = true;
        }
        if (!anyQueued && draining) break;
    }

    waveOutReset(wo);
    for (int i = 0; i < NB; ++i) {
        if (queued[i] || (hdr[i].dwFlags & WHDR_PREPARED))
            waveOutUnprepareHeader(wo, &hdr[i], sizeof(WAVEHDR));
    }
    waveOutClose(wo);
    CloseHandle(ev);
}

void workerLoop() {
    MfRuntime mf;
    if (!mf.init()) {
        // Clear pending playback state so currentTrack() returns None (unblocking the hourly chime),
        // publish the failure, and mark the worker exited so a later playback request can cleanly retry.
        std::lock_guard<std::mutex> lk(gMu);
        const bool hadTrack = (gTrack != Track::None || gJukebox);
        if (gActive) gFailedFlightId = gFlightId;
        if (hadTrack) gFailedTrackSeq = gTrackSeq;
        gTrack = Track::None;
        gJukebox = false;
        gActive = false;
        gTargetGain = 0.f;
        gLastError = "Windows Media Foundation is unavailable";
        if (hadTrack) gPendingTrackFailure = true;
        ++gFailureSeq;
        gWorkerExited = true;
        gCv.notify_all();
        return;
    }

    Clip ufoClip;
    bool ufoTried = false;

    for (;;) {
        Track wantTrack = Track::None;
        uint64_t wantSeq = 0;
        bool wantUfo = false;
        uint64_t curFlight = 0;
        float initPan = 1.f;
        {
            std::unique_lock<std::mutex> lk(gMu);
            gCv.wait(lk, [] {
                if (gShuttingDown) return true;
                if (gSuppressed.load(std::memory_order_relaxed)) return false;
                if (gTrack != Track::None && gTrackSeq != gFailedTrackSeq) return true;
                return gActive && gFlightId != gFailedFlightId;
            });
            if (gShuttingDown) {
                gWorkerExited = true;
                return;
            }
            if (gTrack != Track::None && gTrackSeq != gFailedTrackSeq) {
                wantTrack = gTrack;
                wantSeq = gTrackSeq;
            } else if (gActive && gFlightId != gFailedFlightId) {
                wantUfo = true;
                curFlight = gFlightId;
                initPan = gInitialPan;
            }
        }

        if (wantTrack != Track::None) {
            playFullTrack(mf, wantTrack, wantSeq);
            continue;
        }
        if (wantUfo) {
            if (!ufoTried) {
                ufoTried = true;
                decodeUfoFlybyClip(mf, ufoClip);
            }
            if (ufoClip.mono.empty()) {
                std::lock_guard<std::mutex> lk(gMu);
                gFailedFlightId = curFlight;
                gActive = false;
                continue;
            }
            playUfoFlyby(ufoClip, curFlight, initPan);
        }
    }
}

// Ensures a live worker thread is running, cleanly joining and replacing any previously exited worker
// without ever joining while gMu is locked.
void ensureWorkerRunning() {
    std::thread exited;
    {
        std::lock_guard<std::mutex> lk(gMu);
        if (gShuttingDown) return;
        if (gThreadStarted && !gWorkerExited) return;
        if (gWorkerExited && gThread.joinable()) exited = std::move(gThread);
        gThreadStarted = false;
        gWorkerExited = false;
    }
    if (exited.joinable()) exited.join();
    {
        std::lock_guard<std::mutex> lk(gMu);
        if (gShuttingDown || gThreadStarted) return;
        if (gTrack == Track::None && !gActive) return;
        gThreadStarted = true;
        gWorkerExited = false;
        gThread = std::thread(workerLoop);
    }
}

struct ShutdownGuard {
    ~ShutdownGuard() { shutdown(); }
} gGuard;

} // namespace

void setEnabled(bool enabled) {
    gEnabled.store(enabled, std::memory_order_relaxed);
    if (!enabled) {
        std::lock_guard<std::mutex> lk(gMu);
        gActive = false;
        gTargetGain = 0.f;
        gTrack = Track::None;
        gJukebox = false;
    }
}

void setSuppressed(bool suppressed) {
    const bool prev = gSuppressed.exchange(suppressed, std::memory_order_relaxed);
    if (suppressed && !prev) {
        std::lock_guard<std::mutex> lk(gMu);
        gActive = false;
        gTargetGain = 0.f;
        gTrack = Track::None;
        gJukebox = false;
    }
}

void update(uint64_t flightId, float xNorm, float u) {
    if (!gEnabled.load(std::memory_order_relaxed) || gSuppressed.load(std::memory_order_relaxed)) return;
    float pan = 0.f, gain = 0.f;
    flybyParams(xNorm, u, pan, gain);

    bool wake = false;
    bool needWorker = false;
    {
        std::lock_guard<std::mutex> lk(gMu);
        if (gShuttingDown || gTrack != Track::None) return;        // let an explicitly started full-track song finish uninterrupted
        if (flightId == gFailedFlightId) return;                   // do not respawn every frame during the same failed flight
        const bool newFlight = (flightId != gFlightId);
        if (newFlight) {
            gFlightId = flightId;
            gRestart = true;
            gInitialPan = pan;
        }
        const bool wasActive = gActive;
        gTargetPan = pan;
        gTargetGain = gain;
        gLastTick = GetTickCount64();
        gActive = true;
        needWorker = !gThreadStarted || gWorkerExited;
        wake = !wasActive || newFlight;
    }
    if (needWorker) ensureWorkerRunning();
    if (wake) gCv.notify_one();
}

void stop() {
    std::lock_guard<std::mutex> lk(gMu);
    gActive = false;
    gTargetGain = 0.f;
}

Track toggleTrack(Track track) {
    if (track == Track::None) { stopTrack(); return Track::None; }
    if (!gEnabled.load(std::memory_order_relaxed) || gSuppressed.load(std::memory_order_relaxed)) return Track::None;
    Track nowPlaying = Track::None;
    bool needWorker = false;
    {
        std::lock_guard<std::mutex> lk(gMu);
        if (gShuttingDown) return Track::None;
        const bool wasJukebox = gJukebox;
        gJukebox = false;
        if (gTrack == track && !wasJukebox) {
            gTrack = Track::None;
            nowPlaying = Track::None;
        } else {
            gTrack = track;
            ++gTrackSeq;
            gActive = false;                                       // yield any active UFO fly-by cleanly
            gTargetGain = 0.f;
            nowPlaying = track;
            needWorker = !gThreadStarted || gWorkerExited;
        }
    }
    if (needWorker) ensureWorkerRunning();
    gCv.notify_one();
    return nowPlaying;
}

bool toggleJukebox() {
    if (!gEnabled.load(std::memory_order_relaxed) || gSuppressed.load(std::memory_order_relaxed)) return false;
    bool started = false;
    bool needWorker = false;
    {
        std::lock_guard<std::mutex> lk(gMu);
        if (gShuttingDown) return false;
        if (gJukebox) {
            gJukebox = false;
            gTrack = Track::None;
            started = false;
        } else {
            gJukebox = true;
            gTrack = Track::HippyShit;                             // Song 1 -> Song 2 -> stop
            ++gTrackSeq;
            gActive = false;
            gTargetGain = 0.f;
            started = true;
            needWorker = !gThreadStarted || gWorkerExited;
        }
    }
    if (needWorker) ensureWorkerRunning();
    gCv.notify_one();
    return started;
}

bool isJukebox() {
    std::lock_guard<std::mutex> lk(gMu);
    return gJukebox && gTrack != Track::None;
}

void stopTrack() {
    {
        std::lock_guard<std::mutex> lk(gMu);
        gTrack = Track::None;
        gJukebox = false;
    }
    gCv.notify_one();
}

Track currentTrack() {
    std::lock_guard<std::mutex> lk(gMu);
    return gTrack;
}

uint64_t failureSeq() {
    std::lock_guard<std::mutex> lk(gMu);
    return gFailureSeq;
}

std::string lastError() {
    std::lock_guard<std::mutex> lk(gMu);
    return gLastError;
}

bool consumeTrackFailure(std::string& outError) {
    std::lock_guard<std::mutex> lk(gMu);
    if (!gPendingTrackFailure) return false;
    gPendingTrackFailure = false;
    outError = gLastError;
    return true;
}

uint64_t initAttemptCount() {
    return gInitAttempts.load(std::memory_order_relaxed);
}

void setLibraryLoaderForTest(LibraryLoaderFn fn) {
    gLibLoader.store(fn, std::memory_order_release);
}

void shutdown() {
    gEnabled.store(false, std::memory_order_relaxed);
    std::thread toJoin;
    {
        std::lock_guard<std::mutex> lk(gMu);
        gShuttingDown = true;
        gActive = false;
        gTargetGain = 0.f;
        gTrack = Track::None;
        gJukebox = false;
        if (gThread.joinable()) toJoin = std::move(gThread);
        gThreadStarted = false;
        gWorkerExited = false;
    }
    gCv.notify_all();
    if (toJoin.joinable()) toJoin.join();
    {
        std::lock_guard<std::mutex> lk(gMu);
        gShuttingDown = false;
        gFlightId = 0;
        gFailedFlightId = ~0ULL;
        gFailedTrackSeq = ~0ULL;
        gFailureSeq = 0;
        gLastError.clear();
        gPendingTrackFailure = false;
    }
    gInitAttempts.store(0, std::memory_order_relaxed);
}

} // namespace ufoaudio
