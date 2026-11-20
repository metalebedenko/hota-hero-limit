#include "rules.hpp"

#include <cassert>
#include <iostream>

using hero_limit::Departure;
using hero_limit::Match;
using hero_limit::Source;

int main() {
    for (unsigned limit = 1; limit <= 3; ++limit) {
        auto match = Match::start({static_cast<std::uint8_t>(limit), true, false}).value();
        for (unsigned player : {0u, 5u}) {
            const unsigned starter = 100 + 10 * player;
            assert(match.add_starting_hero(player, starter));
            assert(!match.add_starting_hero(player, starter + 5));
            assert(!match.can_acquire(player, starter + 6, Source::prison));

            for (unsigned i = 1; i < limit; ++i) {
                assert(match.can_acquire(player, starter + i, Source::tavern));
                assert(match.remaining(player) == limit - i);
                assert(match.acquire(player, starter + i, Source::tavern));
                assert(!match.acquire(player, starter + i, Source::tavern));
                assert(match.depart(player, starter + i, Departure::defeated));
            }
            assert(match.remaining(player) == 0);
            assert(!match.acquire(player, starter + 3, Source::tavern));
            assert(match.depart(player, starter, Departure::surrendered));

            match = Match::restore(match.snapshot()).value();
            assert(match.acquire(player, starter, Source::tavern));
            assert(match.remaining(player) == 0);
            assert(match.depart(player, starter, Departure::retreated));
            match = Match::restore(match.snapshot()).value();
            assert(!match.acquire(player, starter, Source::tavern));
            assert(!match.can_acquire(7, starter, Source::tavern));
        }
    }

    auto match = Match::start({1, false, true}).value();
    assert(match.add_starting_hero(2, 23));
    assert(match.acquire(2, 66, Source::prison));
    assert(match.depart(2, 23, Departure::retreated));
    assert(match.depart(2, 66, Departure::retreated));
    assert(!match.can_acquire(3, 66, Source::tavern));
    assert(match.acquire(2, 23, Source::tavern));
    assert(match.acquire(2, 66, Source::tavern));
    assert(match.remaining(2) == 0);
    assert(match.depart(2, 66, Departure::dismissed));
    assert(!match.acquire(2, 66, Source::tavern));

    auto normal = Match::start({3, false, true}).value();
    assert(normal.add_starting_hero(0, 10));
    assert(normal.acquire(0, 11, Source::tavern));
    assert(normal.depart(0, 11, Departure::defeated));
    assert(normal.remaining(0) == 1);
    assert(normal.acquire(0, 11, Source::tavern));
    assert(normal.remaining(0) == 0);
    assert(normal.acquire(0, 12, Source::prison));
    assert(normal.remaining(0) == 0);
    assert(normal.depart(0, 10, Departure::retreated));
    assert(normal.acquire(0, 10, Source::tavern));
    assert(normal.depart(0, 12, Departure::surrendered));
    assert(normal.acquire(0, 12, Source::tavern));
    assert(!normal.can_acquire(0, 13, Source::tavern));

    auto saved = match.snapshot();
    assert(!Match::restore(std::span{saved}.first(saved.size() - 1)));
    saved[7] = 2;
    assert(!Match::restore(saved));
    assert(!Match::start({4, false, false}));
    std::cout << "HeroLimit: budgets, prison, battle outcomes and save round trips passed\n";
}
