#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "config.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <span>

namespace {
using hero_limit::Match;
using hero_limit::Source;
using Address = std::uintptr_t;
using Code = std::array<std::uint8_t, 5>;
constexpr Address game_base = 0x400000;
constexpr Address main_slot = 0x699538;
constexpr Address current_color = 0x69ccf4;
constexpr Address selected_tavern_hero = 0x6aa690;
constexpr Address retreated_in_combat = 0x6985f3;
constexpr Address surrendered_in_combat = 0x697794;
constexpr unsigned player_offset = 0x20ad0;
constexpr unsigned player_stride = 0x168;
constexpr unsigned hero_id_offset = 0x1a;
constexpr unsigned hero_owner_offset = 0x22;
constexpr unsigned hero_flags_offset = 0x105;
constexpr std::uint32_t reserved_tavern_offer = 0x20000;
constexpr unsigned object_type_offset = 0x1e;
constexpr int prison_object = 62;

struct Roster {
    unsigned count = 0;
    std::array<int, 8> heroes{};

    bool contains(int hero) const {
        const auto owned = std::span(heroes).first(count);
        return std::ranges::find(owned, hero) != owned.end();
    }
};

struct ColorState {
    bool initialized = false;
    Roster roster;
};

hero_limit::Settings settings;
std::optional<Match> match;
Address main_object = 0;
std::array<ColorState, Match::player_count> colors;
std::FILE* journal;
std::atomic<bool> initialized{false};

template<class T> bool read(Address address, T& value) {
    SIZE_T size = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
                             &value, sizeof(value), &size) && size == sizeof(value);
}

bool read_roster(Address main, int color, Roster& roster) {
    std::array<std::uint8_t, 40> header{};
    const Address player = main + player_offset + player_stride * static_cast<unsigned>(color);
    if (!read(player, header) || header[0] != color || header[1] > roster.heroes.size()) {
        return false;
    }
    roster.count = header[1];
    std::memcpy(roster.heroes.data(), header.data() + 8, sizeof(roster.heroes));
    return std::ranges::all_of(std::span(roster.heroes).first(roster.count), [](int id) {
        return id >= 0 && id < static_cast<int>(Match::hero_count);
    });
}

int hero_id(const void* hero) {
    int id = -1;
    if (hero) (void)read(reinterpret_cast<Address>(hero) + hero_id_offset, id);
    return id;
}

int hero_owner(const void* hero) {
    std::int8_t color = -1;
    if (hero) (void)read(reinterpret_cast<Address>(hero) + hero_owner_offset, color);
    return color;
}

void log_event(const char* action, const char* route, int color, int hero, unsigned count) {
    if (journal) {
        std::fprintf(journal, "%s route=%s color=%d hero=%d owned=%u remaining=%u\n",
                     action, route, color, hero, count,
                     match && color >= 0 ? match->remaining(static_cast<unsigned>(color)) : 0);
        std::fflush(journal);
    }
}

bool sync_color(int color) {
    if (color < 0 || color >= static_cast<int>(colors.size())) return false;
    Address main = 0;
    Roster current;
    if (!read(main_slot, main) || !main || !read_roster(main, color, current)) return false;
    if (main_object != main) {
        main_object = main;
        match = Match::start(settings);
        colors = {};
    }
    auto& state = colors[color];
    if (!state.initialized) {
        if (current.count) {
            (void)match->add_starting_hero(color, current.heroes[0]);
            for (unsigned index = 1; index < current.count; ++index) {
                (void)match->acquire(color, current.heroes[index], Source::tavern);
            }
        }
        state.initialized = true;
        log_event("initialized", "roster", color, -1, current.count);
    } else {
        for (int id : std::span(state.roster.heroes).first(state.roster.count)) {
            if (!current.contains(id)) {
                (void)match->depart(color, id, hero_limit::Departure::dismissed);
            }
        }
    }
    state.roster = current;
    return true;
}

bool allow_hire(int color, int id, const char* route) {
    if (!sync_color(color)) {
        log_event("denied-invalid-state", route, color, id, 0);
        return false;
    }
    if (id >= 0 && match->can_acquire(color, id, Source::tavern)) return true;
    log_event("denied", route, color, id, colors[color].roster.count);
    return false;
}

