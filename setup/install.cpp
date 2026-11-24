#include "install.h"
#include "pathcheck.h"
#include <shlobj.h>
#include <shobjidl.h>
#include <knownfolders.h>
#include <cwchar>
#include <cstdio>
#include <vector>
#include <algorithm>

namespace inst {

// ------------------------------------------------------------------ the places and names (the same ones the 2.6.x setup and Install-ScreenSaver.bat use)
static const wchar_t* UNINSTALL_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Meridian";
static const wchar_t* RUN_KEY       = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* DESKTOP_KEY   = L"Control Panel\\Desktop";
static const wchar_t* BACKUP_KEY    = L"Software\\Meridian\\ScreenSaverBackup";
static const wchar_t* APP_KEY       = L"Software\\Meridian";
static const wchar_t* SAVER_KEY     = L"Software\\Meridian\\Saver";      // SleepAfterMinutes lives here; Meridian reads it when it runs as the screen saver
static const char* APP_VERSION      = "3.0.0";

// ------------------------------------------------------------------ small helpers
static std::string narrow(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    if (n <= 0) return "";
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
std::wstring widen(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    if (n <= 0) return L"";
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}
bool pathsEqual(const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) == 0; }
bool fileExists(const std::wstring& p) { DWORD a = GetFileAttributesW(p.c_str()); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }
bool dirExists(const std::wstring& p) { DWORD a = GetFileAttributesW(p.c_str()); return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY); }
std::wstring exeName(const std::wstring& p) { size_t i = p.find_last_of(L"\\/"); return i == std::wstring::npos ? p : p.substr(i + 1); }
std::wstring parentDir(const std::wstring& p) { size_t i = p.find_last_of(L"\\/"); return i == std::wstring::npos ? L"" : p.substr(0, i); }
static std::wstring trimSlash(std::wstring s) { while (s.size() > 3 && (s.back() == L'\\' || s.back() == L'/')) s.pop_back(); return s; }
static std::wstring envVar(const wchar_t* n) {
    wchar_t buf[MAX_PATH * 2]; DWORD k = GetEnvironmentVariableW(n, buf, MAX_PATH * 2);
    return k > 0 && k < MAX_PATH * 2 ? std::wstring(buf, k) : L"";
}
std::wstring defaultDir() {
    std::wstring b = envVar(L"LOCALAPPDATA");
    if (b.empty()) b = envVar(L"USERPROFILE") + L"\\AppData\\Local";
    return b + L"\\Programs\\Meridian";
}
std::wstring settingsFolder(bool local) {
    std::wstring b = envVar(local ? L"LOCALAPPDATA" : L"APPDATA");
    return b.empty() ? L"" : b + L"\\Meridian";
}
static std::wstring knownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr; std::wstring r;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p)) && p) r = p;
    if (p) CoTaskMemFree(p);
    return r;
}
static std::wstring lastErrorText(DWORD e) {
    switch (e) {
        case ERROR_ACCESS_DENIED: return L"Windows did not allow it (access denied)";
        case ERROR_SHARING_VIOLATION: return L"another program is using the file";
        case ERROR_DISK_FULL: case ERROR_HANDLE_DISK_FULL: return L"the disk is full";
        case ERROR_PATH_NOT_FOUND: return L"the folder could not be created";
        default: { wchar_t b[48]; swprintf(b, 48, L"Windows error %lu", (unsigned long)e); return b; }
    }
}

