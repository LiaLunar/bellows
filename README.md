# Bellows

[![build](https://github.com/happy-dwarf-team/bellows/actions/workflows/build.yml/badge.svg?branch=main)](https://github.com/happy-dwarf-team/bellows/actions/workflows/build.yml)
[![Latest release](https://img.shields.io/github/v/release/happy-dwarf-team/bellows)](https://github.com/happy-dwarf-team/bellows/releases/latest)
[![Steam Workshop](https://img.shields.io/badge/Steam%20Workshop-Bellows-1b2838?logo=steam)](https://steamcommunity.com/sharedfiles/filedetails/?id=3823747170)
[![License: MIT](https://img.shields.io/github/license/happy-dwarf-team/bellows)](LICENSE)

**Faster Europa Universalis IV start-up, simulation and saves.** An
[Anvil](https://github.com/happy-dwarf-team/anvil) plugin that makes the game
start faster and the years pass sooner at speed 5, without changing game files
or what the game computes: every module gives the game's own answers, only
sooner (see [AI ties and multiplayer](#ai-ties-and-multiplayer) for the one
thing that can still differ). Like a smith's bellows, it gets the forge hot
sooner.

**Install guide:
[anvil.happydwarf.dev/plugins/bellows](https://anvil.happydwarf.dev/plugins/bellows/).**
Players can also subscribe on the
[Steam Workshop](https://steamcommunity.com/sharedfiles/filedetails/?id=3823747170);
the rest of Happy Dwarf Team's tools for EU4 are on
[happydwarf.dev](https://happydwarf.dev/).

Formerly **EU4 Startup Boost** (plugin id `eu4-startup-boost`, up to 0.5.0);
see [Updating from EU4 Startup Boost](#updating-from-eu4-startup-boost).

> Status: public releases before 1.0 — see
> [Releases](https://github.com/happy-dwarf-team/bellows/releases) and
> [CHANGELOG.md](CHANGELOG.md); what is left before 1.0 is in
> [#90](https://github.com/happy-dwarf-team/bellows/issues/90). Bellows 0.19.0 and newer
> needs **Anvil 1.7 or newer** and also runs on the GOG build of EU4; 0.8.0 to
> 0.18.0 need Anvil 1.0 (plugin API 2), 0.7.0 and earlier Anvil 0.2. From
> 0.9.0 it is also published as a Steam Workshop mod (see
> [Steam Workshop](#steam-workshop)).
> Design: [docs/design.md](docs/design.md).

## Measured

EU4 1.37.5 (Steam, Windows 64-bit, 10 UI/graphics/music mods), warm start to
the main menu (`Running application` in `system.log`), original `eu4.exe`,
Anvil 1.5.0 and Bellows 0.10.0 from the release files, Bellows with default
settings and the profiles it learned on an earlier start, 3 interleaved
starts each, every start only after no other program had used the processor
for 15 seconds:

| Setup | Starts | Median |
| --- | --- | ---: |
| Anvil without plugins | 39.24 / 38.68 / 38.44 s | 38.68 s |
| Anvil + anvil-game | 39.74 / 39.22 / 39.51 s | 39.51 s |
| Anvil + anvil-game + Bellows 0.10.0 | 12.79 / 12.24 / 12.28 s | **12.28 s** |

Bellows needs the standard plugin anvil-game, which on its own made no clear
difference (the ranges overlap). The very first start, with the profile
shipped in the release, took 12.80 s. Starts on the same day while other
programs were busy took 40–46 s without Bellows and 12.8–13.6 s with it;
they are left out of the table.

The game's `setup.log`, `error.log` and `system.log` were byte for byte the
same in all 31 starts of that day, with and without Bellows. With default settings
the modules also compare a sample of their answers with the game's own code
(about 45,700 comparisons per start): 0 differences.

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

### Measure your own

Anvil Manager has a **Benchmark** tab (since Anvil 1.5)
(`AnvilManager.exe` in the game folder). It starts the game again and again,
without Bellows and with it in turn; each start quits by itself once the game
is ready, and nothing in your saves, mods or `anvil.toml` changes. The tab
shows the median and spread per setup, and the cold starts (after a reboot)
the series happened to contain, separately. You can then send the report to
the Anvil project, after seeing exactly what is sent, or copy it into a
[performance report](https://github.com/happy-dwarf-team/bellows/issues/new?template=performance_report.yml).
Bellows 0.10.0 and newer adds its module states and counters to the report.

Benchmark times run to `anvil-game.ready`, the end of start-up, so they are
about 1.5 s longer than the times above, which stop at `Running application`.

## Modules

| Module | What it does |
| --- | --- |
| `mimalloc` | Faster memory allocator for the game's C runtime until the main menu (`startup`, the default; later allocations use the Windows heap), or for the whole session (`on`, which made the simulation about 5% slower in the lab) |
| `verifypath_listing` | Answers PhysFS path checks from directory listings |
| `exists_index` | Answers "does this file exist" for the modifier icon check |
| `trigger_intern` | Faster trigger interning at the end of loading (~0.4 s instead of ~0.6 s for the game's own code with the shipped trigger profile; a `config.toml` written by 0.3.0 or earlier said `trigger_intern = "off"`, and the migration keeps that: delete the line from `settings.toml` to use the default `"on"`) |
| `metascript_subst` | Single-pass `$parameter$` substitution in scripted effects |
| `metascript_blocks` | Single-pass resolution of the `[[parameter] ...]` blocks of scripted effects and triggers (matters for mods with large scripted effects, e.g. Anbennar) |
| `open_warmup` | Opens the files the game will need ahead of it on parallel threads, from a profile learned per mod set (meant for the first start after a reboot: 61 → 15 s in the lab's build before the plugin; the plugin's own cold start has not been measured yet) |
| `ram_prefetch` | Reads and unpacks the DLC archive members the game will need into RAM on worker threads (learned per mod set; uses up to 1 GB of extra RAM during start-up, setting `ram_prefetch_budget_mb`) |
| `attr_cache` | Remembers "file not found" answers for game and mod files during start-up (off by default: ~0.1 s left next to the other modules) |
| `token_dict_cache` | Caches the script tokenizer's keyword lookups (~0.4 s) |
| `range_witness` | **Simulation, not start-up.** Answers the country range check from the province that was in range last time instead of walking all of the country's provinces again; same answers as the game's code (lab: a year at speed 5 in 20.6 s instead of 27.6 s). `"verify"` compares every answer with the game's and writes the first mismatches to `range_witness_mismatches.tsv` |
| `modifier_merge` | **Simulation, on by default.** Adds large modifier lists (each country's modifiers to every unit's in the monthly update) with an index instead of a scan per entry: the same entries, order, values and memory growth as the game's code. `"verify"` runs the game's merge and compares. Lab: 10.1 µs a merge against 1.08 µs, so about 0.7 s less in the merge per game year (about 3% of a year at speed 5 in 1720, 2% in 1821); 1.5 million merges compared with the game's, all the same. SIMULATION: the year 1720 in 19.45 s instead of 20.64 s (medians, p = 0.02); in 1821 the same direction, not conclusive |
| `province_modifier_view` | **Simulation, off by default.** Reads one value from a province's modifiers in place instead of copying the whole list first (the daily knowledge refresh of every country does this for each province it owns): the same value as the game's code. `"verify"` runs the game's copy and compares |
| `save_compress` | **Saving, on by default since 0.18.** For compressed saves (`compress_autosave = yes` and the like): compresses each member of the save with the game's own zlib, call for call as the game does, but computes its CRC-32 eight bytes at a time instead of one; the file is byte for byte the game's. `"verify"` runs the game's own compression as well, writes its result and compares the two members byte for byte. Lab, the 1719 save with `compress_autosave = yes`, 6 autosaves per setting in interleaved games: an autosave took 0.831 s (0.823–0.842) instead of 0.873 s (0.871–0.900); `"verify"` found 0 differences in 10 members. `save_compress_parallel = "on"` (off by default) compresses large members (the gamestate) in 1 MiB chunks on several threads instead: a valid archive with the same members, which the game loads and other tools read, but with other compressed bytes than the game's (`"verify"` decodes each member again before writing it). An autosave then took 0.612 s (0.575–0.637), the gamestate 80–95 ms on 7 threads instead of about 300 ms |
| `frame_pacing` | **Simulation, on by default since 0.15.** At the fastest speed, draws frames from inside the daily tick less often while you do nothing (setting `frame_pacing_interval_ms`, 100 ms; the game's own is 50 ms), and at once when a key press, click or wheel turn waits (`frame_pacing_input = "draw"`). The simulation is the same; the map updates about 16 instead of 22 times a second at speed 5 while you do nothing (lab: a year at speed 5 in 20.8 s instead of 23.4 s; a key press at speed 5 handled after 21 ms instead of 51.5 ms in the median, a real key press after 21.6 ms instead of 56.8 ms) |

Diagnostics, off by default: `zip_profile`, `token_dict_profile`,
`range_profile`, `tick_profile` (time of each stage of the daily, monthly and
yearly tick per game day), `army_search_profile` (calls, time and repeated
answers of the AI army search), `score_probe` (how often the inputs of the AI
search's scoring functions repeat within a game day), `monthly_probe` (time of
the monthly tick's loops, modifier merges and random draws per context).
They are tools for the lab: their names, counters and output files may
change in any release.

Supported game builds: EU4 1.37.5 (Steam, Windows 64-bit) and EU4 1.37.5.2
(GOG, Windows 64-bit), with Anvil 1.7 or newer. Every game address
is declared in [`plugin/symbols.toml`](plugin/symbols.toml) for both builds
([design §6d](docs/design.md#6d-the-gog-build-13752-d)); Anvil checks
them against `eu4.exe` before it loads Bellows, and on another build (or any
mismatch) skips the plugin with the reason in `anvil/anvil.log` and the game
starts normally.

## AI ties and multiplayer

Every module gives the game's own answers, so Bellows changes no rule and no
result the game computes. One thing can still come out differently in single
player: when the AI chooses between options with exactly the same score, for
example two leaders with the same pips for one army, or two countries its
diplomacy rates the same, the game breaks the tie by where the two objects lie
in memory. `mimalloc = "startup"` (the default) and Bellows' own memory use
change where the game's objects end up, so such a tie can go the other way
than in a game without Bellows. The choice is as valid as the game's own, just
the other of two equal options, and from then on that game takes its own
course. Two computers without Bellows lay out memory differently too, so the
same happens between them.

In multiplayer this is safe. The host's AI makes these choices and sends them
to every player as commands, so all games stay the same whoever runs Bellows.
In the lab the host ran Bellows 0.17.0 with default settings and the other
computer ran no Bellows: 233 game days without an out-of-sync, with the game's
own out-of-sync check shown to catch a single extra random draw on the host.
Details in [bellows#79](https://github.com/happy-dwarf-team/bellows/issues/79)
and the [design notes](docs/design.md#4-correctness-safeguards-d).

## Settings

Each module is a setting `on`, `off` or (where the module has it) `verify`,
plus `ram_prefetch_budget_mb` (64–8192), `frame_pacing_interval_ms`
(0–1000; 0 keeps the game's own interval), `frame_pacing_input` (`draw`, the
default, or `wait`, the 0.12.0 behaviour) and `save_compress_parallel` (`off`,
`on` or `verify`, used while `save_compress` is on); the defaults, labels and
descriptions are in [`plugin/settings.toml`](plugin/settings.toml). Your
values go into `anvil/data/bellows/settings.toml`, one line each, only those
that differ from the default, for example:

```toml
attr_cache = "on"
ram_prefetch_budget_mb = 2048
```

Every setting applies at the next start. For one start, an environment
variable overrides it: `ANVIL_BELLOWS_<NAME>`, e.g.
`ANVIL_BELLOWS_EXISTS_INDEX=verify` (Anvil logs each override). The plugin
writes its counters to `anvil/anvil.log` when the game is ready.

**From 0.7.0 or earlier:** on its first start Bellows moves your
`anvil/data/bellows/config.toml` to `settings.toml` (values that differ from
the defaults; `[ram_prefetch] budget_mb` becomes `ram_prefetch_budget_mb`)
and keeps the old file as `config.toml.migrated`; `anvil/anvil.log` says what
was moved or left out. The `EU4SB_<NAME>` variables are no longer read: use
`ANVIL_BELLOWS_<NAME>` (a set `EU4SB_` variable is named in the log).

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
  mods of the playset, and whether it happens with Bellows removed.

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
  Edit your files between starts, or turn those modules off while you develop. Nothing of this applies
  after the main menu is up: the start-up modules stop there.
- Learned profiles are per mod set (the enabled mods in `dlc_load.json` and
  the game build), not per mod version. After an update of your mod the
  profile adds the new files on the next start and drops the ones no longer
  used, so the first start after an update may gain a little less.
- Large scripted effects and triggers with `$parameter$` and `[[parameter]]`
  blocks are where `metascript_subst` and `metascript_blocks` save the most
  (about 10 s of Anbennar's start-up); the text they produce is the game's.

## Install / uninstall

Install [Anvil](https://github.com/happy-dwarf-team/anvil#install--uninstall)
1.7 or newer first (it includes the `anvil-game` plugin Bellows needs). Then
download `bellows-windows-x64.zip` from the
[latest release](https://github.com/happy-dwarf-team/bellows/releases/latest)
and extract it into `<EU4>/anvil/plugins/`; it contains the `bellows`
folder (`SHA256SUMS.txt` next to the archive holds its checksum). Delete that
folder to uninstall.

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
archive of the Anvil release `SB_ANVIL_VERSION` in `CMakeLists.txt` (1.4.0),
downloaded by the first configure and checked against its SHA-256. Use an
unpacked SDK archive, or one installed from a local Anvil checkout, with
`-DAnvilPlugin_DIR=<sdk>/cmake`. After changing
`plugin/symbols.toml`, see [CONTRIBUTING.md](CONTRIBUTING.md#game-addresses).

Contributions, the commit convention and the release process: see
[CONTRIBUTING.md](CONTRIBUTING.md).

## Steam Workshop

Bellows can also be published as a Steam Workshop mod; Anvil (since 1.3)
loads plugins from enabled mods after the player approves each version.
The mod is the `workshop/` folder ([`workshop.toml`](workshop.toml)):
`descriptor.mod`, `thumbnail.png` and `anvil/bellows/`, the plugin folder of a
release, which is not in Git. [`workshop/description.md`](workshop/description.md)
is the item's description: every publish replaces the description on Steam with
it. `checksum = "keep"` makes the publish fail if the mod would change the game's
checksum.

- Every pull request runs a dry run of
  [paradox-workshop-publish](https://github.com/happy-dwarf-team/paradox-workshop-publish)
  (`pdx-ws`) in `build.yml`: the job summary shows the files, the descriptor and
  the rendered description. It needs no secrets.
- [`workshop-publish.yml`](.github/workflows/workshop-publish.yml) runs when a
  release is published. It downloads that release's `bellows-windows-x64.zip`,
  checks its build attestation, unpacks `bellows/` into `workshop/anvil/bellows/`
  and publishes the mod, with the release notes as the change note.
- To update the Workshop page between releases, run it by hand on `main`
  with the latest release:
  `gh workflow run workshop-publish.yml --ref main -f tag=vX.Y.Z`. It publishes
  that release's package again with `main`'s `workshop/` folder, and fails when
  `main`'s descriptor version is not that release's (for example while a newer
  release is still a draft). Steam keeps the mod's version when the files are
  unchanged and records no change note, but takes the new description.

Setting it up, once (maintainers):

1. **Install `pdx-ws`** from its
   [releases](https://github.com/happy-dwarf-team/paradox-workshop-publish/releases)
   ([setup guide](https://github.com/happy-dwarf-team/paradox-workshop-publish/blob/main/docs/guide/setup.md)).
2. **Create the Workshop item** on a computer with Steam running and logged in to
   the account that owns EU4 and will own the item. Build the plugin package, put
   its `bellows/` folder into the mod and check the plan first:

   ```powershell
   cmake -S . -B build -G "Visual Studio 17 2022" -A x64
   cmake --build build --config Release
   cmake --install build --config Release --component plugin --prefix dist/package
   Remove-Item workshop/anvil -Recurse -Force -ErrorAction Ignore
   New-Item -ItemType Directory workshop/anvil -Force | Out-Null
   Copy-Item dist/package/bellows workshop/anvil/bellows -Recurse
   pdx-ws plan
   pdx-ws init
   ```

   `pdx-ws init` (client mode, through the Steam client) creates a **private**
   item with the descriptor's name and tags, the preview and the description,
   uploads the mod and writes `remote_file_id` into `workshop/descriptor.mod`.
   Commit that line through a pull request. Make the item public on Steam when it
   is ready: publishing never changes its visibility.
3. **Share the Steam session** with this repository. The session is kept once,
   as the organization secrets `STEAM_SESSION` and `STEAM_USERNAME`, for the
   Happy Dwarf Team repositories that publish to the Workshop (read the
   [security notes](https://github.com/happy-dwarf-team/paradox-workshop-publish/blob/main/docs/guide/security.md)
   and the [organization setup](https://github.com/happy-dwarf-team/paradox-workshop-publish/blob/main/docs/guide/setup.md#several-repositories-in-one-organization)
   first). Add this repository to both secrets, and create a `steam-workshop`
   environment without secrets of its own:

   ```sh
   id=$(gh api repos/happy-dwarf-team/bellows --jq .id)
   gh api --method PUT orgs/happy-dwarf-team/actions/secrets/STEAM_SESSION/repositories/$id
   gh api --method PUT orgs/happy-dwarf-team/actions/secrets/STEAM_USERNAME/repositories/$id
   gh api --method PUT repos/happy-dwarf-team/bellows/environments/steam-workshop
   ```

   In **Settings → Environments → steam-workshop**, add a required reviewer and
   allow only the tags `v*` and the branch `main` (for the manual run): the
   environment gates every publish job.
4. **Turn publishing on:** `gh variable set WORKSHOP_PUBLISH --body true`.

From then on, publishing a release's draft (see
[CONTRIBUTING.md](CONTRIBUTING.md#versions-and-releases)) also publishes the
Workshop mod. When the Steam session expires, the job fails with exit code 3:
export a new one into the organization secret, for every repository that uses
it (Git Bash or PowerShell 7.4+, so the pipe passes the session unchanged):

```sh
pdx-ws session export --user my_steam_name | gh secret set STEAM_SESSION --org happy-dwarf-team --visibility selected --repos bellows,tongs,eu4-anbennar-widescreen-loading-screens
```

## License

MIT — see [LICENSE](LICENSE). Bundles mimalloc (MIT, Microsoft) and zlib (zlib License, Jean-loup Gailly and Mark Adler). Bellows is an
unofficial community project, not affiliated with or endorsed by Paradox Interactive.
