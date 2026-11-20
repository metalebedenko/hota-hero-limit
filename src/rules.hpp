#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace hero_limit {

struct Settings {
    std::uint8_t heroes_per_game = 3;
    bool retreat_is_permanent = true;
    bool prisons_enabled = false;

    [[nodiscard]] bool valid() const;
};

enum class Source : std::uint8_t { tavern, prison };
enum class Departure : std::uint8_t { defeated, dismissed, retreated, surrendered };

class Match {
public:
    static constexpr unsigned player_count = 8;
    static constexpr unsigned hero_count = 1024;
    static constexpr std::size_t snapshot_size = 7 + player_count + hero_count;
    using Snapshot = std::array<std::uint8_t, snapshot_size>;

    [[nodiscard]] static std::optional<Match> start(Settings settings);
    [[nodiscard]] static std::optional<Match> restore(std::span<const std::uint8_t> snapshot);

    [[nodiscard]] const Settings& settings() const;
    [[nodiscard]] unsigned remaining(unsigned player) const;
    [[nodiscard]] Snapshot snapshot() const;

    // Called once for each hero actually owned at map start.
    [[nodiscard]] bool add_starting_hero(unsigned player, unsigned hero);
    [[nodiscard]] bool can_acquire(unsigned player, unsigned hero, Source source) const;
    // Call only after the game confirms that acquisition succeeded.
    [[nodiscard]] bool acquire(unsigned player, unsigned hero, Source source);
    [[nodiscard]] bool depart(unsigned player, unsigned hero, Departure departure);

private:
    explicit Match(Settings settings);

    // 0 = unowned, 1..8 = owned, 9..16 = free return to former owner, 17 = retired.
    static constexpr std::uint8_t returning = player_count + 1;
    static constexpr std::uint8_t retired = 2 * player_count + 1;

    Settings settings_;
    std::array<std::uint8_t, player_count> remaining_{};
    std::array<std::uint8_t, hero_count> heroes_{};
};

} // namespace hero_limit