// ------------------------------------------------------------------ registry
static bool regGet(const wchar_t* key, const wchar_t* name, std::wstring& out, DWORD* typeOut = nullptr) {
    HKEY k; if (RegOpenKeyExW(HKEY_CURRENT_USER, key, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return false;
    wchar_t buf[2048]; DWORD sz = sizeof(buf) - 2, type = 0;
    LONG r = RegQueryValueExW(k, name, nullptr, &type, (LPBYTE)buf, &sz);
    RegCloseKey(k);
    if (r != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) return false;
    buf[sz / 2] = 0; out = buf;
    if (typeOut) *typeOut = type;                                  // REG_SZ or REG_EXPAND_SZ: the text is returned as stored (a %SystemRoot% in it is not expanded)
    return true;
}
static bool regSet(const wchar_t* key, const wchar_t* name, const std::wstring& v, DWORD type = REG_SZ) {
    HKEY k; if (RegCreateKeyExW(HKEY_CURRENT_USER, key, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) return false;
    LONG r = RegSetValueExW(k, name, 0, type == REG_EXPAND_SZ ? REG_EXPAND_SZ : REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * 2));
    RegCloseKey(k);
    return r == ERROR_SUCCESS;
}
static bool regGetDword(const wchar_t* key, const wchar_t* name, DWORD& out) {
    HKEY k; if (RegOpenKeyExW(HKEY_CURRENT_USER, key, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS) return false;
    DWORD v = 0, sz = 4, type = 0;
    LONG r = RegQueryValueExW(k, name, nullptr, &type, (LPBYTE)&v, &sz);
    RegCloseKey(k);
    if (r != ERROR_SUCCESS || type != REG_DWORD) return false;
    out = v; return true;
}
static bool regSetDword(const wchar_t* key, const wchar_t* name, DWORD v) {
    HKEY k; if (RegCreateKeyExW(HKEY_CURRENT_USER, key, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) return false;
    LONG r = RegSetValueExW(k, name, 0, REG_DWORD, (const BYTE*)&v, 4);
    RegCloseKey(k);
    return r == ERROR_SUCCESS;
}
static void regDelValue(const wchar_t* key, const wchar_t* name) {
    HKEY k; if (RegOpenKeyExW(HKEY_CURRENT_USER, key, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
    RegDeleteValueW(k, name); RegCloseKey(k);
}
static void regDelTree(const wchar_t* key) { RegDeleteTreeW(HKEY_CURRENT_USER, key); }

// ------------------------------------------------------------------ files
static void clearReadOnly(const std::wstring& p) { SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL); }
static bool makeDirs(const std::wstring& d) {
    if (dirExists(d)) return true;
    int r = SHCreateDirectoryExW(nullptr, d.c_str(), nullptr);
    return r == ERROR_SUCCESS || r == ERROR_ALREADY_EXISTS || dirExists(d);
}
static bool deleteFile(const std::wstring& p) {
    if (!fileExists(p)) return true;
    clearReadOnly(p);
    if (DeleteFileW(p.c_str())) return true;
    Sleep(150);
    return DeleteFileW(p.c_str()) != 0;
}
static bool readWhole(const std::wstring& p, std::vector<uint8_t>& out) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz{};
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 || sz.QuadPart > (256LL << 20)) { CloseHandle(h); return false; }
    out.assign((size_t)sz.QuadPart, 0); size_t done = 0; bool ok = true;
    while (done < out.size()) { DWORD g = 0; if (!ReadFile(h, out.data() + done, (DWORD)std::min<size_t>(out.size() - done, 1 << 20), &g, nullptr) || !g) { ok = false; break; } done += g; }
    CloseHandle(h);
    return ok;
}
static void deleteTree(const std::wstring& dir) {
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::wstring n = fd.cFileName;
            if (n == L"." || n == L"..") continue;
            std::wstring p = dir + L"\\" + n;
            const bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
            const bool isReparse = (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
            if (isDir && !isReparse) deleteTree(p);
            else if (isDir && isReparse) RemoveDirectoryW(p.c_str());
            else deleteFile(p);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(dir.c_str());
}
static std::wstring writeNewFile(const std::wstring& path, const void* data, size_t n) {
    if (!makeDirs(parentDir(path))) return L"I could not create the folder " + parentDir(path) + L".";
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return L"Could not stage " + exeName(path) + L": " + lastErrorText(GetLastError()) + L".";
    size_t done = 0; bool ok = true; DWORD err = 0;
    while (done < n) {
        DWORD w = 0;
        if (!WriteFile(h, (const uint8_t*)data + done, (DWORD)std::min<size_t>(n - done, 1 << 20), &w, nullptr) || !w) { ok = false; err = GetLastError(); break; }
        done += w;
    }
    if (ok && !FlushFileBuffers(h)) { ok = false; err = GetLastError(); }
    CloseHandle(h);
    if (!ok) { DeleteFileW(path.c_str()); return L"Could not finish staging " + exeName(path) + L": " + lastErrorText(err) + L"."; }
    return L"";
}

static void removeOldLeftovers(const std::wstring& dir) {
    // Only remove legacy single-file swap leftovers; never wildcard-delete `.meridian-*` directories
    // (owned transaction staging/backup folders are cleaned exclusively via `.meridian-tx.log`).
    for (const wchar_t* n : {L"Meridian.exe.old", L"Meridian.scr.old", L"uninstall.exe.old", L"Meridian.exe.new", L"Meridian.scr.new", L"uninstall.exe.new"})
        DeleteFileW((dir + L"\\" + n).c_str());
}

namespace {

constexpr const wchar_t* TX_LOG_NAME = L".meridian-tx.log";

struct TxEntry {
    std::wstring rel;
    bool hadOld = false;
};

struct TxRecord {
    std::wstring stageName;   // single folder name inside `dir` (".meridian-stage-...") or empty
    std::wstring bakName;     // single folder name inside `dir` (".meridian-backup-...")
    std::wstring state;       // COMMITTING | ROLLING_BACK | COMMITTED
    std::vector<TxEntry> entries;
};

static bool isOwnedTxFolderName(const std::wstring& name, const wchar_t* prefix) {
    if (name.empty() || name.find(L'\\') != std::wstring::npos || name.find(L'/') != std::wstring::npos ||
        name.find(L':') != std::wstring::npos || name.find(L"..") != std::wstring::npos) return false;
    const size_t plen = wcslen(prefix);
    return name.size() > plen && _wcsnicmp(name.c_str(), prefix, plen) == 0;
}

static bool writeTxRecord(const std::wstring& dir, const TxRecord& rec) {
    std::string body = "MERIDIAN_TX_V1\n";
    body += "STAGE " + narrow(rec.stageName) + "\n";
    body += "BACKUP " + narrow(rec.bakName) + "\n";
    body += "STATE " + narrow(rec.state) + "\n";
    for (const auto& e : rec.entries) {
        body += std::string("FILE ") + (e.hadOld ? "1 " : "0 ") + narrow(e.rel) + "\n";
    }
    const std::wstring txPath = dir + L"\\" + TX_LOG_NAME;
    const std::wstring tmpPath = txPath + L".tmp";
    HANDLE h = CreateFileW(tmpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    size_t done = 0; bool ok = true;
    while (done < body.size()) {
        DWORD w = 0;
        if (!WriteFile(h, body.data() + done, (DWORD)(body.size() - done), &w, nullptr) || !w) { ok = false; break; }
        done += w;
    }
    if (ok && !FlushFileBuffers(h)) ok = false;
    CloseHandle(h);
    if (!ok) { DeleteFileW(tmpPath.c_str()); return false; }
    if (!MoveFileExW(tmpPath.c_str(), txPath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(tmpPath.c_str());
        return false;
    }
    return true;
}

static bool readTxRecord(const std::wstring& dir, TxRecord& out) {
    const std::wstring txPath = dir + L"\\" + TX_LOG_NAME;
    if (!fileExists(txPath)) return false;
    std::vector<uint8_t> raw;
    if (!readWhole(txPath, raw)) return false;
    std::string text(raw.begin(), raw.end());
    out = TxRecord{};
    size_t pos = 0; bool headerOk = false;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        std::string line = (nl == std::string::npos) ? text.substr(pos) : text.substr(pos, nl - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        pos = (nl == std::string::npos) ? text.size() : nl + 1;
        if (line.empty()) continue;
        if (!headerOk) { if (line != "MERIDIAN_TX_V1") return false; headerOk = true; continue; }
        if (line.rfind("STAGE ", 0) == 0) out.stageName = widen(line.substr(6));
        else if (line.rfind("BACKUP ", 0) == 0) out.bakName = widen(line.substr(7));
        else if (line.rfind("STATE ", 0) == 0) out.state = widen(line.substr(6));
        else if (line.rfind("FILE ", 0) == 0 && line.size() > 7) {
            bool hadOld = (line[5] == '1');
            if ((line[5] == '0' || line[5] == '1') && line[6] == ' ') {
                std::wstring rel = widen(line.substr(7));
                if (!rel.empty() && rel[0] != L'\\' && rel[0] != L'/' && rel.find(L"..") == std::wstring::npos && rel.find(L':') == std::wstring::npos) {
                    out.entries.push_back({rel, hadOld});
                }
            }
        }
    }
    return headerOk;
}

static void cleanupOwnedTxFolders(const std::wstring& dir, const TxRecord& rec, bool cleanBackup, bool cleanStage) {
    if (cleanBackup && isOwnedTxFolderName(rec.bakName, L".meridian-backup-")) {
        deleteTree(dir + L"\\" + rec.bakName);
    }
    if (cleanStage && isOwnedTxFolderName(rec.stageName, L".meridian-stage-")) {
        deleteTree(dir + L"\\" + rec.stageName);
    }
}

static std::wstring joinFileList(const std::vector<std::wstring>& files) {
    std::wstring out;
    for (size_t i = 0; i < files.size(); ++i) {
        if (i) out += L", ";
        out += files[i];
    }
    return out;
}

static bool rollbackTxRecord(const std::wstring& dir, TxRecord& rec, std::vector<std::wstring>& unresolvedOut) {
    unresolvedOut.clear();
    rec.state = L"ROLLING_BACK";
    writeTxRecord(dir, rec);

    const std::wstring bakDir = isOwnedTxFolderName(rec.bakName, L".meridian-backup-") ? (dir + L"\\" + rec.bakName) : L"";
    std::vector<TxEntry> remainingRev;
    for (size_t i = rec.entries.size(); i-- > 0;) {
        const TxEntry& e = rec.entries[i];
        const std::wstring dst = dir + L"\\" + e.rel;
        if (e.hadOld) {
            if (bakDir.empty()) continue;
            const std::wstring bak = bakDir + L"\\" + e.rel;
            if (!fileExists(bak)) continue;   // never moved out, or already restored in a prior pass
            makeDirs(parentDir(dst));
            SetFileAttributesW(dst.c_str(), FILE_ATTRIBUTE_NORMAL);
            SetFileAttributesW(bak.c_str(), FILE_ATTRIBUTE_NORMAL);
            bool restored = false;
            for (int tries = 0; tries < 3 && !restored; ++tries) {
                if (MoveFileExW(bak.c_str(), dst.c_str(), MOVEFILE_REPLACE_EXISTING)) { restored = true; break; }
                DWORD err = GetLastError();
                if (err != ERROR_SHARING_VIOLATION && err != ERROR_ACCESS_DENIED) break;
                Sleep(80);
            }
            if (!restored) {
                remainingRev.push_back(e);
                unresolvedOut.push_back(e.rel);
            }
        } else {
            if (fileExists(dst)) {
                if (!deleteFile(dst) && fileExists(dst)) {
                    remainingRev.push_back(e);
                    unresolvedOut.push_back(e.rel);
                }
            }
        }
    }

    if (unresolvedOut.empty()) {
        cleanupOwnedTxFolders(dir, rec, true, true);
        deleteFile(dir + L"\\" + TX_LOG_NAME);
        return true;
    }

    // Preserve unresolved backup directory and rewrite journal with only the remaining files so a later retry recovers them cleanly.
    std::reverse(remainingRev.begin(), remainingRev.end());
    std::reverse(unresolvedOut.begin(), unresolvedOut.end());
    rec.entries = remainingRev;
    rec.state = L"ROLLING_BACK";
    writeTxRecord(dir, rec);
    return false;
}

} // namespace

void cleanupOwnedTxAt(const std::wstring& dirIn) {
    const std::wstring dir = trimSlash(dirIn);
    if (dir.empty() || !dirExists(dir)) return;
    TxRecord rec;
    if (readTxRecord(dir, rec)) {
        cleanupOwnedTxFolders(dir, rec, true, true);
        deleteFile(dir + L"\\" + TX_LOG_NAME);
    }
    deleteFile(dir + L"\\" + TX_LOG_NAME + L".tmp");
}

std::wstring recoverPendingTransaction(const std::wstring& dirIn) {
    const std::wstring dir = trimSlash(dirIn);
    if (dir.empty() || !dirExists(dir)) return L"";
    TxRecord rec;
    if (!readTxRecord(dir, rec)) return L"";
    if (rec.state == L"COMMITTED") {
        cleanupOwnedTxFolders(dir, rec, true, true);
        deleteFile(dir + L"\\" + TX_LOG_NAME);
        return L"";
    }
    std::vector<std::wstring> unresolved;
    if (rollbackTxRecord(dir, rec, unresolved)) return L"";
    return L"A previous installation was interrupted and could not finish restoring: " + joinFileList(unresolved) +
           L". Your original files are preserved in " + rec.bakName + L". Close any program using those files and run Setup again to finish recovery.";
}

std::wstring commitStagedFiles(const std::wstring& dirIn, const std::wstring& stageDirIn, const std::vector<std::wstring>& relFiles) {
    const std::wstring dir = trimSlash(dirIn);
    const std::wstring stageDir = trimSlash(stageDirIn);
    // Always recover any interrupted transaction BEFORE staging/swapping new files so backups are never lost.
    std::wstring pendingErr = recoverPendingTransaction(dir);
    if (!pendingErr.empty()) return pendingErr;

    const std::wstring bakName = L".meridian-backup-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount());
    const std::wstring bakDir = dir + L"\\" + bakName;
    TxRecord rec;
    if (pathsEqual(trimSlash(parentDir(stageDir)), dir) && isOwnedTxFolderName(exeName(stageDir), L".meridian-stage-")) {
        rec.stageName = exeName(stageDir);
    }
    rec.bakName = bakName;
    rec.state = L"COMMITTING";
    rec.entries.reserve(relFiles.size());
    if (!writeTxRecord(dir, rec)) {
        return L"Could not write the installation transaction record inside " + dir + L". Your previous installation was left untouched.";
    }

    auto failWithRollback = [&](const std::wstring& leadMsg) -> std::wstring {
        std::vector<std::wstring> unresolved;
        if (rollbackTxRecord(dir, rec, unresolved)) {
            return leadMsg + L" Your previous installation was left untouched.";
        }
        return leadMsg + L" Rollback could not restore locked file(s): " + joinFileList(unresolved) +
               L". Original copies were preserved in " + rec.bakName + L"; close any program using them and run Setup again to complete recovery.";
    };

    for (const std::wstring& rel : relFiles) {
        const std::wstring staged = stageDir + L"\\" + rel;
        const std::wstring target = dir + L"\\" + rel;
        const std::wstring backup = bakDir + L"\\" + rel;
        if (!makeDirs(parentDir(target))) {
            return failWithRollback(L"I could not create the folder " + parentDir(target) + L".");
        }
        const bool hadOld = fileExists(target);
        rec.entries.push_back({rel, hadOld});
        if (!writeTxRecord(dir, rec)) {
            rec.entries.pop_back();
            return failWithRollback(L"Could not update the installation transaction record for " + exeName(target) + L".");
        }
        if (hadOld) {
            if (!makeDirs(parentDir(backup))) {
                return failWithRollback(L"Could not prepare backup for " + exeName(target) + L".");
            }
            SetFileAttributesW(target.c_str(), FILE_ATTRIBUTE_NORMAL);
            bool moved = false; DWORD err = 0;
            for (int tries = 0; tries < 3 && !moved; ++tries) {
                if (MoveFileExW(target.c_str(), backup.c_str(), MOVEFILE_REPLACE_EXISTING)) { moved = true; break; }
                err = GetLastError();
                if (err != ERROR_SHARING_VIOLATION && err != ERROR_ACCESS_DENIED) break;
                Sleep(80);
            }
            if (!moved) {
                return failWithRollback(L"Could not replace " + exeName(target) + L": " + lastErrorText(err) + L". Close any program using it and try again.");
            }
        }
        if (!MoveFileExW(staged.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            DWORD err = GetLastError();
            return failWithRollback(L"Could not put " + exeName(target) + L" in place: " + lastErrorText(err) + L".");
        }
    }

    rec.state = L"COMMITTED";
    writeTxRecord(dir, rec);
    cleanupOwnedTxFolders(dir, rec, true, false);
    deleteFile(dir + L"\\" + TX_LOG_NAME);
    return L"";
}
static std::wstring winPath(const std::string& rel) { std::wstring w = widen(rel); for (auto& c : w) if (c == L'/') c = L'\\'; return w; }

// ------------------------------------------------------------------ Meridian itself
bool isMeridianRunning() { return FindWindowW(L"MeridianWnd", nullptr) != nullptr; }

// Asks a running Meridian to close, the same as its close button (it saves first), and waits for it to be gone.
static bool closeMeridian() {
    HWND w = FindWindowW(L"MeridianWnd", nullptr);
    if (!w) return true;
    DWORD pid = 0; GetWindowThreadProcessId(w, &pid);
    HANDLE proc = pid ? OpenProcess(SYNCHRONIZE, FALSE, pid) : nullptr;
    PostMessageW(w, WM_CLOSE, 0, 0);
    bool gone = false;
    for (int i = 0; i < 40 && !gone; ++i) { Sleep(250); gone = FindWindowW(L"MeridianWnd", nullptr) == nullptr; }
    if (gone && proc) {
        if (WaitForSingleObject(proc, 6000) != WAIT_OBJECT_0) gone = false;
    }
    if (proc) CloseHandle(proc);
    if (gone) Sleep(200);
    return gone;
}

// ------------------------------------------------------------------ shortcuts
static std::wstring linkPath(bool desktop) {
    std::wstring base = knownFolder(desktop ? FOLDERID_Desktop : FOLDERID_Programs);
    return base.empty() ? L"" : base + L"\\Meridian.lnk";
}
static bool makeLink(const std::wstring& lnk, const std::wstring& target, const std::wstring& workDir) {
    IShellLinkW* sl = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&sl)) || !sl) return false;
    sl->SetPath(target.c_str()); sl->SetWorkingDirectory(workDir.c_str()); sl->SetIconLocation(target.c_str(), 0);
    sl->SetDescription(L"Meridian - always-on sky clock");
    IPersistFile* pf = nullptr; bool ok = false;
    if (SUCCEEDED(sl->QueryInterface(IID_IPersistFile, (void**)&pf)) && pf) { ok = SUCCEEDED(pf->Save(lnk.c_str(), TRUE)); pf->Release(); }
    sl->Release();
    return ok;
}
static std::wstring linkTarget(const std::wstring& lnk) {
    std::wstring out; IShellLinkW* sl = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&sl)) || !sl) return out;
    IPersistFile* pf = nullptr;
    if (SUCCEEDED(sl->QueryInterface(IID_IPersistFile, (void**)&pf)) && pf) {
        if (SUCCEEDED(pf->Load(lnk.c_str(), STGM_READ))) { wchar_t b[MAX_PATH * 2]; if (SUCCEEDED(sl->GetPath(b, MAX_PATH * 2, nullptr, SLGP_RAWPATH))) out = b; }
        pf->Release();
    }
    sl->Release();
    return out;
}
// removes the shortcut only when it really is ours
static void removeLinkIfOurs(bool desktop, const std::wstring& exe) {
    std::wstring l = linkPath(desktop);
    if (!l.empty() && fileExists(l) && pathsEqual(linkTarget(l), exe)) DeleteFileW(l.c_str());
}

// ------------------------------------------------------------------ screen saver (the same steps as Install-ScreenSaver.bat / Remove-ScreenSaver.bat)
static void refreshScreenSaver() {
    wchar_t sysDir[MAX_PATH];
    const UINT sn = GetSystemDirectoryW(sysDir, MAX_PATH);
    if (sn == 0 || sn >= MAX_PATH) return;
    const std::wstring app = std::wstring(sysDir, sn) + L"\\rundll32.exe";
    wchar_t cmd[] = L"rundll32.exe user32.dll,UpdatePerUserSystemParameters 1, True";
    STARTUPINFOW si{}; si.cb = sizeof(si); si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (CreateProcessW(app.c_str(), cmd, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 10000);
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    }
}
static void setScreenSaver(const std::wstring& dir, int minutes) {
    std::wstring scr = dir + L"\\Meridian.scr", cur; DWORD curType = REG_SZ;
    regGet(DESKTOP_KEY, L"SCRNSAVE.EXE", cur, &curType);
    if (_wcsicmp(exeName(cur).c_str(), L"Meridian.scr") != 0) {                       // an earlier Meridian setting is not "what was there before"
        std::wstring act, wait; regGet(DESKTOP_KEY, L"ScreenSaveActive", act); regGet(DESKTOP_KEY, L"ScreenSaveTimeOut", wait);
        regDelTree(BACKUP_KEY);
        regSet(BACKUP_KEY, L"Saved", L"1");
        if (!cur.empty()) { regSet(BACKUP_KEY, L"Scr", cur); regSet(BACKUP_KEY, L"ScrType", curType == REG_EXPAND_SZ ? L"REG_EXPAND_SZ" : L"REG_SZ"); }   // (a saver stored with %SystemRoot% goes back the same way)
        if (!act.empty()) regSet(BACKUP_KEY, L"Active", act);
        if (!wait.empty()) regSet(BACKUP_KEY, L"Wait", wait);
    }
    regSet(DESKTOP_KEY, L"SCRNSAVE.EXE", scr);
    regSet(DESKTOP_KEY, L"ScreenSaveActive", L"1");
    regSet(DESKTOP_KEY, L"ScreenSaveTimeOut", std::to_wstring(std::max(kAwayMin, std::min(kAwayMax, minutes)) * 60));
    refreshScreenSaver();
}
// Puts the earlier screen saver back, but only while Windows still points at this copy of Meridian.scr.
static void restoreScreenSaver(const std::wstring& dir) {
    std::wstring cur; regGet(DESKTOP_KEY, L"SCRNSAVE.EXE", cur);
    if (pathsEqual(cur, dir + L"\\Meridian.scr")) {
        std::wstring saved, scr, scrType, act, wait;
        regGet(BACKUP_KEY, L"Saved", saved);
        if (saved == L"1") {
            regGet(BACKUP_KEY, L"Scr", scr); regGet(BACKUP_KEY, L"ScrType", scrType); regGet(BACKUP_KEY, L"Active", act); regGet(BACKUP_KEY, L"Wait", wait);
            if (!scr.empty()) regSet(DESKTOP_KEY, L"SCRNSAVE.EXE", scr, scrType == L"REG_EXPAND_SZ" ? REG_EXPAND_SZ : REG_SZ); else regDelValue(DESKTOP_KEY, L"SCRNSAVE.EXE");
            regSet(DESKTOP_KEY, L"ScreenSaveActive", act.empty() ? L"0" : act);
            if (!wait.empty()) regSet(DESKTOP_KEY, L"ScreenSaveTimeOut", wait);
        } else {
            regDelValue(DESKTOP_KEY, L"SCRNSAVE.EXE");
            regSet(DESKTOP_KEY, L"ScreenSaveActive", L"0");
        }
        refreshScreenSaver();
        regDelTree(BACKUP_KEY);
    }
}

// ------------------------------------------------------------------ what is there now
Existing detect() {
    Existing x;
    std::wstring dir;
    if (!regGet(UNINSTALL_KEY, L"InstallLocation", dir) || dir.empty()) regGet(APP_KEY, L"InstallDir", dir);
    dir = trimSlash(dir);
    if (dir.empty() || !fileExists(dir + L"\\Meridian.exe")) return x;
    x.found = true; x.dir = dir;
    regGet(UNINSTALL_KEY, L"DisplayVersion", x.version);
    std::wstring run; regGet(RUN_KEY, L"Meridian", run);
    x.runAtLogon = pathsEqual(run, L"\"" + dir + L"\\Meridian.exe\"");
    std::wstring scr, wait; regGet(DESKTOP_KEY, L"SCRNSAVE.EXE", scr);
    if (pathsEqual(scr, dir + L"\\Meridian.scr")) {
        x.saver = true;
        regGet(DESKTOP_KEY, L"ScreenSaveTimeOut", wait);
        int m = _wtoi(wait.c_str()) / 60;
        x.saverMinutes = std::max(kAwayMin, std::min(kAwayMax, m ? m : 10));
        DWORD sl = 0;
        if (regGetDword(SAVER_KEY, L"SleepAfterMinutes", sl) && sl > 0) { x.sleep = true; x.sleepMinutes = std::max(kSleepMin, std::min(kSleepMax, (int)sl)); }
    }
    std::wstring sm = linkPath(false), dk = linkPath(true);
    x.startMenu = !sm.empty() && fileExists(sm);
    x.desktop = !dk.empty() && fileExists(dk);
    x.gta = dirExists(dir + L"\\themes\\gta6");
    x.halloween = dirExists(dir + L"\\themes\\halloween");
    return x;
}

bool looksInstalled(const std::wstring& dirIn) {
    const std::wstring dir = trimSlash(dirIn);
    if (dir.empty()) return false;
    if (fileExists(dir + L"\\Meridian.exe") || fileExists(dir + L"\\Meridian.scr")) return true;
    std::wstring rec;
    if (!regGet(UNINSTALL_KEY, L"InstallLocation", rec) || rec.empty()) regGet(APP_KEY, L"InstallDir", rec);
    return !rec.empty() && pathsEqual(trimSlash(rec), dir);
}

// ------------------------------------------------------------------ install
struct CoGuard { bool on; CoGuard() { on = SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)); } ~CoGuard() { if (on) CoUninitialize(); } };

