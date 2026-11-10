# Release gate and budgets

What a release of Bellows passes before its draft is published
([CONTRIBUTING.md](../CONTRIBUTING.md#versions-and-releases)), and what Bellows may cost.
The game checks run in the lab (`eu4-engine-lab`, private), whose harness starts the
game, drives it through Tongs and compares the logs; this file says what they must
show, not how the lab runs them.

## Gate

Every release:

1. **CI is green** on the release pull request: build, `ctest` (with `plugin_load_*` and
   `symbols_lock`), CodeQL, the Workshop dry run.
2. **The release files.** The draft's `bellows-windows-x64.zip` matches `SHA256SUMS.txt`
   and its build attestation; the game checks below run with the `bellows/` folder from
   that zip, not a local build.
3. **Start-up, defaults.** Three or more interleaved warm starts with and without Bellows
   (Anvil and anvil-game in both) on the lab playset: the start with Bellows is not
   slower, the `modules:` line of `anvil/anvil.log` has no `FAILED`, every start-up
   module reports `mismatches 0`, and `setup.log`, `error.log` and `system.log` are the
   same in both arms.
4. **Play, defaults.** A save loaded, one game year at speed 5, one autosave: no crash
   or error dialog, nothing new in `error.log` against the same session without
   Bellows, `mismatches 0` in the simulation modules' `.tsv` files, and the autosave
   loads again.
5. **`"verify"` for what changed.** A module that is new or changed in the release, and
   every module whose default becomes `on`, runs a `"verify"` session on the 1719 save
   and on a late-game save (1821) with `mismatches 0`. A module that replaces a game
   computation turns default `on` only after its `"verify"` sessions and a timed series
   (CONTRIBUTING.md, measurement rules).
6. **Claims.** A number the release adds to the README, the Workshop description or the
   plugin page comes from a measurement with the method written next to it.

Every few releases, and before 1.0, the gate also covers what a single release does not
touch: a big mod set (Anbennar) in `"verify"`, the repeatability pair below and the
multiplayer test (host with Bellows, client without).

### The repeatability pair

The lab's repeatability oracle: the game with Bellows at its defaults against the same game
without Bellows, in conditions where the game without Bellows repeats itself exactly.

- **When:** every release that changes which simulation modules are on by default, and
  every few releases otherwise.
- **Conditions:** speed 5, `-seed=12345` and `-randomlog`, the lab plugin `lab-tiebreak`
  (which breaks the known AI ties by a stable rule instead of the memory address), the
  save of Russia of 17 March 1719, 60 game days, one run to the end date without stops.
  Anvil, anvil-game and Tongs in both arms; Bellows from the release files with default
  settings in one.
- **Pass:** every full game day has the same random draws (file, line, value) and the same
  executed commands (without frame numbers, command IDs and `client_ping`) in every run.
  A run whose random log was not copied completely does not count.
- **A difference caused by a Bellows module** keeps that module from being `on` by default
  until it is explained.
- **A difference from the game itself** (a new tie broken by address, or another cause the
  game without Bellows also shows) is written up, and `lab-tiebreak` is extended to cover
  it. It does not block the release.

Last pair: Bellows 1.4.1 with default settings, 9 November 2026 (lab batch `r84e`). Six
runs without Bellows were the same as each other in all 60 days, and so were two runs with
Bellows and one more without. The whole random log was the same byte for byte in all nine.
The defaults have not changed since 1.4. The pair runs with `-seed`, which also runs the
AI's daily work on one thread, so it shows that the default modules change no draw. It does
not show that a player's game without `-seed` comes out the same (README, "AI ties and
multiplayer").

## Budgets

Measured on Bellows 0.18.0 with default settings, EU4 1.37.5 (Steam), the lab playset
(10 mods), 28 starts on 2 November 2026 (lab sessions of `range_witness` `"verify"`,
`army_search_memo`, `province_modifier_view` and the Anvil 1.7 test; Anvil 1.5 to 1.7, warm starts):

| What | Budget | 0.18.0: min / median / max | Source |
| --- | ---: | --- | --- |
| `load()` (Anvil's `bellows …: loaded (early, … ms)`) | 100 ms | 13.2 / 50.2 / 57.8 ms | `anvil/anvil.log` |
| Main thread at the ready event (mimalloc collect) | 25 ms | 0.7 / 8.8 / 15.5 ms | `mimalloc: freed pages returned at ready in … ms` |
| Main thread for a collect in a frame after 64 MiB freed | 10 ms | 3.8 / 4.2 / 4.6 ms (19 collects) | `… returned at 64 MiB freed in … ms` |
| Memory held by `ram_prefetch` during start-up | `ram_prefetch_budget_mb` (1024 MB) | peak 1023 of 1024 MB | `ram_prefetch: … peak not yet taken …` |
| Process commit after the ready collect | not set | 3733 / 3808 / 3863 MiB | `commit … -> … MiB` |
| Answers compared with the game's | 0 mismatches | 0 | every module's counters |
| Start-up against Anvil + anvil-game without Bellows | not slower | 12.76 s against 40.80 s (0.19.0 on Anvil 1.8.0, README) | Benchmark tab or lab series |
| A game year at speed 5 with the default simulation modules, as a set | not slower than the previous release's defaults or the game without Bellows | 1720: 16.86 s against 19.41 s (1.3 defaults) and 27.27 s (without Bellows); 1821: 21.09 / 24.94 / 34.05 s (medians, README, "A year at speed 5") | SIMULATION series |

Not measured yet, so without a budget: the process commit without Bellows at the same
point (the commit above is the whole game's), the cost of the `.tsv` writers of the
simulation modules (one small file every 5 s each), and the first start after a reboot
with Bellows.

The year budget holds for the default set as a whole, since 1.4: a release that changes
which simulation modules are on by default measures the new set against the previous
defaults and the game without Bellows, not each module on its own. A module's own effect
on the year is a separate claim, measured when the README makes it.

A release that goes over a budget says so in its pull request, with the measurement and
why; the budget changes in this file in the same pull request, or the change waits.