using TownPurchase = void (__thiscall*)(void*, const void*, int);
using TavernPurchase = void (__thiscall*)(const void*, int, int);
using ConfirmPurchase = unsigned char (__thiscall*)(void*);
using AiPurchase = unsigned char (__fastcall*)(int, void*);
using RemoveHero = void (__thiscall*)(void*, int, int);
using VisitPrison = void (__thiscall*)(void*, void*, const void*, int, int);
TownPurchase original_town_purchase;
TavernPurchase original_tavern_purchase;
ConfirmPurchase original_confirm;
AiPurchase original_ai_purchase;
RemoveHero original_remove;
VisitPrison original_prison;

const char* active_route;

template<class Action>
void purchase(const char* fallback, int color, const void* hero, Action original) {
    const char* route = active_route ? active_route : fallback;
    const int id = hero_id(hero);
    if (!allow_hire(color, id, route)) return;
    const unsigned before = match->remaining(color);
    original();
    Roster current;
    if (read_roster(main_object, color, current) && current.contains(id) &&
        match->acquire(color, id, Source::tavern)) {
        colors[color].roster = current;
        log_event(match->remaining(color) == before ? "returned" : "purchased",
                  route, color, id, current.count);
    } else {
        log_event("purchase-unconfirmed", route, color, id, colors[color].roster.count);
    }
}

void __fastcall town_purchase(void* town, void*, const void* hero, int color) {
    purchase("town", color, hero, [&] { original_town_purchase(town, hero, color); });
}

void __fastcall tavern_purchase(const void* hero, void*, int color, int position) {
    purchase("tavern", color, hero, [&] { original_tavern_purchase(hero, color, position); });
}

unsigned char __fastcall confirm_purchase(void* dialog, void*) {
    const auto confirmed = original_confirm(dialog);
    if (!confirmed) return confirmed;
    // Let the game run the entire tavern dialog, including the thieves' guild.
    // Suppress a confirmed purchase before the caller debits gold or refreshes the town.
    int color = -1;
    Address selected = 0;
    const bool authorized = read(current_color, color) && read(selected_tavern_hero, selected) &&
        allow_hire(color, hero_id(reinterpret_cast<const void*>(selected)), "human");
    return authorized ? confirmed : 0;
}

unsigned char __fastcall ai_purchase(int color, void* data) {
    if (!allow_hire(color, hero_id(data), "ai")) return 0;
    const char* previous_route = active_route;
    active_route = "ai";
    const auto outcome = original_ai_purchase(color, data);
    active_route = previous_route;
    return outcome;
}

void __fastcall remove_hero(void* hero, void*, int remove_from_map, int update) {
    using hero_limit::Departure;
    const int id = hero_id(hero);
    const int color = hero_owner(hero);
    const bool owned = id >= 0 && sync_color(color) && colors[color].roster.contains(id);
    std::uint8_t retreated = 0;
    std::uint8_t surrendered = 0;
    if (owned) {
        (void)read(retreated_in_combat, retreated);
        (void)read(surrendered_in_combat, surrendered);
    }
    original_remove(hero, remove_from_map, update);

    Roster current;
    if (!owned || !read_roster(main_object, color, current) || current.contains(id)) return;
    std::uint32_t hero_flags = 0;
    (void)read(reinterpret_cast<Address>(hero) + hero_flags_offset, hero_flags);
    const bool has_return_offer = (hero_flags & reserved_tavern_offer) != 0;
    const auto reason = has_return_offer && surrendered ? Departure::surrendered
        : has_return_offer && retreated ? Departure::retreated
        : Departure::defeated;
    const char* outcome = reason == Departure::surrendered ? "surrendered"
        : reason == Departure::retreated ? "retreated" : "lost";
    if (match->depart(color, id, reason)) {
        log_event(outcome, "departure", color, id, current.count);
    }
    colors[color].roster = current;
}

void __fastcall visit_prison(void* manager, void*, void* visitor, const void* tile,
                            int position, int show_dialog) {
    const int color = hero_owner(visitor);
    int id = -1;
    int type = -1;
    const Address object = reinterpret_cast<Address>(tile);
    const bool known = tile && read(object + object_type_offset, type) && type == prison_object &&
        read(object, id) && id >= 0 && id < static_cast<int>(Match::hero_count) && sync_color(color);
    if (known && !settings.prisons_enabled) {
        log_event("denied", "prison", color, id, colors[color].roster.count);
        return;
    }
    const bool already_owned = known && colors[color].roster.contains(id);
    original_prison(manager, visitor, tile, position, show_dialog);

    Roster current;
    if (known && !already_owned && read_roster(main_object, color, current) &&
        current.contains(id) && match->acquire(color, id, Source::prison)) {
        colors[color].roster = current;
        log_event("released", "prison", color, id, current.count);
    }
}

