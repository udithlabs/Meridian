// The check on the folder Meridian is installed to. Plain C++ (no Windows calls), so tests/pathcheck_test.cpp can run it anywhere.
#pragma once
#include <string>

namespace inst {

// Accepts  C:\Folder\Sub  and  \\server\share\Sub  (a forward slash counts as a backslash and is turned into one).
// Refuses what Windows would read in a way nobody meant:
//   C:foo       a drive-relative path: it means "foo in whatever folder this drive is currently in"
//   foo, .\foo  a relative path (it would follow whatever folder the setup was started from)
//   \\?\C:\x, \\.\x   device paths;   C:\a\..\b   dots in the middle;   C:\a\\b   an empty part;   <>"|?*   characters no folder name may hold
//   C:\         a whole drive;   a colon anywhere but right after the drive letter
inline bool validInstallDir(std::wstring& d) {
    for (auto& c : d) if (c == L'/') c = L'\\';
    while (d.size() > 3 && d.back() == L'\\') d.pop_back();
    if (d.find_first_of(L"<>\"|?*") != std::wstring::npos) return false;
    for (wchar_t c : d) if (c < 32) return false;
    size_t start;
    const bool drive = d.size() >= 4 && ((d[0] >= L'A' && d[0] <= L'Z') || (d[0] >= L'a' && d[0] <= L'z')) && d[1] == L':' && d[2] == L'\\';
    const bool unc = d.size() >= 5 && d[0] == L'\\' && d[1] == L'\\' && d[2] != L'\\' && d[2] != L'?' && d[2] != L'.';
    if (drive) start = 3; else if (unc) start = 2; else return false;
    if (d.find(L':', drive ? 2 : 0) != std::wstring::npos) return false;
    int parts = 0;
    size_t i = start;
    while (i <= d.size()) {
        size_t j = d.find(L'\\', i); if (j == std::wstring::npos) j = d.size();
        const std::wstring p = d.substr(i, j - i);
        if (p.empty() || p == L"." || p == L"..") return false;
        if (p.back() == L' ' || p.back() == L'.') return false;                   // Windows silently drops these, so the folder would not be the one asked for
        ++parts; i = j + 1;
    }
    return unc ? parts >= 2 : parts >= 1;
}

} // namespace inst