static bool included(const PayloadEntry& e, const Options& o) { return e.group == 0 || (e.group == 1 && o.gta) || (e.group == 2 && o.halloween); }

static void removeGroup(const Payload& pl, int group, const std::wstring& dir) {
    std::vector<std::wstring> dirs;
    for (auto& e : pl.e) if (e.group == group) { std::wstring p = dir + L"\\" + winPath(e.path); deleteFile(p); dirs.push_back(parentDir(p)); }
    std::sort(dirs.begin(), dirs.end(), [](const std::wstring& a, const std::wstring& b) { return a.size() > b.size(); });
    for (auto& d : dirs) { std::wstring p = d; while (p.size() > dir.size() + 1) { if (!RemoveDirectoryW(p.c_str())) break; p = parentDir(p); } }
}

static void retireOldInstall(const Payload& pl, const std::wstring& oldDir) {
    if (oldDir.empty() || !dirExists(oldDir)) return;
    std::vector<std::wstring> dirs;
    for (auto& e : pl.e) {
        std::wstring p = oldDir + L"\\" + winPath(e.path);
        deleteFile(p);
        dirs.push_back(parentDir(p));
    }
    deleteFile(oldDir + L"\\Meridian.scr");
    deleteFile(oldDir + L"\\uninstall.exe");
    cleanupOwnedTxAt(oldDir);
    removeOldLeftovers(oldDir);
    std::sort(dirs.begin(), dirs.end(), [](const std::wstring& a, const std::wstring& b) { return a.size() > b.size(); });
    for (auto& d : dirs) { std::wstring p = d; while (p.size() > oldDir.size()) { if (!RemoveDirectoryW(p.c_str())) break; p = parentDir(p); } }
    RemoveDirectoryW((oldDir + L"\\themes").c_str());
    RemoveDirectoryW(oldDir.c_str());
}

