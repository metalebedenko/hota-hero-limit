#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "config.hpp"

// The diagnostic DLL never changes game memory. Keep initialization safe under the loader lock.
BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        OutputDebugStringW(L"HeroLimit probe attached. Game rules are inactive.");
    }
    return TRUE;
}

extern "C" __declspec(dllexport) unsigned HeroLimitProbeVersion() {
    return 1;
}

extern "C" __declspec(dllexport) int HeroLimitProbeCheck(const wchar_t* config_path) {
    if (!config_path) {
        return 1;
    }
    try {
        const auto settings = hero_limit::read_settings(config_path);
        if (!settings) {
            return 2;
        }
        using hero_limit::Departure;
        using hero_limit::Match;
        using hero_limit::Source;

        auto match = Match::start(*settings).value();
        for (unsigned player : {0u, 1u}) {
            const unsigned starter = 10 + 10 * player;
            if (!match.add_starting_hero(player, starter)) {
                return 3;
            }
            for (unsigned i = 1; i < settings->heroes_per_game; ++i) {
                if (!match.acquire(player, starter + i, Source::tavern) ||
                    !match.depart(player, starter + i, Departure::defeated)) {
                    return 4;
                }
            }
            if (match.remaining(player) != 0 || match.acquire(player, starter + 3, Source::tavern) ||
                match.acquire(player, starter + 4, Source::prison) != settings->prisons_enabled ||
                !match.depart(player, starter, Departure::surrendered)) {
                return 5;
            }
            auto restored = Match::restore(match.snapshot());
            if (!restored || !restored->acquire(player, starter, Source::tavern) ||
                !restored->depart(player, starter, Departure::retreated) ||
                restored->acquire(player, starter, Source::tavern) == settings->retreat_is_permanent) {
                return 6;
            }
            match = *restored;
        }
        return 0;
    } catch (...) {
        return 7;
    }
}