Code jump(Address from, Address to) {
    Code bytes{0xe9};
    const auto offset = static_cast<std::uint32_t>(to - (from + bytes.size()));
    std::memcpy(bytes.data() + 1, &offset, sizeof(offset));
    return bytes;
}

struct Hook {
    Address site;
    Address entry;
    std::array<std::uint8_t, 12> signature;
    unsigned length = 5;
};

Address build_trampoline(const Hook& hook, std::uint8_t* code) {
    std::array<std::uint8_t, 12> actual{};
    if (!read(hook.site, actual)) return 0;
    if (actual == hook.signature) {
        const auto trampoline = reinterpret_cast<Address>(code);
        std::ranges::copy(std::span(actual).first(hook.length), code);
        const auto resume = jump(trampoline + hook.length, hook.site + hook.length);
        std::ranges::copy(resume, code + hook.length);
        return trampoline;
    }
    if (actual[0] == 0xe9 &&
        std::ranges::all_of(std::span(actual).subspan(5, hook.length - 5),
                            [](std::uint8_t value) { return value == 0x90; }) &&
        std::ranges::equal(std::span(actual).subspan(hook.length),
                           std::span(hook.signature).subspan(hook.length))) {
        std::uint32_t relative;
        std::memcpy(&relative, actual.data() + 1, sizeof(relative));
        const Address target = hook.site + 5 + relative;
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQuery(reinterpret_cast<const void*>(target), &region, sizeof(region)) ||
            region.State != MEM_COMMIT ||
            (region.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE |
                               PAGE_EXECUTE_WRITECOPY)) == 0) return 0;
        return target;
    }
    return 0;
}