std::wstring registerAndRetireMove(const Payload& pl, const std::wstring& dirIn, const std::wstring& prevDirIn, DWORD estSizeKb, const RegOps* ops) {
    const std::wstring dir = trimSlash(dirIn);
    const std::wstring prevDir = trimSlash(prevDirIn);
    const std::wstring exe = dir + L"\\Meridian.exe";
    auto setS = [&](const wchar_t* key, const wchar_t* name, const std::wstring& v, DWORD type = REG_SZ) -> bool {
        if (ops && ops->setStr) return ops->setStr(key, name, v, type);
        return regSet(key, name, v, type);
    };
    auto setD = [&](const wchar_t* key, const wchar_t* name, DWORD v) -> bool {
        if (ops && ops->setDword) return ops->setDword(key, name, v);
        return regSetDword(key, name, v);
    };

    const bool ok =
        setS(APP_KEY, L"InstallDir", dir) &&
        setS(UNINSTALL_KEY, L"DisplayName", L"Meridian") &&
        setS(UNINSTALL_KEY, L"DisplayVersion", widen(APP_VERSION)) &&
        setS(UNINSTALL_KEY, L"Publisher", L"Meridian") &&
        setS(UNINSTALL_KEY, L"DisplayIcon", exe) &&
        setS(UNINSTALL_KEY, L"InstallLocation", dir) &&
        setS(UNINSTALL_KEY, L"UninstallString", L"\"" + dir + L"\\uninstall.exe\" /uninstall") &&
        setS(UNINSTALL_KEY, L"QuietUninstallString", L"\"" + dir + L"\\uninstall.exe\" /uninstall /S") &&
        setS(UNINSTALL_KEY, L"Comments", L"Meridian - Sonnet Edition") &&
        setD(UNINSTALL_KEY, L"NoModify", 1) &&
        setD(UNINSTALL_KEY, L"NoRepair", 1) &&
        setD(UNINSTALL_KEY, L"EstimatedSize", estSizeKb);

    if (!ok) {
        if (!prevDir.empty() && !pathsEqual(prevDir, dir)) {
            return L"Could not update Windows registration for the new folder (" + dir + L"). Your previous installation in " + prevDir + L" was kept untouched.";
        }
        return L"Files were placed in " + dir + L", but Windows denied writing the app registration entries. Check your permissions and try again.";
    }

    // Only retire the old installation folder AFTER required Windows registration succeeds!
    if (!prevDir.empty() && !pathsEqual(prevDir, dir)) {
        retireOldInstall(pl, prevDir);
    }
    return L"";
}

