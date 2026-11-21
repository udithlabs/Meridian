// Sends the setup's song to the speakers with the old, plain waveOut interface (winmm: every Windows has it, and it needs no extra files).
#include "song.h"
#include <windows.h>
#include <mmsystem.h>
#include <atomic>
#include "resource.h"
#include "song_engine.h"

namespace song {

namespace {

const wchar_t* kRegKey = L"Software\\Meridian\\Setup";
std::atomic<bool> gMuted{false}, gMutedLoaded{false};

struct Loaded { const uint8_t* blob = nullptr; size_t n = 0; uint64_t key = 0; songeng::Song s; std::wstring artist, title; bool ok = false; };

std::wstring widenUtf8(const std::string& u) {
    if (u.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, u.data(), (int)u.size(), nullptr, 0); std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, u.data(), (int)u.size(), &w[0], n); return w;
}
Loaded& loaded() {                                   // the song resource, read once
    static Loaded L; static bool done = false;
    if (done) return L; done = true;
    HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_SONG), RT_RCDATA);
    HGLOBAL h = r ? LoadResource(nullptr, r) : nullptr;
    L.blob = h ? (const uint8_t*)LockResource(h) : nullptr; L.n = r ? SizeofResource(nullptr, r) : 0;
    if (!L.blob || L.n < 12) return L;
    L.key = songeng::hash64(L.blob, L.n);
    if (songeng::unlock(L.blob, L.n, L.s)) { L.artist = widenUtf8(L.s.artist); L.title = widenUtf8(L.s.title); L.ok = true; }
    return L;
}
void loadMute() {
    if (gMutedLoaded.exchange(true)) return;
    DWORD v = 0, n = sizeof(v);
    if (RegGetValueW(HKEY_CURRENT_USER, kRegKey, L"Mute", RRF_RT_REG_DWORD, nullptr, &v, &n) == ERROR_SUCCESS) gMuted = v != 0;
}

// ---- the player thread
HANDLE gThread = nullptr;
std::atomic<bool> gRunning{false}, gStop{false}, gFastStop{false};

DWORD WINAPI playerProc(LPVOID) {
    Loaded& L = loaded();
    songeng::Pcm pcm;
    if (L.ok && !gFastStop && songeng::decode(L.s.mp3.data(), L.s.mp3.size(), pcm) && !gFastStop) {
        WAVEFORMATEX fmt{}; fmt.wFormatTag = WAVE_FORMAT_PCM; fmt.nChannels = (WORD)pcm.ch; fmt.nSamplesPerSec = (DWORD)pcm.rate; fmt.wBitsPerSample = 16;
        fmt.nBlockAlign = (WORD)(fmt.nChannels * 2); fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
        HANDLE ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        HWAVEOUT wo = nullptr;
        if (ev && waveOutOpen(&wo, WAVE_MAPPER, &fmt, (DWORD_PTR)ev, 0, CALLBACK_EVENT) == MMSYSERR_NOERROR) {
            constexpr int NB = 4; constexpr size_t FR = 4096;                  // four blocks of ~93 ms: quick to react to Mute, safe against a busy PC
            std::vector<int16_t> mem((size_t)NB * FR * (size_t)pcm.ch); WAVEHDR hdr[NB]{}; bool queued[NB] = {};
            songeng::Mixer mx; mx.pcm = &pcm;
            auto submit = [&](int i) {
                int16_t* p = &mem[(size_t)i * FR * (size_t)pcm.ch];
                const bool more = mx.fill(p, FR);
                hdr[i].lpData = (LPSTR)p; hdr[i].dwBufferLength = (DWORD)(FR * (size_t)pcm.ch * 2); hdr[i].dwFlags = 0;
                if (waveOutPrepareHeader(wo, &hdr[i], sizeof(WAVEHDR)) != MMSYSERR_NOERROR) return false;
                if (waveOutWrite(wo, &hdr[i], sizeof(WAVEHDR)) != MMSYSERR_NOERROR) { waveOutUnprepareHeader(wo, &hdr[i], sizeof(WAVEHDR)); return false; }
                queued[i] = true; return more;
            };
            loadMute();
            bool alive = true, last = false, wasMuted = gMuted;
            mx.gain = 0.f; mx.to(wasMuted ? 0.f : 1.f, 0.5f);                  // starts silent and fades in (or stays silent, if it was muted earlier)
            for (int i = 0; i < NB && alive && !last && !gFastStop; ++i) { bool more = submit(i); if (!more) last = true; }
            while (alive && !gFastStop) {
                WaitForSingleObject(ev, 40);
                if (gFastStop) break;
                if (gStop && !mx.stopping) mx.fadeOutAndStop(0.7f);
                if (!gStop && gMuted != wasMuted) { wasMuted = gMuted; mx.to(gMuted ? 0.f : 1.f, 0.35f); }
                bool anyQueued = false;
                for (int i = 0; i < NB; ++i) {
                    if (!queued[i]) continue;
                    if (hdr[i].dwFlags & WHDR_DONE) {
                        waveOutUnprepareHeader(wo, &hdr[i], sizeof(WAVEHDR)); queued[i] = false;
                        if (!last) { bool more = submit(i); if (!more) last = true; }
                    }
                    if (queued[i]) anyQueued = true;
                }
                if (!anyQueued) alive = false;                                  // everything has been played
            }
            waveOutReset(wo);
            for (int i = 0; i < NB; ++i) if (queued[i] || (hdr[i].dwFlags & WHDR_PREPARED)) waveOutUnprepareHeader(wo, &hdr[i], sizeof(WAVEHDR));
            waveOutClose(wo);
        }
        if (ev) CloseHandle(ev);
    }
    gRunning = false;
    return 0;
}

} // namespace

uint64_t key() { return loaded().key; }
const std::wstring& artist() { return loaded().artist; }
const std::wstring& title() { return loaded().title; }

bool muted() { loadMute(); return gMuted; }
void setMuted(bool m) {
    loadMute(); gMuted = m;
    HKEY k; if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
        DWORD v = m ? 1 : 0; RegSetValueExW(k, L"Mute", 0, REG_DWORD, (const BYTE*)&v, sizeof(v)); RegCloseKey(k);
    }
}

void start() {
    if (gRunning && !gStop) return;                                           // already playing (one that is still fading out, after an error, is waited for and started again)
    if (gThread) { WaitForSingleObject(gThread, INFINITE); CloseHandle(gThread); gThread = nullptr; }
    if (!loaded().ok) return;
    loadMute();
    gStop = false; gFastStop = false; gRunning = true;
    gThread = CreateThread(nullptr, 0, playerProc, nullptr, 0, nullptr);
    if (!gThread) gRunning = false;
}
void stop() { gStop = true; }
void shutdown() {
    gStop = true;
    gFastStop = true;
    if (gThread) { WaitForSingleObject(gThread, INFINITE); CloseHandle(gThread); gThread = nullptr; }
}

} // namespace song
