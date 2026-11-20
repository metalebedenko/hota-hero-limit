#include "rules.hpp"

#include <algorithm>

namespace hero_limit {
namespace {
constexpr std::array<std::uint8_t, 4> magic{'H', 'L', 'M', 1};
constexpr unsigned header_size = magic.size() + 3;
}

bool Settings::valid() const {
    return heroes_per_game >= 1 && heroes_per_game <= 3;
}

Match::Match(Settings settings) : settings_(settings) {
    remaining_.fill(settings_.heroes_per_game - 1);
}

std::optional<Match> Match::start(Settings settings) {
    if (!settings.valid()) {
        return std::nullopt;
    }
    return Match(settings);
}

const Settings& Match::settings() const {
    return settings_;
}

unsigned Match::remaining(unsigned player) const {
    return player < player_count ? remaining_[player] : 0;
}

bool Match::add_starting_hero(unsigned player, unsigned hero) {
    if (player >= player_count || hero >= hero_count || heroes_[hero] != 0 ||
        std::ranges::find(heroes_, player + 1) != heroes_.end()) {
        return false;
    }
    heroes_[hero] = static_cast<std::uint8_t>(player + 1);
    return true;
}

bool Match::can_acquire(unsigned player, unsigned hero, Source source) const {
    if (player >= player_count || hero >= hero_count) {
        return false;
    }
    const auto status = heroes_[hero];
    if (status == retired || (status != 0 && status < returning)) {
        return false;
    }
    switch (source) {
    case Source::tavern:
        return status == returning + player || remaining_[player] != 0;
    case Source::prison:
        return settings_.prisons_enabled && status == 0;
    }
    return false;
}

bool Match::acquire(unsigned player, unsigned hero, Source source) {
    if (!can_acquire(player, hero, source)) {
        return false;
    }
    if (source == Source::tavern && heroes_[hero] != returning + player) {
        --remaining_[player];
    }
    heroes_[hero] = static_cast<std::uint8_t>(player + 1);
    return true;
}

bool Match::depart(unsigned player, unsigned hero, Departure departure) {
    if (player >= player_count || hero >= hero_count || heroes_[hero] != player + 1) {
        return false;
    }
    switch (departure) {
    case Departure::defeated:
    case Departure::dismissed:
        heroes_[hero] = 0;
        return true;
    case Departure::retreated:
        if (settings_.retreat_is_permanent) {
            heroes_[hero] = retired;
            return true;
        }
        [[fallthrough]];
    case Departure::surrendered:
        heroes_[hero] = static_cast<std::uint8_t>(returning + player);
        return true;
    }
    return false;
}

Match::Snapshot Match::snapshot() const {
    Snapshot data{};
    std::ranges::copy(magic, data.begin());
    data[4] = settings_.heroes_per_game;
    data[5] = settings_.retreat_is_permanent;
    data[6] = settings_.prisons_enabled;
    std::ranges::copy(remaining_, data.begin() + header_size);
    std::ranges::copy(heroes_, data.begin() + header_size + player_count);
    return data;
}

std::optional<Match> Match::restore(std::span<const std::uint8_t> data) {
    if (data.size() != snapshot_size || !std::ranges::equal(magic, data.first(magic.size())) ||
        data[5] > 1 || data[6] > 1) {
        return std::nullopt;
    }
    const Settings settings{data[4], data[5] != 0, data[6] != 0};
    auto match = start(settings);
    if (!match) {
        return std::nullopt;
    }
    const auto budgets = data.subspan(header_size, player_count);
    const auto heroes = data.subspan(header_size + player_count);
    if (std::ranges::any_of(budgets, [settings](auto n) { return n >= settings.heroes_per_game; }) ||
        std::ranges::any_of(heroes, [](auto status) { return status > retired; })) {
        return std::nullopt;
    }
    std::ranges::copy(budgets, match->remaining_.begin());
    std::ranges::copy(heroes, match->heroes_.begin());
    return match;
}

} // namespace hero_limit