std::wstring install(const Payload& pl, const Options& o, const Progress& pr, bool* stillRunning, std::vector<std::wstring>* warnings) {
    if (stillRunning) *stillRunning = false;
    if (warnings) warnings->clear();
    CoGuard co;
    std::wstring dir = trimSlash(o.dir);
    if (!validInstallDir(dir)) return L"That is not a folder path I can use. Choose a full folder path like C:\\Users\\you\\Meridian (a drive letter, a colon and a backslash, with nothing like C:folder or ..\\ in it).";
    dir = trimSlash(dir);
    if (dir.size() > 200) return L"That folder path is too long. Choose a shorter one.";
    const Existing prev = detect();
    const bool movedDir = prev.found && !pathsEqual(prev.dir, dir);
    pr(0.01, L"Getting ready");
    if (!closeMeridian()) {
        if (stillRunning) *stillRunning = true;
        return L"Meridian is still running. Close it (right-click its icon in the taskbar, then Close window) and try again.";
    }
    if (!makeDirs(dir)) return L"I could not create " + dir + L". Choose a folder inside your own user folder, or run setup as administrator.";
    // Recover any interrupted or partially rolled-back transaction FIRST before cleanup or staging.
    {
        std::wstring recErr = recoverPendingTransaction(dir);
        if (!recErr.empty()) return recErr;
    }
    removeOldLeftovers(dir);

    // Phase 1: unpack and verify every file into a staging folder before touching the existing installation
    const std::wstring stageDir = dir + L"\\.meridian-stage-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount());
    if (!makeDirs(stageDir)) return L"Could not create temporary staging folder inside " + dir + L".";

    double total = 0, done = 0;
    for (auto& e : pl.e) if (included(e, o)) total += (double)e.size + 4096;
    total = std::max(1.0, total);
    std::vector<uint8_t> exeData, buf;
    std::vector<std::wstring> relFiles;
    for (auto& e : pl.e) {
        if (!included(e, o)) continue;
        std::wstring rel = winPath(e.path);
        pr(0.04 + 0.56 * done / total, L"Unpacking " + exeName(rel));
        if (!Payload::unpack(e, buf)) { deleteTree(stageDir); return L"The setup file is damaged (" + exeName(rel) + L" would not unpack). Download it again."; }
        std::wstring err = writeNewFile(stageDir + L"\\" + rel, buf.data(), buf.size());
        if (!err.empty()) { deleteTree(stageDir); return err; }
        relFiles.push_back(rel);
        if (e.path == "Meridian.exe") exeData = buf;
        done += (double)e.size + 4096;
    }
    if (exeData.empty()) { deleteTree(stageDir); return L"The setup file does not contain Meridian.exe."; }

    // Stage the uninstaller (a copy of this program) and screen saver binary (when enabled) together with the app files
    pr(0.64, L"Staging the uninstaller");
    size_t extraBytes = 0;
    {
        wchar_t me[MAX_PATH * 2]; DWORD n = GetModuleFileNameW(nullptr, me, MAX_PATH * 2);
        std::vector<uint8_t> self;
        if (!n || n >= MAX_PATH * 2 || !readWhole(me, self)) {
            deleteTree(stageDir);
            return L"Could not copy the uninstaller to " + dir + L".";
        }
        std::wstring err = writeNewFile(stageDir + L"\\uninstall.exe", self.data(), self.size());
        if (!err.empty()) { deleteTree(stageDir); return err; }
        relFiles.push_back(L"uninstall.exe");
        extraBytes += self.size();
    }
    if (o.saver) {
        std::wstring err = writeNewFile(stageDir + L"\\Meridian.scr", exeData.data(), exeData.size());
        if (!err.empty()) { deleteTree(stageDir); return err; }
        relFiles.push_back(L"Meridian.scr");
        extraBytes += exeData.size();
    }

    // Phase 2: transactional swap into `dir` (persisted journal + verified rollback preserving unresolved backups)
    pr(0.72, L"Installing files");
    {
        std::wstring err = commitStagedFiles(dir, stageDir, relFiles);
        deleteTree(stageDir);
        if (!err.empty()) return err;
    }
    if (!o.gta) removeGroup(pl, 1, dir);
    if (!o.halloween) removeGroup(pl, 2, dir);

    const std::wstring exe = dir + L"\\Meridian.exe";
    const std::wstring prevExe = movedDir ? prev.dir + L"\\Meridian.exe" : L"";
    pr(0.80, o.saver ? L"Setting up the screen saver" : L"Tidying up the screen saver");
    if (o.saver) {
        setScreenSaver(dir, o.saverMinutes);
        if (o.sleep) regSetDword(SAVER_KEY, L"SleepAfterMinutes", (DWORD)std::max(kSleepMin, std::min(kSleepMax, o.sleepMinutes)));
        else regDelValue(SAVER_KEY, L"SleepAfterMinutes");
    } else {
        restoreScreenSaver(dir);
        if (movedDir) restoreScreenSaver(prev.dir);
        deleteFile(dir + L"\\Meridian.scr");
        regDelValue(SAVER_KEY, L"SleepAfterMinutes");
    }

    pr(0.88, L"Making shortcuts");
    std::wstring sm = linkPath(false), dk = linkPath(true);
    if (o.startMenu) {
        if (sm.empty() || !makeLink(sm, exe, dir)) {
            if (warnings) warnings->push_back(L"Start Menu shortcut could not be created.");
        }
    } else { removeLinkIfOurs(false, exe); if (movedDir) removeLinkIfOurs(false, prevExe); }
    if (o.desktop) {
        if (dk.empty() || !makeLink(dk, exe, dir)) {
            if (warnings) warnings->push_back(L"Desktop shortcut could not be created.");
        }
    } else { removeLinkIfOurs(true, exe); if (movedDir) removeLinkIfOurs(true, prevExe); }
    if (o.runAtLogon) {
        if (!regSet(RUN_KEY, L"Meridian", L"\"" + exe + L"\"")) {
            if (warnings) warnings->push_back(L"Start with Windows registration could not be updated.");
        }
    } else {
        std::wstring cur; regGet(RUN_KEY, L"Meridian", cur);
        if (pathsEqual(cur, L"\"" + exe + L"\"") || (movedDir && pathsEqual(cur, L"\"" + prevExe + L"\""))) regDelValue(RUN_KEY, L"Meridian");
    }

    pr(0.94, L"Telling Windows about Meridian");
    const DWORD estSizeKb = (DWORD)((total + (double)extraBytes) / 1024.0) + 1;
    std::wstring regErr = registerAndRetireMove(pl, dir, movedDir ? prev.dir : L"", estSizeKb, nullptr);
    if (!regErr.empty()) return regErr;
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    pr(1.0, L"Done");
    return L"";
}