bool install_hooks() {
    if (reinterpret_cast<Address>(GetModuleHandleW(nullptr)) != game_base) return false;
    std::array hooks{
        Hook{0x5c16e0, reinterpret_cast<Address>(town_purchase),
             {0x55, 0x8b, 0xec, 0x83, 0xec, 0x08, 0x53, 0x8b, 0x5d, 0x0c, 0x56, 0x8b}, 6},
        Hook{0x4d7b00, reinterpret_cast<Address>(tavern_purchase),
             {0x55, 0x8b, 0xec, 0x53, 0x56, 0x8b, 0x15, 0x38, 0x95, 0x69, 0x00, 0x57}},
        Hook{0x5d82c0, reinterpret_cast<Address>(confirm_purchase),
             {0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0x5b, 0x68, 0x63, 0x00, 0x64, 0xa1}},
        Hook{0x431800, reinterpret_cast<Address>(ai_purchase),
             {0x55, 0x8b, 0xec, 0x6a, 0xff, 0x68, 0xab, 0x83, 0x62, 0x00, 0x64, 0xa1}},
        Hook{0x4da130, reinterpret_cast<Address>(remove_hero),
             {0x55, 0x8b, 0xec, 0x83, 0xec, 0x28, 0x56, 0x8b, 0xf1, 0x57, 0xc6, 0x45}, 6},
        Hook{0x4a3b20, reinterpret_cast<Address>(visit_prison),
             {0x55, 0x8b, 0xec, 0x83, 0xec, 0x2c, 0xa1, 0x38, 0x95, 0x69, 0x00, 0x53}, 6},
    };
    constexpr std::size_t trampoline_size = 11;
    const auto code_size = trampoline_size * hooks.size();
    auto* code = static_cast<std::uint8_t*>(
        VirtualAlloc(nullptr, code_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!code) return false;
    std::array<Address, hooks.size()> originals{};
    DWORD previous = 0;
    for (std::size_t i = 0; i < hooks.size(); ++i) {
        originals[i] = build_trampoline(hooks[i], code + i * trampoline_size);
        if (!originals[i]) {
            VirtualFree(code, 0, MEM_RELEASE);
            return false;
        }
    }
    if (!VirtualProtect(code, code_size, PAGE_EXECUTE_READ, &previous)) {
        VirtualFree(code, 0, MEM_RELEASE);
        return false;
    }
    FlushInstructionCache(GetCurrentProcess(), code, code_size);

    std::array<DWORD, hooks.size()> protections{};
    std::size_t protected_sites = 0;
    for (const auto& hook : hooks) {
        if (!VirtualProtect(reinterpret_cast<void*>(hook.site), 5, PAGE_EXECUTE_READWRITE,
                            &protections[protected_sites])) break;
        ++protected_sites;
    }
    const bool ready = protected_sites == hooks.size();
    if (ready) {
        original_town_purchase = reinterpret_cast<TownPurchase>(originals[0]);
        original_tavern_purchase = reinterpret_cast<TavernPurchase>(originals[1]);
        original_confirm = reinterpret_cast<ConfirmPurchase>(originals[2]);
        original_ai_purchase = reinterpret_cast<AiPurchase>(originals[3]);
        original_remove = reinterpret_cast<RemoveHero>(originals[4]);
        original_prison = reinterpret_cast<VisitPrison>(originals[5]);
        for (const auto& hook : hooks) {
            const auto redirect = jump(hook.site, hook.entry);
            auto* location = reinterpret_cast<void*>(hook.site);
            std::memcpy(location, redirect.data(), redirect.size());
            FlushInstructionCache(GetCurrentProcess(), location, redirect.size());
        }
    } else {
        VirtualFree(code, 0, MEM_RELEASE);
    }
    for (std::size_t i = 0; i < protected_sites; ++i) {
        VirtualProtect(reinterpret_cast<void*>(hooks[i].site), 5, protections[i], &previous);
    }
    return ready;
}

LRESULT CALLBACK install_on_game_thread(int code, WPARAM wparam, LPARAM lparam) {
    if (code >= 0 && !initialized.load()) {
        const bool installed = install_hooks();
        if (journal) {
            std::fprintf(journal, "%s heroes=%u\n",
                         installed ? "active" : "unsupported-runtime", settings.heroes_per_game);
            std::fflush(journal);
        }
        initialized.store(true);
        if (!installed) {
            MessageBoxW(nullptr, L"Эта сборка игры не прошла проверку функций. Лимит отключён.",
                        L"Hero Limit", MB_OK | MB_ICONERROR);
        }
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

BOOL CALLBACK locate_game_window(HWND window, LPARAM result) {
    DWORD process = 0;
    const DWORD thread = GetWindowThreadProcessId(window, &process);
    wchar_t window_class[32]{};
    if (process == GetCurrentProcessId() && GetClassNameW(window, window_class, 32) &&
        std::wcscmp(window_class, L"Heroes III") == 0) {
        *reinterpret_cast<DWORD*>(result) = thread;
        return FALSE;
    }
    return TRUE;
}

DWORD WINAPI initialize(void* module) {
    std::array<wchar_t, MAX_PATH> executable{};
    const DWORD length = GetModuleFileNameW(nullptr, executable.data(), executable.size());
    if (!length || length == executable.size()) return 1;
    const auto directory = std::filesystem::path(executable.data()).parent_path();
    journal = _wfopen((directory / L"HeroLimit.log").c_str(), L"w");
    const auto config = hero_limit::read_settings(directory / L"HeroLimit.ini");
    if (!config) {
        if (journal) { std::fputs("invalid-config\n", journal); std::fflush(journal); }
        MessageBoxW(nullptr, L"Проверьте настройки в HeroLimit.ini. Лимит героев отключён.",
                    L"Hero Limit", MB_OK | MB_ICONERROR);
        return 1;
    }
    settings = *config;
    DWORD thread = 0;
    for (unsigned retry = 0; retry < 400 && !thread; ++retry) {
        if (GetModuleHandleW(L"HD_HOTA.dll")) EnumWindows(locate_game_window, reinterpret_cast<LPARAM>(&thread));
        if (!thread) Sleep(50);
    }
    if (!thread) return 2;
    const HHOOK hook = SetWindowsHookExW(WH_GETMESSAGE, install_on_game_thread,
                                        static_cast<HMODULE>(module), thread);
    if (!hook) return 3;
    PostThreadMessageW(thread, WM_NULL, 0, 0);
    for (unsigned retry = 0; retry < 200 && !initialized.load(); ++retry) Sleep(50);
    UnhookWindowsHookEx(hook);
    return initialized.load() ? 0 : 4;
}
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        // CrossOver can start an executable from a folder other than the game's.
        // HD Mod uses relative resource paths during startup.
        std::array<wchar_t, MAX_PATH> executable{};
        const DWORD length = GetModuleFileNameW(nullptr, executable.data(), executable.size());
        if (!length || length == executable.size()) return FALSE;
        auto* separator = std::wcsrchr(executable.data(), L'\\');
        if (!separator) return FALSE;
        separator[1] = L'\0';
        if (!SetCurrentDirectoryW(executable.data())) return FALSE;
        DisableThreadLibraryCalls(module);
        const HANDLE worker = CreateThread(nullptr, 0, initialize, module, 0, nullptr);
        if (worker) CloseHandle(worker);
    }
    return TRUE;
}
