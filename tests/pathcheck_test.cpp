// Tests for the install-folder check of the setup (setup/pathcheck.h). No Windows needed:
//   g++ -std=c++17 -I setup tests/pathcheck_test.cpp -o pathcheck_test && ./pathcheck_test
#include "pathcheck.h"
#include <cstdio>
static int fails = 0;
static bool ok(const wchar_t* s) { std::wstring d = s; return inst::validInstallDir(d); }
#define GOOD(s) do { if (!ok(s)) { std::printf("FAIL (should be accepted) line %d: %ls\n", __LINE__, s); ++fails; } } while (0)
#define BAD(s)  do { if (ok(s))  { std::printf("FAIL (should be refused) line %d: %ls\n", __LINE__, s); ++fails; } } while (0)
int main() {
    GOOD(L"C:\\Users\\me\\AppData\\Local\\Programs\\Meridian");
    GOOD(L"D:\\Meridian");
    GOOD(L"d:/tools/Meridian");                 // forward slashes are fine
    GOOD(L"C:\\Program Files\\Meridian\\");     // a trailing slash is fine
    GOOD(L"\\\\server\\share\\Meridian");
    GOOD(L"C:\\a.b\\c d\\e");
    BAD(L"C:foo");                              // drive-relative
    BAD(L"C:Users\\me\\Meridian");
    BAD(L"C:");  BAD(L"C:\\");                  // a whole drive
    BAD(L"Meridian"); BAD(L".\\Meridian"); BAD(L"..\\Meridian"); BAD(L"\\Meridian"); BAD(L"");
    BAD(L"C:\\a\\..\\b"); BAD(L"C:\\a\\.\\b"); BAD(L"C:\\a\\\\b");
    BAD(L"\\\\?\\C:\\Meridian"); BAD(L"\\\\.\\C:\\Meridian"); BAD(L"\\\\server"); BAD(L"\\\\server\\"); BAD(L"\\\\\\x\\y");
    BAD(L"C:\\a<b"); BAD(L"C:\\a|b"); BAD(L"C:\\a?b"); BAD(L"C:\\a*b"); BAD(L"C:\\a\"b");
    BAD(L"C:\\a:b"); BAD(L"C:\\foo.");  BAD(L"C:\\foo \\bar");
    BAD(L"1:\\Meridian");
    std::printf(fails ? "%d FAILED\n" : "pathcheck: all passed\n", fails);
    return fails ? 1 : 0;
}
