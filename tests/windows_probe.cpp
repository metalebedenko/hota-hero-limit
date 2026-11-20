#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "Usage: HeroLimitProbe.exe <dll> <config.ini>\n");
        return 1;
    }
    const auto module = LoadLibraryW(argv[1]);
    if (!module) {
        std::fprintf(stderr, "LoadLibrary failed (Win32 error %lu)\n", GetLastError());
        return 2;
    }
    using Version = unsigned (*)();
    using Check = int (*)(const wchar_t*);
    const auto version = reinterpret_cast<Version>(GetProcAddress(module, "HeroLimitProbeVersion"));
    const auto check = reinterpret_cast<Check>(GetProcAddress(module, "HeroLimitProbeCheck"));
    if (!version || !check || version() != 1) {
        std::fprintf(stderr, "Unexpected DLL exports\n");
        FreeLibrary(module);
        return 3;
    }
    const int result = check(argv[2]);
    FreeLibrary(module);
    std::printf("HeroLimit Win32 probe: %s (%d)\n", result == 0 ? "PASS" : "FAIL", result);
    return result;
}