// ------------------------------------------------------------------ uninstall
void finishDeferredUninstall(const std::wstring& dirIn) {
    const std::wstring dir = trimSlash(dirIn);
    if (dir.empty()) return;
    std::wstring p = dir + L"\\uninstall.exe";
    for (int t = 0; t < 20 && fileExists(p); ++t) { if (deleteFile(p)) break; Sleep(150); }
    RemoveDirectoryW((dir + L"\\themes").c_str());
    RemoveDirectoryW(dir.c_str());
}

std::wstring uninstall(const Payload& pl, const std::wstring& dirIn, bool deleteSettings, const Progress& pr, bool* stillRunning, bool deferSelfUninstall) {
    if (stillRunning) *stillRunning = false;
    CoGuard co;
    std::wstring dir = trimSlash(dirIn), exe = dir + L"\\Meridian.exe";
    if (!validInstallDir(dir)) return L"The folder Windows has on record for Meridian (" + dirIn + L") does not look like a real folder path, so nothing was removed.";
    dir = trimSlash(dir); exe = dir + L"\\Meridian.exe";
    pr(0.02, L"Closing Meridian");
    if (!closeMeridian()) {
        if (stillRunning) *stillRunning = true;
        return L"Meridian is still running. Close it (right-click its icon in the taskbar, then Close window) and try again.";
    }
    pr(0.15, L"Putting your screen saver back");
    restoreScreenSaver(dir);
    pr(0.30, L"Removing shortcuts");
    removeLinkIfOurs(false, exe); removeLinkIfOurs(true, exe);
    { std::wstring cur; regGet(RUN_KEY, L"Meridian", cur); if (pathsEqual(cur, L"\"" + exe + L"\"")) regDelValue(RUN_KEY, L"Meridian"); }

    // only the files this setup put there: a theme pack you added yourself stays where it is
    std::vector<std::wstring> dirs; std::wstring problem;
    double n = (double)pl.e.size() + 3, i = 0;
    for (auto& e : pl.e) {
        std::wstring p = dir + L"\\" + winPath(e.path);
        pr(0.35 + 0.45 * (i++ / n), L"Removing " + exeName(p));
        if (!deleteFile(p)) problem = L"Could not remove " + exeName(p) + L": " + lastErrorText(GetLastError()) + L".";
        dirs.push_back(parentDir(p));
    }
    // Meridian.scr can be in use (Windows has it as the screen saver right now): wait a little. uninstall.exe stays while anything else is left,
    // so that Meridian is still listed in Apps and the uninstall can be tried again.
    { std::wstring p = dir + L"\\Meridian.scr"; DWORD err = 0;
      for (int t = 0; t < 12 && fileExists(p); ++t) { if (deleteFile(p)) break; err = GetLastError(); Sleep(250); }
      if (fileExists(p) && problem.empty()) problem = L"Could not remove Meridian.scr: " + lastErrorText(err) + L"."; }
    if (problem.empty() && !deferSelfUninstall) {
        std::wstring p = dir + L"\\uninstall.exe"; DWORD err = 0;
        for (int t = 0; t < 12 && fileExists(p); ++t) { if (deleteFile(p)) break; err = GetLastError(); Sleep(250); }
        if (fileExists(p)) problem = L"Could not remove uninstall.exe: " + lastErrorText(err) + L".";
    }
    cleanupOwnedTxAt(dir);
    removeOldLeftovers(dir);
    std::sort(dirs.begin(), dirs.end(), [](const std::wstring& a, const std::wstring& b) { return a.size() > b.size(); });
    for (auto& d : dirs) { std::wstring p = d; while (p.size() > dir.size()) { if (!RemoveDirectoryW(p.c_str())) break; p = parentDir(p); } }
    RemoveDirectoryW((dir + L"\\themes").c_str());
    if (!deferSelfUninstall) RemoveDirectoryW(dir.c_str());          // (only goes when it is empty)

    // If a file could not be removed, Meridian stays listed in Apps (with its uninstaller and your settings) so that this can be tried again.
    if (!problem.empty()) {
        pr(1.0, L"Done");
        return problem + L" Meridian is still listed under Settings, Apps, so you can try again once nothing is using it.";
    }
    pr(0.88, deleteSettings ? L"Removing your settings" : L"Cleaning up");
    // Only delete shared Windows Apps / InstallDir registration when it still points to THIS folder (never clobber a moved installation).
    std::wstring regLoc, regDir;
    const bool ownsUninst = !regGet(UNINSTALL_KEY, L"InstallLocation", regLoc) || regLoc.empty() || pathsEqual(trimSlash(regLoc), dir);
    const bool ownsApp    = !regGet(APP_KEY, L"InstallDir", regDir) || regDir.empty() || pathsEqual(trimSlash(regDir), dir);
    if (deleteSettings && (ownsUninst || ownsApp)) {
        for (bool local : {false, true}) {
            std::wstring f = settingsFolder(local);
            if (f.empty() || !dirExists(f)) continue;
            WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((f + L"\\*").c_str(), &fd);
            if (h != INVALID_HANDLE_VALUE) {
                do {
                    std::wstring nm = fd.cFileName;
                    if (nm == L"." || nm == L"..") continue;
                    std::wstring p = f + L"\\" + nm;
                    const bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
                    const bool isReparse = (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
                    if (isDir) {
                        if (_wcsicmp(nm.c_str(), L"themes") != 0) {
                            if (isReparse) RemoveDirectoryW(p.c_str());
                            else deleteTree(p);
                        }
                    } else deleteFile(p);
                } while (FindNextFileW(h, &fd));
                FindClose(h);
            }
            RemoveDirectoryW(f.c_str());
        }
    }
    if (ownsUninst) regDelTree(UNINSTALL_KEY);
    if (ownsApp)    regDelTree(APP_KEY);
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    pr(1.0, L"Done");
    return problem;
}

} // namespace inst
