# Bellows

[![build](https://github.com/happy-dwarf-team/bellows/actions/workflows/build.yml/badge.svg?branch=main)](https://github.com/happy-dwarf-team/bellows/actions/workflows/build.yml)
[![Latest release](https://img.shields.io/github/v/release/happy-dwarf-team/bellows)](https://github.com/happy-dwarf-team/bellows/releases/latest)
[![Steam Workshop](https://img.shields.io/badge/Steam%20Workshop-Bellows-1b2838?logo=steam)](https://steamcommunity.com/sharedfiles/filedetails/?id=3823747170)
[![License: MIT](https://img.shields.io/github/license/happy-dwarf-team/bellows)](LICENSE)

**Faster Europa Universalis IV start-up, simulation and saves.** Bellows is an
[Anvil](https://github.com/happy-dwarf-team/anvil) plugin. The game starts faster
and years pass sooner at speed 5. It changes no game files and no rule, and in
`verify` the modules' answers match the game's own. A game can still come out
differently, see [AI ties and multiplayer](#ai-ties-and-multiplayer). Like a
smith's bellows, it gets the forge hot sooner.

**Install guide:
[anvil.happydwarf.dev/plugins/bellows](https://anvil.happydwarf.dev/plugins/bellows/).**
Players can also subscribe on the
[Steam Workshop](https://steamcommunity.com/sharedfiles/filedetails/?id=3823747170).
The team's other tools for EU4 are on [happydwarf.dev](https://happydwarf.dev/).

Bellows needs **Anvil 1.7 or newer** and EU4 1.37.5 (Steam, Windows 64-bit) or
1.37.5.2 (GOG, Windows 64-bit). Anvil checks every game address against `eu4.exe`
before it loads Bellows ([`plugin/symbols.toml`](plugin/symbols.toml),
[design §6d](docs/design.md#6d-the-gog-build-13752-d)). On another build, or on
any mismatch, it skips the plugin, gives the reason in `anvil/anvil.log`, and the
game starts normally.

Settings, their values and the Anvil requirement stay the same in every 1.x
release (see [Compatibility](#compatibility)). What 1.0 was checked against is in
[#90](https://github.com/happy-dwarf-team/bellows/issues/90). See also the
[releases](https://github.com/happy-dwarf-team/bellows/releases),
[CHANGELOG.md](CHANGELOG.md) and the design notes in [docs/design.md](docs/design.md).

Formerly **EU4 Startup Boost** (plugin id `eu4-startup-boost`, up to 0.5.0): see
[Updating from EU4 Startup Boost](#updating-from-eu4-startup-boost).

## Measured

EU4 1.37.5 (Steam, Windows 64-bit, 10 UI/graphics/music mods), warm start to
the main menu (`Running application` in `system.log`), original `eu4.exe`,
Anvil 1.8.0 and Bellows 0.19.0 from the release files (checksums and build
attestations checked), Bellows with default settings and the profiles it
learned on an earlier start, 5 interleaved starts each with the order turned
every round, 3 November 2026:

| Setup | Starts | Median |
| --- | --- | ---: |
| Anvil without plugins | 40.99 / 39.73 / 40.81 / 47.69 s | 40.90 s |
| Anvil + anvil-game | 41.36 / 40.63 / 40.48 / 40.80 / 46.91 s | 40.80 s |
| Anvil + anvil-game + Bellows 0.19.0 | 12.76 / 12.49 / 12.47 / 13.31 / 12.81 s | **12.76 s** |

So about 28 s less, a start a little over three times as fast. Bellows needs
the standard plugin anvil-game, which on its own made no difference. One
start without plugins (55.05 s) ran while a build used the same processor
and is left out; the fifth round without Bellows (46.91 s and 47.69 s) was
slower for a reason not found and is kept. Unlike the 0.10.0 series, the
starts did not wait for an idle processor. The first start with the profile
shipped in the release was not timed cleanly in this series.

The game's `setup.log`, `error.log` and `system.log` were the same, apart
from times and counts, in every round with and without Bellows. With default
settings the modules also compare a sample of their answers with the game's
own code (about 45,700 comparisons per start): 0 differences, and no module
`FAILED`.

Earlier: Bellows 0.10.0 on Anvil 1.5.0 (October 2026, 3 starts each, every
start after 15 s of an idle processor) took 12.28 s against 39.51 s with
anvil-game alone.

### A big mod set: Anbennar

Anbennar with Proper 2K UI Enhanced and P2KE Patch - Anbennar (the Workshop
items as of 26 October 2026), warm start to the main menu, original
`eu4.exe`, Anvil 1.5.2 and Bellows 0.14.0 from the release files, Bellows with
default settings and the profiles it learned on earlier starts, 5 interleaved
starts each, in a separate game profile with no other game running:

| Setup | Starts | Median |
| --- | --- | ---: |
| Without Anvil | 85.32 / 87.19 / 84.64 / 85.29 / 86.01 s | 85.32 s |
| Anvil + anvil-game + Bellows 0.14.0 | 22.42 / 21.81 / 21.83 / 22.47 / 23.01 s | **22.42 s** |

About 10 s of that gain comes from `metascript_blocks` (0.14.0), which matters
for Anbennar's large scripted effects and makes no difference on the 10-mod set
above. The game's logs were the same in all five pairs of starts. The first
start after a reboot was not measured on this mod set.

### A year at speed 5

Every default setting together, against the same game without Bellows: EU4 1.37.5
(Steam), original `eu4.exe`, the save of Russia on 17 March 1719, warmed up to
1 January 1720, then the year 1720 at speed 5 with the game's main thread on one
processor and 8 workers in both of its pools. Anvil 1.9.0, anvil-game and Tongs (which
drives the run) were in both setups, and Bellows was a build of the code that
became 1.2. One game per run, interleaved, each after a check that the processor
was idle, 5 November 2026:

| Setup | Year 1720 | Median |
| --- | --- | ---: |
| Without Bellows | 26.50 / 27.11 / 24.70 / 28.95 s | 26.80 s |
| With Bellows, default settings | 18.72 / 21.63 / 20.92 / 20.07 s | **20.49 s** |

So about a quarter less, and the two ranges do not overlap. All modules
reported 0 differences from the game's own answers. Runs in which an event held the
game paused were thrown out, as the method requires: 5 of the 16 in this series,
none of them in these two setups. A late-game year has no such comparison yet: the runs
of the 1821 save without Bellows were lost to a dialog. This is one machine and one
save: your years will differ with your processor and your campaign.

### Loading a save

"Continue" on the main menu until the game is ready, EU4 1.37.5 (Steam), original
`eu4.exe`, Anvil 1.9.0, a build of Bellows from before 1.2 with default settings
apart from the settings shown, each run a new start of the game, interleaved, 4 November 2026:

| Save | Setup | Runs | Median (range) |
| --- | --- | ---: | --- |
| 1821, text, 64.5 MB, 10 mods | Bellows 1.1 defaults | 5 | 8.55 s (8.32–8.88) |
| | + `verifypath_session` | 5 | 6.79 s (6.72–7.09) |
| | + `history_add` as well | 5 | **6.27 s** (6.13–6.92) |
| 1450, text, 40 MB, vanilla | Bellows 1.1 defaults | 3 | 4.43 s (4.28–4.86) |
| | + both | 3 | **4.22 s** (4.15–4.40) |

The game's `setup.log`, `error.log`, `system.log` and `game.log` were the same
apart from times and counts, and so were the game's random count and its own
values for three countries, the player and three provinces read after the load.
A compressed save, a first load after a reboot, multiplayer and the GOG build were
not timed.

### Measure your own

Anvil Manager (`AnvilManager.exe` in the game folder) has a **Benchmark** tab
since Anvil 1.5. It starts the game again and again, without Bellows and with it
in turn. Each start quits by itself once the game is ready, and nothing in your
saves, mods or `anvil.toml` changes. The tab shows the median and spread per
setup, and separately any cold starts (after a reboot) the series contained. You
can send the report to the Anvil project, after seeing exactly what is sent, or
copy it into a
[performance report](https://github.com/happy-dwarf-team/bellows/issues/new?template=performance_report.yml).
Bellows 0.10.0 and newer adds its module states and counters to the report.

Benchmark times run to `anvil-game.ready`, the end of start-up, so they are
about 1.5 s longer than the times above, which stop at `Running application`.

## Modules

| Module | What it does |
| --- | --- |
| `mimalloc` | Faster memory allocator for the game's C runtime until the main menu (`startup`, the default; later allocations use the Windows heap), or for the whole session (`on`, which made the simulation about 5% slower in the lab) |
| `verifypath_listing` | Answers PhysFS path checks from directory listings. With `verifypath_session = "on"` (**loading, on by default since 1.2**) it goes on after the main menu is up, so loading a save finds its textures sooner; a listing is then read again once it is 30 s old. `verifypath_listing = "verify"` compares every answer, also after the main menu: 0 differences in about 650,000 answers on three saves. Lab: see [Loading a save](#loading-a-save) |
| `exists_index` | Answers "does this file exist" for the modifier icon check |
| `trigger_intern` | Faster lookup of each trigger in the game's trigger profile at the end of loading (~0.4 s instead of ~0.6 s for the game's own code with the shipped trigger profile; a `config.toml` written by 0.3.0 or earlier said `trigger_intern = "off"`, and the migration keeps that: delete the line from `settings.toml` to use the default `"on"`) |
| `metascript_subst` | Single-pass `$parameter$` substitution in scripted effects |
| `metascript_blocks` | Single-pass resolution of the `[[parameter] ...]` blocks of scripted effects and triggers (matters for mods with large scripted effects, e.g. Anbennar) |
| `open_warmup` | Opens the files the game will need ahead of it on parallel threads, from a profile learned per mod set (meant for the first start after a reboot, when every first open of a file is slow: 61 → 15 s in the lab's build before the plugin; the first start after a reboot with the plugin has not been measured, so Bellows makes no claim for it) |
| `ram_prefetch` | Reads and unpacks the DLC archive members the game will need into RAM on worker threads (learned per mod set; uses up to 1 GB of extra RAM during start-up and at most a quarter of the memory free at that moment, setting `ram_prefetch_budget_mb`) |
| `attr_cache` | Remembers "file not found" answers for game and mod files during start-up (off by default: ~0.1 s left next to the other modules) |
| `token_dict_cache` | Caches the script tokenizer's keyword lookups (~0.4 s) |
| `range_witness` | **Simulation, not start-up.** Answers the country range check from the province that was in range last time instead of walking all of the country's provinces again; same answers as the game's code (lab: a year at speed 5 in 20.6 s instead of 27.6 s). `"verify"` compares every answer with the game's and writes the first mismatches to `range_witness_mismatches.tsv` |
| `modifier_merge` | **Simulation, on by default.** Adds large modifier lists (each country's modifiers to every unit's in the monthly update) with an index instead of a scan per entry: the same entries, order, values and memory growth as the game's code. `"verify"` runs the game's merge and compares. Lab: 10.1 µs a merge against 1.08 µs, so about 0.7 s less in the merge per game year (about 3% of a year at speed 5 in 1720, 2% in 1821); 1.5 million merges compared with the game's, all the same. SIMULATION: the year 1720 in 19.45 s instead of 20.64 s (medians, p = 0.02); in 1821 the same direction, not conclusive |
| `port_table` | **Simulation, off by default.** Rebuilds a country's table of the nearest port from one list of its ports instead of walking its provinces and relations again for every province of the map; the table and the distances stay the game's |
| `province_modifier_view` | **Simulation, off by default.** Reads one value from a province's modifiers in place instead of copying the whole list first (the daily knowledge refresh of every country does this for each province it owns): the same value as the game's code. `"verify"` runs the game's copy and compares |
| `army_search_memo` | **Simulation, off by default.** In the AI's daily search for each army, in a path search and when a unit's moves from a province are listed, works out once whether modifier `0xE0` lets the army ignore forts' zones of control and what its speed modifier is, instead of for every move (each of these builds a modifier list from the army's regiments): the same answers. `"verify"` runs the game's check for every move and compares. Lab: 183 million answers compared with the game's on the 1719 save and an 1821 save, 0 differences; 99.6–99.8% of the checks answered from the memo. Its effect on a year at speed 5 is not measured yet |
| `pops_pacing` | **Simulation, off by default.** At the fastest speed, runs the game's Paradox online-service callbacks every 500 ms instead of twice a frame; with nothing to deliver, each run makes the main thread sleep 1 ms. The simulation is the same, the callbacks arrive later. Multiplayer, pauses and other speeds keep the game's rate. `"verify"` runs them as the game does and counts what would be left out. Lab: its counters show about 1 s less sleep per game year; whether a year gets shorter by as much is not measured yet (two series without the lab's fixed conditions could not tell) |
| `save_compress` | **Saving, on by default since 0.18.** For compressed saves (`compress_autosave = yes` and the like): compresses each member of the save with the game's own zlib, call for call as the game does, but computes its CRC-32 eight bytes at a time instead of one; the file is byte for byte the game's. `"verify"` runs the game's own compression as well, writes its result and compares the two members byte for byte. Lab, the 1719 save with `compress_autosave = yes`, 6 autosaves per setting in interleaved games: an autosave took 0.831 s (0.823–0.842) instead of 0.873 s (0.871–0.900); `"verify"` found 0 differences in 10 members. `save_compress_parallel = "on"` (off by default) compresses large members (the gamestate) in 1 MiB chunks on several threads instead: a valid archive with the same members, which the game loads and other tools read, but with other compressed bytes than the game's (`"verify"` decodes each member again before writing it). An autosave then took 0.612 s (0.575–0.637), the gamestate 80–95 ms on 7 threads instead of about 300 ms |
| `transfer_skip` | **Loading, on by default since 1.1.** In single player, "Continue" of a text save no longer compresses the whole file at level 9 for a multiplayer transfer nobody receives; the game loads the same file as before. Hosted multiplayer is left as it is. `"verify"` keeps the game's copy and checks that it is the save, that the load does not use it and that nothing is sent. Lab, interleaved runs, 5 per setting, click on "Continue" to the game ready: the 64.5 MB save of 1821 8.95 s (8.55–9.23) instead of 10.91 s (10.78–11.06), a 40 MB vanilla save of 1450 4.47 s (4.35–4.99) instead of 5.84 s (5.70–6.14). Compressed saves have no such copy and do not change |
| `history_add` | **Loading, on by default since 1.2.** Adds an entry the game just read to a dated list without first walking the whole list to see whether that new entry is in it; the list comes out the same. The gain grows with the square of the list, so it depends on the mods: about 0.5 s of "Continue" with the lab's 10 mods (38,000 entries, 154 million list steps), almost nothing on vanilla (4,300 entries, 2 million steps). `"verify"` lets the game walk and compares: 0 differences. See [Loading a save](#loading-a-save) |
| `frame_pacing` | **Simulation, on by default since 0.15.** At the fastest speed, draws frames from inside the daily tick less often while you do nothing (setting `frame_pacing_interval_ms`, 100 ms; the game's own is 50 ms), and at once when a key press, click or wheel turn waits (`frame_pacing_input = "draw"`). The simulation is the same; the map updates about 16 instead of 22 times a second at speed 5 while you do nothing (lab: a year at speed 5 in 20.8 s instead of 23.4 s; a key press at speed 5 handled after 21 ms instead of 51.5 ms in the median, a real key press after 21.6 ms instead of 56.8 ms) |

Diagnostics, off by default: `zip_profile`, `token_dict_profile`,
`range_profile`, `tick_profile` (time of each stage of the daily, monthly and
yearly tick per game day), `army_search_profile` (calls, time and repeated
answers of the AI army search), `score_probe` (how often the inputs of the AI
search's scoring functions repeat within a game day), `monthly_probe` (time of
the monthly tick's loops, modifier merges and random draws per context),
`knowledge_probe` (how often a country's knowledge refresh leaves the country
as it was, per call site).
They are tools for the lab: their names, counters and output files may
change in any release (see [Compatibility](#compatibility)).

## AI ties and multiplayer

Bellows changes no rule, and `verify` found the modules' answers equal to the
game's. A game can still come out differently in single player, and not all of
it is explained yet.

The known cause is a tie: when the AI chooses between options with exactly the same score, for
example two leaders with the same pips for one army, or two countries its
diplomacy rates the same, the game breaks the tie by where the two objects lie
in memory. `mimalloc = "startup"` (the default) and Bellows' own memory use
change where the game's objects end up, so such a tie can go the other way
than in a game without Bellows. The choice is as valid as the game's own, just
the other of two equal options, and from then on that game takes its own
course. Two computers without Bellows lay out memory differently too, so the
same happens between them.

Ties do not explain everything. In the lab, runs of one vanilla save of 1444
sometimes became a different game with the same seed, also with no Bellows at
all. In every case we traced, one AI command landed on the other side of the
game's daily tick, so the order depends on timing. Bellows makes that happen
more often: in a planned series at speed 2, 11 of 30 runs with default settings
went the other way, against 1 of 30 without Bellows (sign test over paired
blocks, p = 0.002), each time at the same point of that save. That is one save,
one speed and one computer, and we don't know how often it happens over a whole
campaign. Both orders are normal games.

In multiplayer the known ties are safe. The host's AI makes these choices and sends them
to every player as commands, so every game gets the same choices whoever runs Bellows.
In the lab the host ran Bellows with default settings and the other computer ran
no Bellows: 233 game days on 0.17.0, with the game's own out-of-sync check shown
to catch a single extra random draw on the host, and 231 on the code of 1.3.0, at
speed 3 because the other computer could not keep up at 5. Neither run went out
of sync. Both used one save of 1444, and neither had Bellows on the other computer.
Details in [bellows#79](https://github.com/happy-dwarf-team/bellows/issues/79)
and the [design notes](docs/design.md#4-correctness-safeguards-d).

## Settings

Each module is a setting: `on`, `off` or, where the module has it, `verify`.
There are five more:

| Setting | Values |
| --- | --- |
| `ram_prefetch_budget_mb` | 64–8192 |
| `frame_pacing_interval_ms` | 0–1000; 0 keeps the game's own interval |
| `frame_pacing_input` | `draw` (default) or `wait`, the 0.12.0 behaviour |
| `save_compress_parallel` | `off`, `on` or `verify`, used while `save_compress` is on |
| `verifypath_session` | `off` or `on`, used while `verifypath_listing` is on |

The defaults, labels and descriptions are in
[`plugin/settings.toml`](plugin/settings.toml). Your values go into
`anvil/data/bellows/settings.toml`, one line each, only those that differ from the
default, for example:

```toml
attr_cache = "on"
ram_prefetch_budget_mb = 2048
```

`frame_pacing_interval_ms` and `frame_pacing_input` apply at once when you change
them in the Anvil panel (Ctrl+Shift+F12) while `frame_pacing` is on; 0 ms turns
its effect off until you set another value. Every other setting applies at the
next start. For one start, an environment variable
overrides it: `ANVIL_BELLOWS_<NAME>`, for example
`ANVIL_BELLOWS_EXISTS_INDEX=verify`. Anvil logs each override. The plugin writes
its counters to `anvil/anvil.log` when the game is ready.

**From 0.7.0 or earlier:** on its first start Bellows moves your
`anvil/data/bellows/config.toml` to `settings.toml` and keeps the old file as
`config.toml.migrated`. `anvil/anvil.log` says what was moved or left out. The
`EU4SB_<NAME>` variables are no longer read, so use `ANVIL_BELLOWS_<NAME>`. These
migrations stay in every 1.x release.

### Compatibility

From 1.0 on, every 1.x release keeps these as they are, so a
`settings.toml` written for 1.0 means the same in any later 1.x:

- the name of every module setting and the meaning of its values (`on`,
  `off`, `verify`, and `startup` for `mimalloc`);
- `ram_prefetch_budget_mb`, `frame_pacing_interval_ms`, `frame_pacing_input`,
  `save_compress_parallel` and `verifypath_session`, with their ranges and
  values;
- the oldest Anvil Bellows needs (Anvil 1.7).

A release that renames or removes one of these, gives a value another
meaning or needs a newer Anvil is 2.0. A module's default may still change in
a minor release, after the checks in [docs/release-gate.md](docs/release-gate.md);
the changelog says so. Not covered: the diagnostics (`zip_profile`,
`token_dict_profile`, `range_profile`, `tick_profile`, `army_search_profile`,
`score_probe`, `monthly_probe`, `knowledge_probe`), which are lab tools, and the wording of the
lines and counters Bellows writes to `anvil/anvil.log`, the fields it adds to
the Benchmark report and the modules' `.tsv` files, which may change in any
release.

## Troubleshooting

- **Was Bellows loaded?** `anvil/anvil.log` has one line per start with every
  module and its mode, e.g. `modules: mimalloc=startup, open_warmup=on, …`. A
  module shown as `FAILED` turned itself off (the line above it says why) and
  the game went on without it. When Anvil skips the whole plugin (another game
  build, an unapproved Workshop version), the log and the overlay's **Mods** tab
  (Ctrl+Shift+F12) say so, and the game starts as without Bellows.
- **Turn one module off** to see whether it causes a problem: in the overlay's
  settings for Bellows, or with a line such as `range_witness = "off"` in
  `anvil/data/bellows/settings.toml`; it applies at the next start. For one
  start only, `ANVIL_BELLOWS_RANGE_WITNESS=off` does the same.
- **Not faster?** `open_warmup` and `ram_prefetch` learn what a mod set needs
  during one start and pay off from the next; each mod set has its own profile
  in `anvil/data/bellows/profiles/`. Deleting that folder makes Bellows learn
  again. The Benchmark tab (see [Measure your own](#measure-your-own)) gives a
  fair number.
- **A module counted `mismatches` above 0** in `anvil/anvil.log` or in its
  `.tsv` file in `anvil/data/bellows/`: one of its answers differed from the
  game's. The modules that compare a sample in `"on"` mode then go back to the
  game's code for the rest of the session (the log says so). Please report it
  with the log and the `.tsv` file; it is the report the comparisons exist for.
- **Reporting a problem:** a
  [bug report](https://github.com/happy-dwarf-team/bellows/issues/new?template=bug_report.yml)
  with `anvil/anvil.log` of that start, `anvil/data/bellows/settings.toml`, the
  mods of the playset, and whether it happens with Bellows removed. To ask
  first, post in `#help-and-bugs` on the team's
  [Discord server](https://discord.happydwarf.dev/) with the Bellows tag; what
  the team confirms there is tracked in an issue.

## For mod authors

- Nothing to do: Bellows reads your mod as the game does and gives the game's
  own answers. `setup.log`, `error.log` and `system.log` are the same with and
  without it (the lab compares them on every start-up series), so a script
  error your mod has shows up as it would without Bellows.
- **Files must not change while the game starts.** `verifypath_listing` and
  `exists_index` answer path checks from directory listings read once during
  start-up, and `attr_cache` (off by default) remembers "not found". A file
  that a tool writes into the game or mod folders while the game is loading
  may be missed, and one it deletes may still be found, until the next start.
  Edit your files between starts, or turn those modules off while you develop.
- **After the main menu is up** the start-up modules stop, with one exception:
  with `verifypath_session` (on by default since 1.2), a file added or deleted in
  the game or mod folders counts after at most 30 s. Turn it off while you develop
  if you need new files to count at once. Mods in the game's user folder
  (Documents) are not affected.
- Learned profiles are per mod set (the enabled mods in `dlc_load.json` and
  the game build), not per mod version. After an update of your mod the
  profile adds the new files on the next start and drops the ones that are
  gone, so the first start after an update may gain a little less.
- Large scripted effects and triggers with `$parameter$` and `[[parameter]]`
  blocks are where `metascript_subst` and `metascript_blocks` save the most
  (about 10 s of Anbennar's start-up); the text they produce is the game's.

## Install / uninstall

[Install Anvil](https://anvil.happydwarf.dev/docs/install/) 1.7 or newer first. It
includes the `anvil-game` plugin Bellows needs. Then
[install the plugin](https://anvil.happydwarf.dev/docs/install-plugins/) one of
these ways:

- click **Install** next to Bellows on the **Catalog** tab of Anvil Manager (Anvil
  1.9 or newer). To uninstall, click **Remove** on the **Plugins** tab.
- subscribe to the
  [Steam Workshop mod](https://steamcommunity.com/sharedfiles/filedetails/?id=3823747170)
  and approve it. To uninstall, unsubscribe.
- extract `bellows-windows-x64.zip` from the
  [latest release](https://github.com/happy-dwarf-team/bellows/releases/latest)
  into `<EU4>/anvil/plugins/`. `SHA256SUMS.txt` next to the archive holds its
  checksum. To uninstall, delete the `bellows` folder.

If something does not work, see
[Troubleshooting](https://anvil.happydwarf.dev/docs/troubleshooting/).

### Updating from EU4 Startup Boost

Delete `<EU4>/anvil/plugins/eu4-startup-boost/` and extract Bellows as above.
On its first start Bellows moves your settings and learned profiles from
`anvil/data/eu4-startup-boost/` to `anvil/data/bellows/` (and the settings
from `config.toml` to `settings.toml`, see [Settings](#settings)). Anvil 1.0
does not load the old plugin (plugin API 1); Bellows mentions the leftover
folder in `anvil/anvil.log`. The exported `EU4SB_*` counters keep their
names.

## Building

Visual Studio 2022 (or Build Tools) with the C++ workload, CMake ≥ 3.28, Git
(fetches mimalloc 3.5.3, toml++ 3.4.0 and the Anvil SDK).

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --component plugin --prefix dist
```

The plugin folder as Anvil loads it is `build/plugin/bellows/`. The Anvil SDK
(the `AnvilPlugin` CMake package, its headers and `anvil.exe`) is the SDK
archive of the Anvil release `SB_ANVIL_VERSION` in `CMakeLists.txt` (1.7.0),
downloaded by the first configure and checked against its SHA-256. Use an
unpacked SDK archive, or one installed from a local Anvil checkout, with
`-DAnvilPlugin_DIR=<sdk>/cmake`. After changing
`plugin/symbols.toml`, see [CONTRIBUTING.md](CONTRIBUTING.md#game-addresses).

Contributions, the commit convention and the release process: see
[CONTRIBUTING.md](CONTRIBUTING.md).

## Steam Workshop

Bellows is also a Steam Workshop mod. Anvil 1.3 and newer loads plugins from
enabled mods after the player approves each version. The mod is the `workshop/`
folder, and a published release publishes it. How that works, and how to set it
up: [docs/workshop.md](docs/workshop.md).

## License

MIT, see [LICENSE](LICENSE). Bundles mimalloc (MIT, Microsoft) and zlib (zlib License, Jean-loup Gailly and Mark Adler). Bellows is an
unofficial community project, not affiliated with or endorsed by Paradox Interactive.
