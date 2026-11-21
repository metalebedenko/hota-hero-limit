# Hero Limit for Heroes III: Horn of the Abyss

[По-русски](README.md)

Hero Limit changes tavern recruitment in offline, single-player games of Horn of the Abyss 1.8.1. You and each computer opponent start with one hero. By default, each player can hire two more from taverns over the course of the game. Before you start, you can reduce that allowance to one or zero.

**Save games in v0.1.0.** The mod keeps track of hires in memory. When you load a save, it treats the current roster as the starting lineup. To keep the limit in effect for the whole game, start a new game and play it in one session.

## Installation

You need a working copy of HotA 1.8.1 with HD Mod. The [v0.1.0 pre-release](https://github.com/metalebedenko/hota-hero-limit/releases/tag/v0.1.0) includes two packages.

- **Installer** (`HeroLimit-HotA-1.8.1.zip`). Unzip the archive, open `HeroLimitSetup.exe` and select `h3hota HD.exe` in your game folder. Start a new single-player game with the separate `h3hota HeroLimit.exe` launcher.
- **Manual package** (`HeroLimit-HotA-1.8.1-manual.zip`). The archive includes the mod files and a script to prepare the same separate launcher. [Manual installation instructions for Windows and macOS (in Russian)](MANUAL.txt).

On macOS with CrossOver, use **Run Command** in the bottle where HotA is installed for both the installer and the new launcher. To play with the original rules, open `h3hota HD.exe` as usual. [Installation and removal notes (in Russian)](INSTALL.txt).

## Recruitment rules

Edit [HeroLimit.ini](HeroLimit.ini) before starting a game.

| Setting | Default | Effect |
| --- | --- | --- |
| `HeroesPerGame` | `3` | Includes the starting hero. Set it to `1`, `2` or `3` to allow zero, one or two new tavern hires per player. |
| `RetreatIsPermanent` | `false` | By default, the previous owner can rehire the same hero after a retreat without using a hire. Set it to `true` to prevent that hero’s return after retreating. |
| `PrisonsEnabled` | `true` | Freeing a prisoner uses no hires. Set it to `false` to disable recruitment through prisons. |

With the default settings, the previous owner can rehire the same hero after retreat or surrender even after spending the last hire. Dismissing a hero or losing one in battle does not give you another hire. If a fallen hero appears in the regular tavern pool again, recruiting that hero costs one of the remaining hires.

## What was checked in game

The recorded playthrough used HD Mod 5.8 R31 under CrossOver. To try the limit yourself, start a new single-player game against the computer with a map template that has no hiring limit of its own. The limit applied to regular tavern hires by both the human player and the computer. The previous owner could still rehire the same hero after a retreat, even with no hires left.

Surrender and prison releases passed the rules and DLL-loading checks. Both still need an in-game playthrough. Multiplayer is untested.

## Working on the project

Run `sh build.sh` on macOS to check the recruitment rules and build both release ZIPs in `dist/`, along with Windows diagnostic tools. The build uses LLVM-MinGW from `.sandbox/toolchain/`. Set `WINDOWS_CXX` and `WINDOWS_WINDRES` to use another compiler or resource compiler.

The rules model can take a snapshot of its settings and remaining hires. In v0.1.0, that snapshot stays separate from the game’s save files.

The source is in `src/`. The rules tests and Windows diagnostic loader are in `tests/`. The [Russian development notes](README.md#работа-с-проектом) cover the diagnostic tools and the separate local game copy used for playtesting. [Design decisions (in Russian)](DECISIONS.md).
