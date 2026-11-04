# Bellows — design

Status: 1.0 (§2 lists every shipped module, §5 the tests, §7 the decided `mimalloc` question; what 1.0 was
checked against is [#90](https://github.com/happy-dwarf-team/bellows/issues/90)). Decisions marked **D** are
agreed; **Q** are open.
Background, measurements and reverse-engineering notes come from the private
research repository (`eu4-engine-lab`); this repository contains production
code only.

## 1. Goals

- Port the start-up optimizations proven in the lab to an Anvil plugin
  (`phase = "early"`, `affects_gameplay = false`).
- Same effect as the lab build: game state, logs and loaded content identical
  to vanilla; only time changes.
- No game file is modified; all changes are in-memory patches through the
  Anvil host API on symbols declared in `plugin/symbols.toml`, which Anvil
  verifies against the game before the plugin loads.
- Every module can be switched off individually; any failure turns that
  module off and the game continues.

## 2. Modules (D)

| Module | What it does | Lab result |
| --- | --- | --- |
| `ram_prefetch` | Reads the files the game will read (archives and loose files from a manifest) into RAM on worker threads; serves the game's zip/file reads from memory | 31 → ~24.8 s |
| `attr_cache` | Negative cache of `GetFileAttributesExW`/`A` for not-found paths during start-up (existing files always hit the real API) | part of the above |
| `exists_index` | Answers the modifier-picture verifier's "file exists" queries from directory listings | 24.6 → 19.6 s |
| `verifypath_listing` | Answers PhysFS `verifyPath` stats from directory listings | → 13.3 s |
| `mimalloc` | Replaces the game's CRT heap with mimalloc for the whole process (`on`) or until the game-ready event (`startup`) | 13.8 → 11.8 s |
| `trigger_intern` | Skips redundant trigger-interning work at the end of loading (empty map), linear algorithm otherwise | tail −0.8 s |
| `metascript_subst` | Single-pass `$param$` substitution in scripted effects/triggers | −0.75 s |
| `metascript_blocks` | Single-pass resolution of the `[[param] ...]` blocks of scripted effects/triggers | Anbennar: 34.4 → 24.0 s; lab playset: no change |
| `frame_pacing` | Simulation, on by default since 0.15: at speed 5, frames drawn from inside the daily tick at most every 100 ms instead of 50 ms while no input waits, at once for a waiting press | year 1720: 23.4 → 20.8 s; key press 51.5 → 21 ms |
| `open_warmup` | Opens the files the game will need on parallel threads (learned list per mod set) so first-open costs after a reboot or antivirus update are paid off the critical path | cold start 61 → 15 s (lab build; not measured with the plugin) |
| `token_dict_cache` | Caches the script tokenizer's keyword lookups during start-up | −0.4 s |
| `range_witness` | Simulation: answers the country range check from the province that was in range last time (§6a) | year 1720: 27.6 → 20.6 s |
| `modifier_merge` | Simulation: adds large modifier lists with an index instead of a scan per entry (§6a) | year 1720: 20.64 → 19.45 s |
| `province_modifier_view` | Simulation, off: reads one value of a province's modifiers without copying the list (§6a) | exact, no gain shown |
| `save_compress` | Saving: the game's own zlib with a faster CRC-32 (the game's bytes); optionally in parallel (§6a) | autosave 0.873 → 0.831 s; parallel 0.612 s |
| `transfer_skip` | Loading in single player: no level 9 copy of a text save for a transfer nobody receives (§6a) | "Continue" of a 64.5 MB save 10.91 → 8.95 s |

The table's start-up modules are in the order they were ported; §6a has the
results per module and release.

Diagnostics, off by default and meant for the lab (`zip_profile`,
`token_dict_profile`, `range_profile`, `tick_profile`, `army_search_profile`,
`score_probe`, `monthly_probe`), are shipped as settings; their output files
and counters may change in any release. The lab's deeper tools (`file_trace`,
deep API counters, heap sampling) are not shipped.

## 3. Changes needed when moving from the lab (D)

- The lab build patched a copy of `eu4.exe` (added import, miniz call sites)
  and ran it from a folder of junctions. In the plugin every executable change
  becomes a runtime change through the Anvil host (plugin API 2: call
  redirects, import replacements, pointer swaps, function hooks and one block
  replacement); Anvil owns the stubs.
- Configuration: Anvil settings (`plugin/settings.toml`; the player's values
  in `anvil/data/bellows/settings.toml`, `ANVIL_BELLOWS_<NAME>` overrides for
  the lab harness). Up to 0.7.0 it was `anvil/data/bellows/config.toml` and
  `EU4SB_<NAME>` (§6b).
- Writable data lives in `anvil/data/bellows/`. The prefetch
  manifest and the warm-up list become one **access profile** (see §3a)
  instead of a lab-generated manifest.
- Readiness comes from the `anvil-game` plugin (`anvil-game.ready`); modules
  stop their start-up behaviour at that point as in the lab.
- Build identity: `build` of `plugin/symbols.toml`; every address and expected
  byte is declared there once, so a future port touches that file.

## 3a. Access profiles: shipped base layer + learning per mod set (D)

`ram_prefetch` must know which files and archive ranges the game will read;
`open_warmup` which files it will open. Both are the same knowledge, kept as
an access profile: entries (source, path or archive entry, first-use order,
bytes read), each attributed to its **source** — the base game or a specific
mod.

1. **Base layer, shipped with the plugin per game build**: generated in the
   lab from a start without mods; contains only paths, sizes and order (no game
   content). Works from the very first start on any machine.
2. **Per-mod-set profile, learned**: key = enabled mods (from `dlc_load.json`
   in the game's user folder: `-userdir`, then `userdir.txt`, then Documents)
   + game build. Every start records what the game actually opened/read and
   merges it into the profile (new entries appended in first-use order;
   entries unused for N consecutive starts are dropped). First start of a new
   mod set: base layer + definitions-only "all" list of the mods' folders
   (filtered as in the lab warm-up), then refined by learning.
3. **Per-mod layers (later, same format)**: because each entry records its
   source, layers per mod (key: mod id + version/update time) can be extracted
   and composed for a new mod set made of known mods — base layer minus files
   the mods override, plus each mod's layer — without a learning start. A mod
   update changes its key; the old layer is used, validated by file size and
   time, until the new one is learned.
4. Memory budget for prefetch (default 1 GB, setting `ram_prefetch_budget_mb`; the lab used 2 GB) and prefetch order
   = first-use order.

Start with 1 and 2; keep the source attribution from day one so 3 needs no
format change.

## 4. Correctness safeguards (D)

- Modules that replace a game computation keep the lab's modes: `verify`
  (compute both, keep the game's result, count mismatches) and on (sampled
  comparison every 256th call, disable on mismatch).
- Startup caches answer only what is provably identical and fall back to the
  original otherwise; all switch off when the game is ready (`anvil-game.ready`).
- Counters per module are written to the plugin's section of `anvil.log` once
  the game is ready (requests, hits, fallbacks, mismatches).
- Same answers do not mean the same heap. Two AI decisions of EU4 1.37.5 sort
  (score, object pointer) pairs with a comparator that breaks equal scores by
  the pointer: `0xB3450` picks leaders for armies (`assign_leader`) and
  `0x137AC0` (`ai_foreignminister.cpp`) ranks countries (#79, a scan of the
  whole `.text` in the lab; virtual calls not followed). mimalloc `startup`
  moves the game's pre-ready blocks off the process heap by design, and
  Bellows' own allocations share that heap, so objects of a save loaded after
  ready sit elsewhere than without Bellows and such a tie can go the other
  way. On the lab's 1640 save the first difference was one `assign_leader`
  between two leaders with the same pips (28 March 1640), repeatable per
  configuration. A separate arena for Bellows' allocations would not bring the
  vanilla choice back (mimalloc `startup` changes the heap on purpose), so the
  behaviour is documented instead (README, "AI ties and multiplayer"). In MP
  the host's AI decides and its commands go to every player: in the lab's MP
  test (host with 0.17.0 defaults, client without Bellows, 233 days) the
  client received 21,011 commands from `server.cpp`, including 73
  `assign_leader`, and there was no out-of-sync; a lab plugin that draws one
  extra random number on the host made the client go out of sync within 2
  days. The code every machine runs (daily tick, command execution) showed no
  address ordering that changes game state. For determinism pairs the lab
  breaks these ties by object id with a lab-only plugin.

## 5. Testing (D)

- Native unit tests in `tests/` (one program per test, run by `ctest`): the
  cores of the modules that compute something (access profiles, path rules,
  the ZIP reader, mimalloc heap, trigger interning, metascript substitution and
  blocks, token dictionary cache, range witness and its statistics, frame
  pacing, modifier merge, province modifier view, save compression, the
  diagnostics' cores), settings and their migration, the rename from EU4
  Startup Boost, `bellows.dll` loaded against a stand-in host
  (`test_plugin_load`, with the benchmark service) and `anvil check plugin`
  (`symbols_lock`). `verifypath_listing` and `exists_index` are tested on real
  folders (with a junction), apart from their hooks. `open_warmup` is tested on
  real folders too (the list a start chooses, the opens it learns, the open
  ahead of the game), and `ram_prefetch` without the game (the budget with
  waiting workers, a member's states when the game opens it first, reads served
  from a buffer, DEFLATE members with zlib in place of the game's `tinfl`).
  `attr_cache`, off by default, has its path rules tested, not its hooks.
- In-game validation with the lab harness: start-up series against vanilla,
  game logs compared, save loading checked, and `"verify"` sessions for the
  modules that replace a game computation (start-up and simulation). What
  every release runs before its draft is published, and the budgets, are in
  [release-gate.md](release-gate.md).

## 6. Stages

1. Skeleton plugin on Anvil stage 1 (manifest, config, logging, counters).
2. Port modules in order of independence: `mimalloc`, `verifypath_listing`,
   `exists_index`, `trigger_intern`, `metascript_subst`, `open_warmup`,
   `attr_cache`, `ram_prefetch` (largest; depends on miniz call-site patches).
3. Measure against the lab build; release candidate.

## 6a. Results of stage 2, first slice (0.1.0)

Modules ported: `mimalloc`, `verifypath_listing`, `exists_index`,
`trigger_intern`, `metascript_subst` (`src/modules/`, addresses only in
`src/build.hpp`). Patches go through Anvil: IAT slots and the vtable slot via
`patch_bytes` (expected pointer values), the verifyPath site via
`patch_bytes` + a stub in `alloc_near_image`, the trigger and metascript
sites via `redirect_call`. Plugin: 10 patches, loaded in ~42 ms at the entry
point.

Findings while porting (both also apply to the lab build):

- **`trigger_profile.txt`**: the game ships it in its root folder (332 KB);
  at start-up `0xB9B5A0` reads it and fills the trigger-interning map (6870
  entries). The lab's junction copies listed root files explicitly and missed
  it, so every lab measurement ran with an empty map. With the real file the
  module takes its hash-consing path for all 2.35 M calls; verify run: 0
  differences.
- **Write directory**: `verifypath_listing` in verify mode found 2 differences
  per start, both `logs/error.log` / `logs/error_old.log` in the PhysFS write
  directory (Documents\…\Europa Universalis IV), which the game renames during
  start-up after the listing was read. Both listing-based modules now never
  answer for the write directory or anything inside it (`PHYSFS_getWriteDir`);
  after the change: 0 differences.

Verify start (`verify` for all four modules that have it): verifypath
932,405, exists_index 16,074, trigger_intern 2,347,067, metascript_subst
320,905 comparisons, 0 mismatches.

Timing (lab smoke harness `scripts/anvil-smoke.ps1`, junction copy with the
original `eu4.exe`, loading screens pre-opened, 3 interleaved starts):
Anvil without plugins 34.96 / 35.50 / 34.70 s (median 34.96 s); with the
plugin 15.65 / 15.38 / 15.40 s (median 15.40 s). `setup.log`, `error.log`,
`system.log` identical between the two.

### Per-module contribution (0.2.0 work)

Leave-one-out series `sb-loo1` (all modules on, then each off; 2 interleaved
starts each; ±1–2 s run-to-run noise): all 16.69 s; without
`verifypath_listing` 25.04 s; without `mimalloc` 18.29 s; without
`exists_index` 16.58 s; without `metascript_subst` 16.71 s; without
`trigger_intern` 15.92 s.

Direct cost measurement in one start (verify mode times our code and the
game's original separately): `trigger_intern` — original 682 ms for 2.35 M
calls, ours ~1263 ms, i.e. **slower** with the real (non-empty) map;
`metascript_subst` — original 860 ms, ours ~64 ms. `trigger_intern` was
therefore **off by default** (kept with its verify mode; a cheaper
canonicalization could revisit it, but at most ~0.7 s is available).

Cheaper index (on by default since this change; a `config.toml` written by
an earlier version keeps its `"off"`): the structure table holds only
the map keys and their substructures (arena + open addressing, built with the
index); a node's shape is only looked up, and a miss marks it "foreign"
(answer 0, and its parents are foreign too), since a key consists solely of
key substructures. Inner nodes are memoized in a flat pointer table presized
from the registry's two set sizes (+0x10, +0x50); leaves skip the memo.
Measured in the game (lab junction copy, warm starts): verify 0 mismatches
over 2,347,067 calls; our part ~487 ms against 628 ms for the original; in
on mode 417–436 ms summed per call. An interleaved on/off series of start-ups
(5 vs 7 starts) gave 12.25 ± 0.50 s vs 12.37 ± 0.36 s to `Running
application`: consistent with ~0.2 s, but within the noise.

Hook overhead: timing every call (two QPC reads and a locked add) and the
interlocked hot-path counters cost ~0.1–0.2 s offline. A locked instruction
waits for the walk's outstanding cache misses, and a timed call cannot overlap
its misses with its neighbours', so per-call timing also inflated the result.
The hook now counts with plain updates (the game interns from one thread
under its own mutex) and logs the span from the first to the last call
(QPC at every 16th entry; the game's loop overhead included). Offline 474 →
360 ms; in the game the span is 381–395 ms (verify: 0 mismatches). The index
is released at the ready report.

### open_warmup and access profiles

Ported with the learned profile per mod set
(`anvil/data/bellows/profiles/<build>-<dlc_load hash>.txt`), the
shipped base layer `plugin/profiles/eu4-1.37.5-steam-win64.txt` (10 695 game
files, generated from a learned profile by keeping only `@\` game-folder
entries — an approximation of a start without mods, because the lab's mod set
was not changed) and the filtered "all" list for mods. Learning uses the
game's `CreateFileW` import through Anvil `replace_import` and saves at the
game-ready event. Warm starts: first start without a profile — "all" list
13 619 entries in 1.86 s; with the base layer — 11 413 entries in 78 ms;
learned profile — 11 503 entries in 76 ms; start ~15.1 s in each case. The
cold-start effect (lab: 61 → 15 s) still has to be measured with the plugin
after a reboot.

### token_dict_cache

The script tokenizer (`0x170E700`) looks every word token up in a global
case-insensitive ternary search tree (`0x2326720`, `0x6E4880`): one CRT
`tolower` and one node per character. `token_dict_profile` (diagnostic)
measured ~8.7 M lookups per start, 16.7 nodes per successful lookup and a
215 k-node tree that grows during loading (get-or-add appends to the run-time
token vector `0x242B8A0`). The tokenizer's call site is redirected to a
per-thread open-addressing cache keyed by the token with ASCII letters
lowercased; hits stay valid until the tree is rebuilt, misses only for the
run-time token count read before the lookup. On by default and kept out of the
written `config.toml`, so existing files get it too.

Lab: verify start 0 mismatches over 8,447,519 cache answers; offline on the
real keys 261–304 vs 704–723 ms (shuffled) and 137–142 vs 233–246 ms (runs);
two interleaved start-up series −0.43 s (8/8 pairs) and −0.32 s (5/8), −0.38 s
over 16 pairs (Anvil ready time); game logs identical in all starts.

### range_profile (diagnostic, simulation)

The first module aimed at the simulation, not the start. In the lab's profile
of a year at speed 5 (1720, Russia 1719 save) 17% of the main thread was one
chain: the province day (`0x9F52E0`) checks whether a country can still reach
a province (`0x3532C0` → `0x3520D0`), and `0x3526B0` takes the minimum
distance from the country's flagged provinces to the target, calling the
province distance `0x2D2520` for each, with an early exit below the threshold.

`range_profile` (off by default, not in the written `config.toml`) hooks
`0x3526B0` and `0x2D2520` through Anvil `hook_function` only to observe: every
check is recorded with its country, province, value, in-range answer, the
distance calls inside it, its time and its caller. The game is usually closed
without a clean exit, so a thread writes `range_profile*.tsv` to the data
folder every 5 s: a summary, per game day, the 5000 busiest (country,
province) pairs and the callers.

Lab, `sim-range-8-a` trial 2 (only `range_profile` on, 8 workers per pool):
the year 1720 took 27.73 s, of which 5.07 s (18.3%) inside the checks; 143,482
checks (393 per day), 12.5 M distance calls (87 per check, up to 1497); every
check came from `0x3520D0`. Over the whole session (655 game days) 1597 pairs
of 85 countries were checked, each about once a day; their values changed only
65 times (52 pairs) and the in-range answer twice. A memo per pair would
answer almost every check if it can tell when the inputs change: the country's
province list, the province flags and lists the check reads, the range
modifier and the range bonus.

### range_witness (simulation)

`range_profile` showed what dominated the check: 97.5% of the distance calls
went into walks that ended "in range", after 92.6 provinces on average, and the
walk usually ended at the same province day after day. The answer 0x3520D0
uses is only `value <= threshold`, and the walk stops at the first province
below the threshold; so any province below the threshold gives the same answer.

`range_witness` redirects the call at `0x35214C` (in `0x3520D0`). Per
(country, target province, threshold) it keeps the province that ended the last
walk (the witness), from the country's own list or from a related country (the
second walk over the relation records at `+0x1510`, parallel to the country
array at `root+0x1CE8`, qualified through the record's list at `+0x128` and the
flag `+0x11F`, with the related country's own bonus through `+0x1420` and
`0x7ACEC0`). A check first evaluates only the witness with the game's own
functions and formula: still in the list, bit `0x10` at `+0x958`, the country in
the province's list at `+0x490`, `min(0x2D2520, straight line) * 1000 / (1000 +
bonus) < threshold`. Then it writes the value the original would write when
stopping there. Otherwise the original walk runs and a new witness is looked
up; when none exists (the answer came from a value exactly at the threshold),
the lookup waits 30 game days. Nothing is kept across loads beyond what the
session builds. `verify` always runs the original and compares.

Counters go to `range_witness.tsv` in the data folder every 5 s. In `verify`
the first 32 cases of each kind of mismatch (`verify_disagree`: a valid witness
but the original says out of range; `verify_value_differs`: the original
stopped at the witness and wrote another value) are also kept: copied into a
fixed array inside the check when detected (no allocation; later cases are only
counted), and appended to `range_witness_mismatches.tsv` by the same writer
thread. The file is started with only its header when the module installs in
`verify`, so an empty table means no mismatch in the session; `on` neither
creates it nor keeps anything. One row per case:

| Column | Meaning |
| --- | --- |
| `kind` | `disagree` or `value_differs` |
| `day`, `date` | game day (days since 1.1.0, 365-day years) and the same as `year.month.day` |
| `country`, `country_id` | the checked country's pointer and its id (bits 32–47 of `+0x20`, the index into the country array) |
| `target`, `threshold` | target province id and the threshold of the key |
| `witness_related`, `witness_owner_id`, `witness_province` | the witness: `-1` for the country's own list, else the index of the relation record, and the id of the country whose list holds it |
| `witness_value` | the module's value at the witness, before the original's rounding to thousands |
| `original_value` | the value the original wrote (`-1` when it returned null) |
| `first_below_related`, `first_below_province` | where the walk stops by the module's formula, evaluated after the original (for `value_differs` this is the witness by definition) |

The country tag is not written: no offset for it is validated in
`symbols.toml`. The province at which the original itself stopped is not
observable from its result; `first_below_*` is the module's reconstruction.

Lab (EU4 1.37.5, Russia 1719 save, 8 workers per pool, only this module on,
Tongs-driven year 1720 at speed 5; lab `docs/simulation-profile.md`):

- `verify`, two full sessions with the first walk only: 361,622 witness
  answers, all equal to the original's, and in 359,338 of them the original
  stopped at the witness and wrote the same value; 0 differences. With the
  second walk (one session, ~200 game days): 72,233 answers, 0 differences,
  72,042 same values.
- `on`: 92% of the checks answered from the witness (236,981 of 258,100 in a
  session, 56,197 from a related country); time inside the checks 1.2 s per
  session instead of ~9 s.
- Interleaved series (ABBA, one launch per trial): `on` 20.68, 20.48, 21.02,
  20.05 s for the year, `off` 27.29, 27.58, 27.91 s (one `off` trial rejected
  on an event pause): −7.0 s (−25.5%), no overlap. Main-thread CPU 19.1 s
  instead of 25.5 s per year.

- `verify`, one session from 17 March 1719 to 1 January 1727 (both walks, the
  game driven year by year at speed 5, two event pauses resumed): 1,106,039
  checks, 1,021,812 witness answers, 0 disagreements; in 1,016,682 the
  original stopped at the witness and wrote the same value, 0 different.

On by default since then, like `token_dict_cache` (kept out of the written
`config.toml`, so existing files get it too). Still open: other saves, mod
sets and much later dates.

### frame_pacing (simulation)

On by default since 0.15 (off and experimental in 0.12–0.14). At speed 5 the lab's profile of year 1720 put about 16% of the
main thread's samples inside frames the game draws from within the daily tick
(lab `docs/simulation-profile.md`, "Klatki rysowane w trakcie ticku").
`0x769EA0` is called about 40 times between the stages of the tick and draws a
frame (`0x14C25A0` with `outer` = 0) when the time since the last one exceeds
base `[0x245B070]` (1.0) times the table `0x1C33E18` indexed by the speed at
`+0x1E08` (0.02 for speeds 1-4, 0.05 for speed 5; the index is the speed minus
one, checked in the game).

A mid hook after the clock read (`0x769F78`) replaces the threshold in xmm6 at
speed 5 with `frame_pacing_interval_ms` (100 ms by default, chosen from the
series below; never with a
shorter one than the game's own); the game still
decides with its own clock and last-frame time. A second mid hook on the
drawing branch (`0x769F9A`) counts the frames per speed into
`frame_pacing.tsv`. `frame_pacing_interval_ms = 0` keeps the game's threshold
and only counts. The simulation does not read these frames; what changes is how
often the screen and input are handled inside long days.

With `frame_pacing_input = "draw"` (the default since 0.13) frames are skipped
only while no input waits. A waiting key press, click or wheel turn gets a
frame at the next check, whatever the game's threshold (at most one every
10 ms; the frame takes the input out of the queue), so the game reacts sooner
than without the module; mouse motion gets the game's own threshold, so moving
the mouse costs no more frames than the game draws itself. Input waits either
in the main thread's Windows queue (`GetQueueStatus` with `QS_KEY`,
`QS_MOUSEBUTTON` and `QS_MOUSEMOVE`; SDL reads that queue only when a frame
pumps events, and the check runs on the main thread, which owns the window) or
in SDL's own queue (`SDL_PeepEvents` of the statically linked SDL 2.0.4 at
`0x17467A0` with no buffer and `SDL_PEEKEVENT` counts the events of a type
range; events pushed by another plugin, such as Tongs, are there). The queues
are probed at most every 2 ms of the game's clock (checks in between reuse the
answer), with one `SDL_PeepEvents` over keys, buttons and motion and a second
one only when something waits. A first version that kept only the
game's threshold while input waited was no faster than `"wait"`: input almost
always arrived when the time since the last frame was still below 50 ms, so
the game's own rule would not have drawn either, and the outer frame between
days handled it. `"wait"` keeps the 0.12.0 behaviour: the longer interval also
while input waits. `frame_pacing.tsv` counts the probes (`input_probes`), the
frames drawn for a press (`input_press`) and the checks with mouse motion
waiting (`input_motion`).

Lab measurement of `"draw"` (lab `docs/bellows-frame-pacing.md`, "Reakcja na
wejście"; EU4 1.37.5, Russia 1719 save, Bellows defaults plus this module):

- Input latency at speed 5 (Tongs `pause`, 60 presses per session, normal
  Process Lasso rule): median 21 ms (p90 82 ms) against 51.5 ms (p90 92 ms)
  without the module, 120 presses each, sessions ACCA with the final build;
  an earlier ABCCBA series gave 21 / 50.5 / 72.5 ms for `"draw"`, without the
  module and `"wait"`.
- Year 1720 at speed 5 (SIMULATION, 8 workers per pool, interleaved): 20.8 s
  (4 trials, 19.1–21.4) against 23.4 s without the module (6 trials,
  21.1–33.9), -10.9%, permutation p = 0.09; `"wait"` 19.4 s (7 trials). In
  the same series `"draw"` and `"wait"` differed by +2.3% (p = 0.94): the
  probes cost no measurable time.
- Real key presses (Space through `SendInput` to the game window, 53 presses
  each, sessions ABBA, the 0.13.0 release build): median 21.6 ms (p90 66 ms)
  against 56.8 ms (p90 114 ms) without the module; the module drew 27-33
  frames per session for the presses it found in the Windows queue.
- Frame rate at speed 5 with no input (Tongs `frames`, 30 s windows, Russia
  1719 and a 1 January 1821 save): 16.4 and 15.8 frames a second against 22.5
  and 22.7 without the module. The frames between days stay (one per day);
  the frames inside the tick drop from 8.5-9.4 to 1.5-2.6 a second, and the
  longest gap between frames stays the same (575 against 585 ms in 1719).

Lab (EU4 1.37.5, Russia 1719 save, 8 workers per pool, Bellows defaults plus
this module, year 1720 at speed 5 through Tongs; lab `docs/bellows-frame-pacing.md`):

- Live check at 250 ms: 5 frames inside the tick in ~10 s at speed 5 instead
  of ~13 per second in an earlier check without the module; frames between
  days were 20 per second (9 in that check), so the screen still updates about
  20 times a second.
- Two interleaved series (one launch per trial, both arms with the hooks
  installed; the control arm keeps the game's threshold): 250 ms median
  19.23 s for the year (n = 8, 18.72-25.23; seven of eight at 18.72-19.68),
  the game's threshold 21.19 s (n = 7, 19.98-25.05): -1.96 s (-9.2%,
  permutation test of the medians p = 0.049); main-thread CPU -7.8%. The
  second series alone: -6.9% (p = 0.11). Six trials were rejected on event
  pauses, in both arms. About 22 frames inside the tick per session at speed 5
  instead of 210-340.
- Input latency at speed 5 (Tongs `pause`: Space pushed into the game's SDL
  queue until the pause flag is set, 10 ms polling; normal CPU affinity; ABBA
  sessions, 120 presses per arm): 250 ms median 76 ms, p90 189 ms, max 401 ms;
  the game's threshold median 52 ms, p90 89 ms, max 183 ms. Input is handled
  later, mostly in the tail: with 250 ms it waits for the frames between
  days. While paused there is no difference.
- Shorter intervals (one more series each, few valid trials): 100 ms -8.5%
  (n = 2) and 150 ms -9.1% (n = 3) for the year against the pooled control,
  as 250 ms; key press median 72.5 and 78.5 ms, p90 135 and 159 ms. The median
  cost stays (input waits for the frames between days whatever the
  interval); the tail shrinks with the interval.
- 100 ms, interleaved series of its own (ACCAACCAACCA): 20.17 s for the year
  (n = 4, 19.35-21.91) against 22.80 s with the game's threshold (n = 5,
  22.53-23.94): -11.6%, p = 0.024, no overlap; main-thread CPU -10.3%. Hence
  the default of 100 ms: the gain of 250 ms with the shortest latency tail.
- Not measured: time until the reaction is on screen, other saves and dates.
  Experimental and off by default, since the median latency cost stays.

### pops_pacing (simulation)

The main loop calls `POPS_RunCallbacks` (`pops_api.dll`, Paradox's online services) twice a frame:
from `0x14C5810` and from `CPopsWrapper::Update` (`0x14ADA90`). When no socket is ready the library
ends that call in `Sleep(1)` on the main thread. At the fastest speed a frame follows every game day,
so these sleeps add up to a few percent of a year in which the main thread does nothing.

The module replaces the import of `POPS_RunCallbacks` in `eu4.exe` (no game address, no change to
`pops_api.dll`). A hook on the daily tick notes the time of the last game day and the speed the game
ran it at. A call of the pump waits when all of this holds: single player (anvil-game's session
event), speed index 4, a game day less than 1 s ago, and that context was pumped less than 500 ms ago.
Every other call goes to the game's function as it came, so the menus, a paused game and the
slower speeds keep the game's rate. Each of the two contexts gets its own clock: both also run
the library's work behind the callbacks (HTTP transfers), which only comes later, never not at all.

The simulation is the same because only the moment the callbacks run changes, and nothing in the
daily tick waits for them. `"verify"` calls the game's function every time and counts what `"on"` would
leave out; `pops_pacing.tsv` has the calls, what was left out and the time in the pump.

### modifier_merge (simulation)

On by default. `0x8EA420` adds the (id, value) entries of one modifier list
to another: into a dense list (static ids `0..0x285` at their own index) a
static id goes in place; every other id is looked up by a linear scan of the
whole destination and appended when missing, doubling the capacity; at the end
the source is recorded in the destination's vector at `+0x38`. The monthly
unit update adds each country's dense list (about 700 entries) to every unit's
sparse list, about 44,000 compared entries a call, 4,850 calls a month.

The hook takes the calls whose scans would compare at least 4,096 entries
(counting the entries appended during the call) and merges with an index of
the destination's ids (a stamped array per thread), then records the source the
way the game does. The result is the same entries in the same order with the
same values, count and capacity; the buffer grows at the same points with the
game's operator new and the list's own release call. Calls outside what the
index covers (ids outside `0..65535`, a dense list with a dynamic id in a static
slot, the source being the destination) and small calls go to the game's
function. `modifier_merge_core.hpp` has the game's loop written out
(`MergeReference`); the test compares it with `MergeFast` on 4,000 random
merges of every shape. `"verify"` runs the game's merge and compares a copy
merged by `MergeFast`; `"on"` does that for every 1,024th call and goes back to
the game's function for good after a mismatch. Counters and the time of both
paths: `modifier_merge.tsv` every 5 s.

Lab (normal Process Lasso rule; lab `docs/simulation-profile.md`, "Tick
miesięczny"): Russia 1719 to 1730, 822,919 merges compared in `"verify"`, 0
mismatches, 8.26 s in the game's function; in `"on"` 815,897 merges took
0.89 s (10.1 µs a call against 1.08 µs), about 0.68 s a game year of ~23 s.
The 1821 autosave in observe mode, 300 s: 697,631 compared, 0 mismatches,
7.44 s against 0.78 s.

SIMULATION (lab, ABBA x3 per series, the same build in both arms, defaults
against defaults plus `modifier_merge`): the year 1720 took 19.45 s instead of
20.64 s (medians, 5 and 6 valid runs, -5.7%, permutation p = 0.02); the year
from 1 March 1821 took 26.25 s instead of 30.17 s (6 and 6, -13%, p = 0.40, the
late-game years vary by ±3 s between runs, so not conclusive). The module gives
the game's own lists and checks itself against the game's merge in the running
game, so it is on by default.

### province_modifier_view (simulation)

Off by default. `0x2FFC60` (country, out, province, culture) copies the
province's modifier list (`+0x998`) into a sparse list: every entry in order
except the placeholders with id `0x286`, growing the buffer from 8 entries by
doubling, plus the list's vector of sources with its reference counts. It then
calls `0x3003B0`, which takes the country's modifiers and, through `0x300720`,
reads one value from the copy: the first entry with id `0x266` or `0x267`
(which one depends on the culture), or nothing when the country accepts the
culture. `0x300720` hands the copy on only to the value object of that modifier
(registry `+0xE680` / `+0xE6E0`) when one is set. The daily knowledge refresh
of a country (`0x93FE30` > `0x93FB80`, `country_knowledge.cpp`) calls
`0x2FFC60` for every province the country owns; AI code calls it as well.

Filtering out id `0x286` keeps the order, so for any other id the first entry
of the copy is the first entry of the province's own list. The hook calls the
game's `0x3003B0` itself with a sparse view of the province's entries (dense
flag 0, the province's entry pointer and count, the rest zero) instead of the
copy, while both value objects are unset; with either set, and for calls
without a province, the game's function runs. Nothing the game keeps changes:
the copy only lived for the call. `province_modifier_view_core.hpp` has the
copy and the read written out; the test compares the value read from the copy
and from the view on 4,000 random lists of both shapes. `"verify"` runs the
game's function and compares the value read through the view; `"on"` does that
for every 1,024th call and goes back to the game's function for good after a
mismatch. Counters and the time of both paths: `province_modifier_view.tsv`
every 5 s.

Lab (lab `docs/simulation-profile.md`, "RefreshKnowledge"): `0x93FE30` is about
9% of the tick without frames in 1720 and 1821; in the samples of the 1821
autosave `0x2FFC60` is 2.2% of the main thread (0.7% in 1720), mostly its copy
loop and the allocations.

Lab, game sessions (normal Process Lasso rule, 300 s each, lab
`results/province-modifier-view-verify.csv`): 12,168,481 values compared with
the game's function, 0 mismatches; the value objects of `0x266` and `0x267`
were unset in every call. The game's function took 0.83 µs a call on the 1719
save and 0.80 µs on the 1821 autosave, the view 0.11 µs and 0.07 µs. With about
0.59 million calls a game year in 1720 and 1.37 million in 1821 that is about
0.4 s and 1.0 s less a game year (by the module's clocks, over all threads;
some calls may run on the AI pool's threads). No SIMULATION series yet.

It stays off by default for now: without the copy the hot path allocates less,
which changes the heap layout the way mimalloc and `range_witness` do, and AI
ties broken by address (#79, section 4) could then go another way in single
player, every answer still being correct. On by default only after a
determinism pair with `-randomlog` with and without the module (as for 0.15.0,
with the lab's tie-break plugin so that address ties do not count) and a
SIMULATION series.

### army_search_memo (simulation)

Off by default. The AI searches the province graph once a day for every army
(`0x1022B0`, called only from `0xEAB50` in `ai_army.cpp`; the lab's
`army_search_profile` session counted one search per AI army a day, about half
of the AI's time). For each move it considers, the scoring functor's method
`0x96F860` ends in `0xD45250`, which checks whether a fort's zone of control
blocks the move. Its first step is `0xD40AE0(army, 0xE0, 0)`: whether modifier
`0xE0` applies to the army. That looks at the leader's modifiers, a second
modifier list of the army, a modifier list `0xD4C6E0` builds for the call from
the army's regiments (`0xD4CA70`: a loop over the regiments, a merge and a
scale into a temporary list that is freed again), the country's cached
modifiers (`0x93D750`, `kraj+0x28C0`) and, through a callback, each
sub-unit. None of it depends on the move; all of it is the same army's state.

The module redirects the call of `0x1022B0` (`0xEBB5F`) to mark a search on the
current thread and the call of `0xD40AE0` in `0xD45250` (`0xD45291`). Inside a
search the first call runs the game's function and later calls for the same
army get its answer; a search entered inside another one gets its own memo, and
calls outside a search (`0xD45250` has two more callers) always run the game's
function. Why the answer cannot change during a search: the search runs in the
AI's daily task (`0x77D8A0`, in parallel on the pool threads, where the game
also forbids random draws), which reads game state and hands its decisions on
as commands; `0xD40AE0` and the functions it calls only read (the country
getter writes a log line if the modifiers are being recalculated, and returns
the same empty list then). That is a statement about the code, so `"verify"`
runs the game's function for every call and compares it with the memo, and
`"on"` does that for every 1,024th answer and goes back to the game's function
for good after a mismatch. `army_search_memo_core.hpp` holds the memo; its test
covers the scope (outside a search, another army, nested searches). Counters,
calls of the game's function and their time: `army_search_memo.tsv` every 5 s.

Lab, statically and from samples (lab `docs/simulation-profile.md`, "AI army
search: the zone-of-control flag"): the path `0x96F860 > 0xD45250 > 0xD40AE0`
held 22.9% of the samples inside the search in the lab's 1720 sampler session,
and the search about half of the AI's daily work.

Lab, game sessions (normal Process Lasso rule, 300 s at speed 5 each, the 1719
save and the 1821 autosave in observe mode): 183.3 million answers of the memo
compared with the game's function (`verify` and the samples of `"on"`), 0
mismatches. A search makes 375 (1720) to 670 (1821) calls, of which the memo
answered 99.6% and 99.8% in `"on"`; one call of the game's function took about
0.2 µs (with two clock reads). That is about 2.2 s of thread time per game year,
roughly 6–7% of the AI's work; the year itself (bounded by the slowest country's
AI) is not measured yet, so the module stays off until a SIMULATION series.
`0xD45250` also gets 42–104 million calls per session from outside the search
(`0xD4A9E0`, `0xD4B270`), which the module leaves alone.

### save_compress (saving)

On by default since 0.18.0 (`save_compress_parallel` stays off). With `compress_autosave = yes` (and for every other compressed
save) the game writes a ZIP archive in memory and compresses each member on the
calling thread: `gamestate`, `meta`, `ai` and, with a random New World, its
archive. One function does all of it, `0x153C790` (writer, input, buffer, size,
crc, level), called from the member writer `0x153B9A0` and for `ai` straight
from `0x5CE5F0`: `deflateInit2_(level 3, 8, -15, 9, 0)` of the zlib 1.2.8
linked into `eu4.exe` (`0x181FC70`), then, while input is left, it seeks the
input stream to what deflate consumed, reads at most half the buffer, calls
`deflate(Z_NO_FLUSH)` with the other half as output, runs CRC-32 over the
consumed bytes one byte per turn (table `0x1D91CD0`) and writes the new output;
then `deflate(Z_FINISH)` until the stream ends. The callers write the local
header before it and rewrite it with the sizes and CRC afterwards (no data
descriptor). Lab (lab `docs/autosave-cost.md`): compression adds about 0.35 s
to every autosave of a 62–65 MB gamestate, and the screen stands for about
0.9 s instead of 0.55 s.

The hook replaces `0x153C790` as a whole. `"on"` runs `CompressEntry`
(`save_compress_core.hpp`): the function call for call with the game's own
`deflateInit2_`, `deflate` and `deflateEnd`, the same reads, seeks, buffers and
flushes (and the same failure when the end of the stream does not fit the
output half), with a slice-by-8 CRC-32 instead of the byte loop. The CRC is a
function of the bytes, so the file is byte for byte the game's. `"verify"` runs
the game's own `0x153C790` first, on the same input into a capture writer
(only its write slot, `+0x50`, is expected), then `CompressEntry` with the
game's byte loop as well into another; the game's writes, CRC and result go
into the archive, and a difference in the bytes, the CRC, the result or between
the two CRC loops is counted and logged. It compares the module with the game
on the very state being saved, which two games cannot do (two runs of the same
save do not reach the same state). `symbols.toml` checks the function's entry, its zlib calls, the
CRC loop and the finish loop, so another build of the function is refused.

`save_compress_parallel = "on"` (separate, off by default) compresses members of
at least 4 MiB differently (`save_compress_parallel.hpp`): the member is read
whole, cut into 1 MiB chunks, and each chunk is compressed on one of up to 8
threads (the process's CPUs) with the zlib 1.3.1 linked into Bellows, level and
parameters as the game's, the 32 KiB before it as its dictionary
(`deflateSetDictionary`), ending with `Z_SYNC_FLUSH` (the last with
`Z_FINISH`), as pigz does. Joined in order, the pieces are one valid raw DEFLATE
stream; the CRC comes from the chunks' CRCs (`crc32_combine`). The member keeps
its name, method 8 and headers, with its own CRC and sizes, but its compressed
bytes differ from the game's, so a file compared byte for byte with one written
without the option differs. `"verify"` decodes the joined stream with zlib's
inflate and compares it with the input before anything is written. When
anything fails before the first byte is written (memory, zlib, a decode
mismatch), the member goes the `"on"` way. Every call appends a row (path,
sizes, chunks, threads, total and CRC time) to `save_compress.tsv`.

Offline (lab, without the game; `tools/save_compress_offline.cpp`, built with
`-DSB_BUILD_SAVE_TOOLS=ON`): on three compressed saves written by the game
(autosaves from the 1719 and 1821 runs with `compress_autosave = yes`, a 1444
save), `CompressEntry` with zlib 1.2.8 at level 3 gives every member byte for
byte as in the file, with work buffers of 50,000,000 bytes, 20 MiB, 4 MiB and
64 KiB (except the 64 KiB buffer on the 1719 autosave: the end of the stream
does not fit, and the function returns 0 as the game's would; the game passes
50,000,000 bytes or the rest of its 90 MiB arena). The CRC-32 of the 64 MB
gamestate took 22 ms slice-by-8 against 85 ms byte by byte. In parallel the
gamestate took 57 ms on 7 threads against about 320 ms on one; it decodes to
the same bytes with zlib's inflate, Python's `zipfile` and Windows `tar`, with
the CRC in the headers, and is 0.1–0.24% smaller than the game's.

In the game (lab `docs/autosave-cost.md`, 2 November 2026): the Russia save of
17 March 1719 with `compress_autosave = yes`, a separate game profile, the
process on CPUs 1–7, observer mode at speed 5 to 10 January 1722 (autosaves of
1720–1722), Bellows with default settings otherwise. Interleaved games with
`save_compress` off, `"on"` and `"on"` with `save_compress_parallel = "on"`,
two games of three autosaves each per setting; the time from the game's
`autosave.tmp` to its rename:

| Setting | Autosaves | Median | Range | Gamestate member |
| --- | ---: | ---: | --- | --- |
| off | 6 | 0.873 s | 0.871–0.900 s | – |
| `"on"` | 6 | 0.831 s | 0.823–0.842 s | 296–302 ms |
| `"on"`, parallel `"on"` | 6 | 0.612 s | 0.575–0.637 s | 80–95 ms, 7 threads |

The ranges do not overlap. `"verify"` (one game each) compared every member
with the game's own function on the same input: 10 members with
`save_compress = "verify"` (three autosaves and one level 9 member, below) and
the small members with both settings on `"verify"`, 0 differences; every
parallel member decoded to its input. Offline, the members of all 15 autosaves
written with the module off, `"on"` and `"verify"` are byte for byte what the
game's function gives with zlib 1.2.8. A save written with
`save_compress_parallel` loaded in a game without Bellows (single player, the
game then ran a month); Python's `zipfile`, Windows `tar`, NanaZip and Etch's
save reader read it with the CRCs of its headers. Starting a game from a
plain-text save also goes through the function once, at level 9, for a copy that
only a multiplayer client reads (64.5 MB in about 2.4 s on the game's function,
0.5 s in parallel; `transfer_skip` below). Not tested: multiplayer (joining, host
transfer) with a parallel save, ironman and cloud saves.

### transfer_skip (loading)

On by default since 1.1.0. Starting a game from a save (`CGameSetup::Load`,
`0x1132CA0`; "Continue" and the lobby's Play go this way) reads the whole file
and, on a session that is a server or a dummy, hands it to
`CFileTransferManager::AddFile` (`0xD7AAD0`), which makes it an entry of the
transfer manager. For a file that is not a ZIP yet, `AddFile` first deflates it
at level 9 into a second buffer, `SFileDesc::CompressedData` ("Compressing save
for transfer" in the game's log): about 2.4 s for a 64.5 MB save, on the main
thread, before anything is parsed. That buffer has one reader,
`CFileTransferManager::SendFile` (`0xD7B2A0`), which sends it to a client instead
of `Data` when it exists and sets the receiver's "compressed" flag from it. The
game's own load does not use it: the start command (`0x279310`) calls
`CGameSetup::LoadFile` (`0x110D760`) with the entry's `Data` and the flag 0, with
or without a copy. A save that is compressed already has no copy, so every path
after `AddFile` runs without one in the unmodified game.

A single-player session is the dummy session (`CSession::_bIsDummy`, `+0x55`,
next to `_bIsServer`, `+0x54`; the constructor sets one of the two from the
session's basic type). When `AddFile` is asked to compress and its session (the
handler's slot 1, as `CFileTransferManager::GetSession`) is a dummy and not a
server, the hook calls the game's `AddFile` with `compress = false`. Nothing else
changes: the entry has its bytes and checksum, the load gets the same bytes and
flag. Hosted multiplayer is a server session and keeps its copy, compressed by
the game or with `save_compress_parallel`; a client never reaches `AddFile`. If a
session did turn out to send an entry without a copy, `SendFile` sends `Data`
with the flag off, as it does for a compressed save: a larger transfer, not a
wrong one.

`"verify"` keeps the game's copy and checks each thing `"on"` relies on: the copy
is an archive of one member that decodes to exactly `Data` with its CRC-32, the
next `LoadFile` gets that entry's `Data` with the flag 0, and `SendFile` is not
called in a dummy session (the last two with hooks that `"on"` does not install).
Every `AddFile` asked to compress appends a row (action, session bytes, sizes,
time) to `transfer_skip.tsv`.

The names come from the `eu4.pdb` of the GOG build; the Steam build has the same
code and offsets. `symbols.toml` checks both functions' entries, the game's own
reads of the session bytes in `CGameSetup::Load` and of the handler in `AddFile`.

In the game (lab `docs/autosave-cost.md`, 4 November 2026): `eu4.exe` 1.37.5
(Steam), Anvil 1.9.0, Tongs 0.26.0, the process on CPUs 1–7, a warm start to the
main menu per run, "Continue" clicked through Tongs; the time from the click to
Tongs' readiness (the game in its session, an outer frame run, paused). Five
interleaved runs per arm on each save: the 1821 save (text, 64,542,833 bytes,
10 mods) and a vanilla autosave of 1450 (text, 40,240,990 bytes, no mods).
Bellows with default settings otherwise; "no Bellows" is Anvil, anvil-game and
Tongs.

| Save | No Bellows | `transfer_skip` off | `save_compress_parallel` on, `transfer_skip` off | `transfer_skip` on |
| --- | --- | --- | --- | --- |
| 1821 | 11.26 s (10.84–11.69) | 10.91 s (10.78–11.06) | 8.97 s (8.88–9.18) | **8.95 s** (8.55–9.23) |
| Vanilla 1450 | 6.11 s (5.70–6.17) | 5.84 s (5.70–6.14) | not run | **4.47 s** (4.35–4.99) |

The ranges of on and off do not overlap. `AddFile` itself took 2,459 ms and 1,385 ms
with the copy and 66 ms and 41 ms without it (what remains is the game's CRC-32
of the file). On the 1821 save the module and the parallel compression come out
the same to within the spread, but the parallel way changes bytes a client
receives and stays opt-in, while this one changes nothing that is read. `"verify"`
on both saves (two sessions each, on a dummy session, not a server): the copy decoded
to the save with its CRC, the load got the entry's `Data` with the flag 0, `SendFile`
was not called, 0 mismatches. After the load, with the module on and off, `game.log`
and `error.log` are the same once digits are replaced, `RandomCount` is the same in
all runs of a save, and Tongs' reads of the player, three countries and three
provinces are the same. On the GOG build the hook ran with `"on"` on the vanilla
save (41 ms, the game loaded it).
Not run: hosted multiplayer (the module leaves it alone; a host in the game
needs the user present), a client, ironman and cloud saves. Compressed saves are
not affected.

### tick_profile (diagnostic, simulation)

Off by default. Where the daily tick spends its time, without a sampler.
`0x758430` runs one game day: the daily part `0x75BBE0`, the monthly tick
`0x75D690` on the first of a month and the yearly tick `0x75E260` on 1 January.
Between their stages it calls `0x769EA0` (draw a frame if enough time passed)
from about 40 fixed places. Both functions are hooked at their entry only to
observe: the day is cut into segments at the `0x769EA0` calls, named by the
return addresses at both ends, and the time inside `0x769EA0` is counted apart.
Every 5 s the module writes `tick_profile_segments.tsv` (from, to, calls,
total and max ms), `tick_profile_frames.tsv` (per checkpoint) and
`tick_profile_days.tsv` (date, ms, frame ms, checkpoints per game day), and
`tick_profile_segments_first.tsv`: the segments of the days dated the 1st of a
month only (a day row carries the date before it moves on, so the monthly tick
is in a month's last row and the 1st is the day after it). The
logic is in `tick_profile_core.hpp`, tested by `tests/test_tick_profile.cpp`.

Lab (Russia 1719 save driven to 1770 at speed 5 with the defaults plus this
module, normal CPU affinity; lab `docs/simulation-profile.md`, "Etapy dziennego
ticku"): the year's tick time does not grow with the campaign up to 1770
(16-19 s in calm years, 22-32 s in some); frames drawn inside the tick take
8.5-18.5%; one segment, from inside `0x75B4B0` (the pool job `0x77D8A0`) to the
next checkpoint of the daily part, takes about half of the tick without frames.

That segment holds the AI pool job: `0x75B4B0` sorts the per-country AI objects
(`gamestate+0xB78`) by tag, not by cost, and runs `0x77D8A0`, a parallel_for
whose item is `0xB0FA0`, one country's daily AI. The module also hooks both to
observe and writes `tick_profile_jobs.tsv` (per job: wall time, items, item sum
and maximum, threads, main-thread and busiest-thread time, and the lower bound
max(longest item, sum / threads)) and `tick_profile_countries.tsv`. Lab, 1719 to
1730, 6 workers per pool: the job is 47% of the tick (112.9 s of 239.2 s), the
busiest thread is busy for 111.2 s of it, the lower bound is 98.5 s, and one
country takes 17% of all AI time. A better order would save at most ~6% of the
tick; making the heaviest countries' AI cheaper is where the time is.

### army_search_profile (diagnostic, simulation)

Off by default. About half of the per-country daily AI (`0xB0FA0`) is
`0x1022B0`, called from `ai_army.cpp` (`0xEAB50`): a priority-queue search
over the province graph with a scoring functor (`param_1`, vtable
`0x1C3CD98`) for one army (`param_3`); it returns a province and a score
(`*param_5`), fills a path (`param_12`) and clears two arrays sized by the
province count on every call. The module hooks it only to observe and writes
`army_search.tsv` (calls, time, armies, keys, and per key whether the result
and score equal the previous call's) and `army_search_days.tsv`.

Lab (Russia 1719 to 1725, defaults plus this module; lab
`docs/simulation-profile.md`, "Wyszukiwanie armii AI"): one search per AI army
a day (about 85), 546 µs on average, 53.5% of the AI time; 92.6% of searches
return the same province and score as the day before. Returning yesterday's
answer would change the AI in the rest, so that is not a Bellows module; the
exact routes left are inside the scoring functions (`0xF8EA0` 34%,
`0x96F860` 23%, `0x102140` 22.5% of the search).

### score_probe (diagnostic, simulation)

Off by default. Asks whether the scoring work inside the AI army search could
be remembered within a game day with the game's own answers. It hooks, only to
observe, `0x96F860` (a method of the search's scoring functor: can the unit
move into a province), `0x102140` (army strength against a province) and
`0x1998F0` (a per-province table value at a date delta). Per call the inputs
become a key, with and without the army or unit; a memo per thread, cleared at
each new game day (no lock on the game's hot path), counts keys that come back
and whether the output is the same. `score_probe.tsv` has one row per function.

Lab (Russia 1719 to 1722; lab `docs/simulation-profile.md`, "Czy funkcje
oceniające da się zapamiętać"): every repeated full key gave the same output
(0 differences in 23.4 million repeats), but only `0x1998F0` repeats often
(55% of 32.9 million calls), worth about 1-2% of the tick; leaving the army or
unit out of the key changes the output (0.25-5.6%), so no function can be
shared between armies. No module followed.

### monthly_probe (diagnostic, simulation)

Off by default. Where the monthly tick spends its time and whether its loops
could run on several threads with the same game. It hooks, only to observe,
the monthly tick `0x75D690`, the per-country (`0x2F36C0`), per-province
(`0x9F35C0`) and unit (`0x763470`) updates it runs in serial loops, the
nearest-own-port table `0x350B80` (rebuilt for every province of the map by
each country's monthly update), the modifier-list merge `0x8EA420` and the
game's Mersenne Twister `0x14C71B0`. Random draws are counted per context: a
loop whose items draw from the shared generator gives a different game when
the items run in another order. Per merge it records the shape (dense or
sparse lists, counts, entries that take the linear scan of the destination)
per context, scan size and call site. The daily country update `0x2EE280`,
its largest parts and every call site of `CalculateModifier` (`0x327DC0`,
with the caller's source file it is given) are also timed per day class, from
the date on entering the daily tick as in `tick_profile`'s rows: dated the 1st
of a month, dated a month's last day (the call that runs the monthly tick) and
the others.

Lab (Russia 1719, 360 s): on the day dated the 1st the daily country update
takes 48.8 ms instead of 7.8 ms. About 90 countries have their modifier flag
`+0x2882` set after the monthly tick, and the daily update then runs
`0x93FE30` (+18 ms) and `CalculateModifier` from `country.cpp:9210` (+13 ms,
almost all in the full recompute `0x93D130`) for each of them, a day after the
monthly tick recalculated the modifiers of all 1,378 countries.
`monthly_probe.tsv`, `monthly_probe_countries.tsv`, `monthly_probe_merges.tsv`
and `monthly_probe_days.tsv` every 5 s.

Lab (Russia 1719 to 1730 and the 1821 autosave in observe mode; lab
`docs/simulation-profile.md`, "Tick miesięczny"): the monthly tick is about
19% of the tick (270 ms a month). 99% of the country updates draw from the
game's generator (1.07 million draws in 10.8 years), so that loop cannot run in
parallel and give the same game; the province updates draw rarely but do
(0.2% of calls); the unit update draws nothing and spends 89% of its time in
one merge site (each country's dense list of about 700 modifiers added to
every unit's list with a linear scan per entry, 44,000 compared entries a
call), which `modifier_merge` targets. The port table is 0.5% of the run.

### attr_cache

Ported in the lab's "negative" policy only: `GetFileAttributesExW` and
`GetFileAttributesA` (the game's only attribute imports) through Anvil
`replace_import`; "not found" answers remembered for paths inside the game
folder and the enabled mods' folders (11 roots), never for the user folder.
Verify start `sb-attr-verify-4` (other modules at defaults): 75 389 queries
before the ready marker (the lab counted ~1.05 M before `verifypath_listing`
and `exists_index` existed), 12 491 real misses, 7 682 would-be hits —
0 mismatches — whose real API calls took **114 ms** in total across threads;
55 216 queries were outside the roots. The hook itself costs a full-path
conversion and a lookup on every query, so the net effect is about zero:
**off by default**, kept for verification.

The skipped queries are almost all PhysFS `verifyPath` component checks in the
write directory (`Documents\…\Europa Universalis IV\gfx`, `\common`,
`\history`, …), which `verifypath_listing` deliberately does not answer. The
write directory is not static: the game regenerates `gfx\flags\flagfiles_*.tga`
there and writes `shadercache`, so a "not found" cache for it would need
invalidation on the game's own directory/file creation. Not implemented;
estimated at most ~1 s summed over threads (hook time / query count), not
measured on the critical path.

### ram_prefetch

Potential first: diagnostic `zip_profile` (off by default; wraps the ZIP
archiver's Io read slot) in start `sb-zipprof-2` with all other modules on:
the game's **main thread** spent **2 748 ms in ZIP member reads (1 216 MB)**,
another thread 567 ms (274 MB).

Ported from the lab with these hooks: OpenArchive / OpenRead / CloseArchive
slots of the ZIP archiver table (`patch_bytes`, expected pointers). Workers
read and decode members into buffers; OpenRead wraps the game's Io and serves
the buffer. Same safeguards as the lab: the local header is cross-checked with
the central directory; CRC-32 is computed over the whole output; at open the
game's own entry (method, CRC, sizes) must match; anything else goes to the
original Io. Differences from the lab:

- Prefetch starts at plugin load, in first-use order, instead of when the game
  mounts an archive.
- A buffer leaves the memory budget when the game takes it; a second open of the
  same member reads through the original path.
- DEFLATE uses the game's own `tinfl_decompress` (pure miniz code, worker-owned
  state) instead of zlib, so there is no new dependency; CRC-32 comes from
  `ntdll!RtlComputeCrc32`.
- The member list is an access profile learned per mod set
  (`<key>.zip.txt`, "archive\tmember"); a game-folder base layer is shipped
  (`plugin/profiles/<build>.zip.txt`, 2 523 members of 120 DLC archives).

Results (EU4 1.37.5, the lab's mod set):

- Learning start: 2 740 opens, 2 523 unique members, the same counts as the
  lab's observer run.
- Verify start `sb-ram-verify-1`: 2 523 served, 13 474 byte comparisons with
  the game's reads, 0 mismatches; 217 second opens read by the game itself.
- On: all 1 478 MB decoded ~0.6–0.7 s after load (decode 4.7–5.3 s summed over
  8 below-normal workers). The main thread's ZIP read time fell from 2 748 ms
  to 1.2 ms (`sb-ram-on-prof-1`).
- Series `sb-ram-ab3` (3 + 3 interleaved, warm, learned profiles kept):
  **12.24 s** median (11.98 / 12.24 / 12.50) vs 15.16 s without
  `ram_prefetch` (15.14 / 15.16 / 15.66); the ranges do not overlap.
  `setup.log`, `error.log` and `system.log` are identical between the two
  variants.
- Memory: with a 2 GB budget the whole profile is resident before the game
  takes the first member (peak not-yet-taken 1 478 MB).
- Budget (`[ram_prefetch] budget_mb` in `config.toml`, 64–8192 MB, override
  `EU4SB_RAM_PREFETCH_BUDGET_MB`): workers wait while the not-yet-taken bytes
  would exceed it. Series `sb-budget-1` (3 × 3 interleaved, warm):
  - 2048 MB: 11.97 s median (11.70 / 11.97 / 12.22); everything decoded after
    669 ms.
  - 1024 MB: 11.95 s (11.70 / 11.95 / 12.22); everything decoded after 2.7 s,
    and the budget was full at its peak.
  - 512 MB: 12.23 s (11.68 / 12.23 / 12.47); 3 members were not ready in time.

  1024 and 2048 MB are equal within the noise, so the default is **1024 MB**.

Next: cold-start check with all modules. The first attempt after a reboot
(15.10, `sb-cold-all-1`, 74.6 s) ran with a stale Anvil 0.1.0 build that
skipped the plugin (manifest requires `>=0.2.0`), so it measured Anvil alone.
### Real game folder

Anvil and the plugin installed in the Steam game folder (no junction copy),
`eu4.exe` started there with Steam running and the launcher's working
directory, fresh plugin data (shipped base layers), removed after each start
(lab harness `anvil-smoke.ps1 -RealGameFolder`; warm, one start each):

| Start | Running application |
| --- | ---: |
| without Anvil (`sb-real-vanilla-1`) | 33.60 s |
| Anvil + plugin, first start (`sb-real-1`) | 14.91 s |
| Anvil + plugin, again (`sb-real-2`) | 11.91 s |

In the first start `open_warmup` took 5.5 s (these paths had not been opened
since the reboot); in the second 74 ms. All modules loaded, `ram_prefetch`
served 1 478 MB with the 1 GB budget; `setup.log`, `error.log` and
`system.log` are identical to the start without Anvil.

Through the Paradox launcher (`sb-launcher-1`, `-3`, `-4`, `-5`): Steam opened
the launcher and Play was clicked through UI automation. `eu4.exe` was started
by `Paradox Launcher` and the plugin loaded with all default modules each
time, with fresh plugin data (shipped base layers). Starts are timed from the
game process's creation:

| Start | Running application | `open_warmup` | `ram_prefetch` workers |
| --- | ---: | ---: | ---: |
| `sb-launcher-1` | 17.77 s | 6 093 ms | 6 865 ms |
| `sb-launcher-3` | 17.27 s | 5 556 ms | 6 709 ms |
| `sb-launcher-4` | 13.81 s | 100 ms | 5 381 ms |
| `sb-launcher-5` | 13.72 s | 89 ms | 5 449 ms |

The game logs are identical to the start without Anvil, and the launcher left
`dlc_load.json` unchanged.

- **Slow warm-up:** the slow `open_warmup` runs were the first starts after a
  break. A direct start shows the same (`sb-real-1`: 5.5 s), so it is not
  launcher-specific.
- **Launcher slower with warm files:** 13.7–14.8 s, against ~11.9 s for direct
  starts. Launcher starts load the Steam overlay (`gameoverlayrenderer64.dll`,
  `steamclient64.dll`, checked 8 s after the process appears); a direct start
  (`sb-real-3`, 11.97 s) loads neither. That the overlay causes the difference
  is a hypothesis.
- **Plugin gain through the launcher:** without Anvil 30.87 s and 30.08 s
  (`sb-launcher-van-1`, `-van-2`), interleaved with `sb-launcher-6` at
  14.79 s; the game logs are identical.
- **Click timing:** a Play click while the launcher was still syncing at
  start-up did not launch the game (`sb-launcher-2`).

### Gameplay

`sb-play-2`: Anvil and the plugin in the game folder, the 1719 Russia save,
five in-game years at speed 5 played through UI automation (37 popups
answered), then exit through the game menu. The game wrote 5 yearly autosaves
and showed no crash, error dialog or broken graphics. `error.log` holds only
the mod-script errors already seen without the plugin. `sb-load-1`: the
1724.1.1 autosave written in that session loads correctly. The starting save
was unchanged after both sessions, and the game folder was clean.

Still open: the cold start with the plugin.

## 6b. Plugin API 2 for Anvil 1.0 (D)

Bellows moved to Anvil's plugin API 2 (Anvil design §5, §8, §9, §11) before
Anvil 1.0 and is released together with it; API 1 is gone, so this release
needs Anvil 1.0 (`api = 2`, `anvil = ">=1.0.0"`, `affects_gameplay = false`).

- **Symbols.** `plugin/symbols.toml` replaces `src/build.hpp`: 44 symbols for
  `eu4-1.37.5-steam-win64`, each with the bytes the module used to check
  itself. PhysFS functions are `export` symbols instead of `GetProcAddress`.
  Globals and struct offsets the modules read come from `values` on code that
  uses them: the game root and its date offset, the trigger registry, the
  keyword dictionary and the run-time token vector, the range walk's map
  object, bonus value and its two float constants (the values equal the RVAs
  of `build.hpp`). The ZIP entry offsets (0x46, 0x48, 0x50, 0x58) and the other
  struct layouts stay constants in code. `plugin/symbols.lock` comes from
  `anvil check plugin --exe <eu4.exe>` on the Steam 1.37.5 executable (SHA-256
  `9AD3EFE1…771A`); CI checks that it matches `symbols.toml`.
- **Verification moved to Anvil.** Anvil checks every symbol before the DLL
  loads; a mismatch skips the whole plugin with the symbol and byte in the log.
  Up to 0.7.0 a mismatch turned one module off.
- **Changes** (C++ wrapper `anvil.hpp`): IAT slots → `replace_import`; the
  `exists_index` vtable slot and the ZIP archiver/Io slots → `swap_pointer`
  (each call reads the chain's `previous`); call sites → `redirect_call`
  with one detour per site, so each passes on through its own `next`; range
  hooks → `hook_function` with typed `HookOf<sym::…>`. The verifyPath stub
  (`alloc_near_image` + `patch_bytes` over `call [r9+68h]; mov ebp,eax`) is a
  `replace_block` on the 6-byte block: the callback calls `SB_VerifyPathStat`
  with rcx/rdx/r8/r9 and writes eax and ebp.
- **One module fails alone.** Changes requested in `load()` are staged and
  enabled together after it returns. A refused change throws `anvil::Error`
  out of that module's `Install`; its `sb::Undo` removes the changes the module
  had already requested, and the other modules load.
- **Readiness.** `[[dependencies]] anvil-game ^1`: the `anvil-game.ready`
  handler sets `sb::g_gameReady` (the hot paths read it like the old marker
  byte) and starts a thread for the profiles and the report, since the event
  runs on the game's main thread.
- **Settings.** `plugin/settings.toml`: one enum per module (`on`/`off`, plus
  `verify` where the module has it) and `ram_prefetch_budget_mb` (int,
  64–8192), all `restart = true`. Overrides are `ANVIL_BELLOWS_<NAME>`; a set
  `EU4SB_<NAME>` is reported in the log with its replacement.
- **Migration of `config.toml`.** Anvil reads the player's settings before the
  plugin loads, so on the first start after the update the plugin itself
  moves `anvil/data/bellows/config.toml`: values that differ from the defaults
  go to `settings.toml` (written atomically; only when it does not exist yet),
  old spellings (`1`, `true`, upper case) are normalized, `verify` for a module
  without it becomes `on`, invalid entries are left out with a log line, and
  the old file is renamed to `config.toml.migrated`. The migrated values also
  apply to that start unless an `ANVIL_BELLOWS_<NAME>` variable overrides them.
  Limit: a setting changed in the overlay later in that same session would
  rewrite the player's file from what Anvil read at start-up (the overlay is
  not in Anvil 1.0).
- **Build.** The `AnvilPlugin` CMake package (`anvil_add_plugin`, `anvil gen`,
  the plugin folder in `build/plugin/bellows/`), from the SDK archive of a
  pinned Anvil release (`SB_ANVIL_VERSION` with its SHA-256; 1.0.0 since
  [#28](https://github.com/happy-dwarf-team/bellows/issues/28)).
- **Not run in the game yet** at the time of the port: unit tests, a load test
  of `bellows.dll` against a stand-in host (`tests/test_plugin_load.cpp`), and
  `anvil check --game` on a copy of `eu4.exe` (Bellows after `anvil-game`, no
  overlap).

## 6c. Start-up benchmark participant (D)

Anvil's start-up benchmark (Anvil `docs/spec/benchmark-v1.md`) runs starts of
the game without and with a plugin, each quitting itself once the game is
ready, and reports the times. Bellows takes part through the core service
`anvil.benchmark` v1 of Anvil 1.4 (`src/benchmark.hpp`); the manifest keeps
`anvil = ">=1.0.0"`, and on an older Anvil the lookup returns nothing and every
call does nothing.

- **Fields.** `load()` declares them all, whatever the modules' modes: per
  module `<module>.mode` (enum `off` / `on` / `verify` / `failed`), and the
  numbers of the module's `anvil.log` lines as `int` counters: the LogStats
  counters (`requests`, `hits`, `misses`, `skips`, `verifications`,
  `mismatches`, and the one printed as "extra" under a name of its own, such
  as `exists_index.listings`), `open_warmup.*` and `ram_prefetch.*` with their
  finish times in `ms` and the profile they used as an enum
  (`open_warmup.profile`: `learned` / `base_and_mods` / `all_list`;
  `ram_prefetch.profile`: `none` / `learned` / `base`). A module that is off
  or did not report leaves its fields `null`. A name keeps its meaning; a new
  meaning gets a new name. The log lines are unchanged.
- **When.** The modes are set in `load()`; the other values on the
  ready-report thread, next to each module's log line. `done()` ends that
  thread, after the profiles are saved and every report is made.
- **Learning.** `open_warmup` without a learned profile for the mod set
  (`base_and_mods` or `all_list`), or `ram_prefetch` building its first
  profile, calls `learning("new_mod_set")`: the start is discarded and
  replaced.
- **Open cost.** `open_warmup` reports the files it opened and its wall time
  (`open_cost(opened, finish_time)`); in the arm with Bellows this is the
  start's open cost that tells warm starts from cold ones. Without a finished
  warm-up there is none, and the Manager falls back to Anvil's measurement.
- **Tests.** `tests/test_plugin_load.cpp` gives the plugin a stand-in service
  that refuses what Anvil refuses (names, kinds, limits, a set before declare
  or after `done()`) and checks the modes, some counters and the single
  `done()`; the `old` scenario has no service.

## 6d. The GOG build 1.37.5.2 (D)

Anvil 1.7 knows the GOG `eu4.exe` 1.37.5.2 (`eu4-1.37.5.2-gog-win64`,
SHA-256 `D29CF745…5319`) and lets one `symbols.toml` list several builds
(Anvil design §5.2, [anvil#160](https://github.com/happy-dwarf-team/anvil/issues/160)).
Bellows declares both builds and needs Anvil 1.7 (`anvil = ">=1.7.0"`, SDK
1.7.0): an older Anvil rejects the `builds` key and skips the plugin.

- **Same code, other addresses.** Every symbol was matched to the same
  instructions in the GOG executable: functions through `.pdata` and their
  disassembly with addresses masked, leaf code by the gap between two matched
  functions, pointer slots by the matched target and the neighbouring slots.
  The GOG build ships `eu4.pdb`, which names each match
  (`CCountry::GetTradingRangeTo`, `CPdxSystem::FileExists`,
  `CZipArchive::WriteDeflateCompressed`, …); `modifier_merge` is
  `CModifier::operator+=` there, whose first chunk equals that of
  `operator-=`, so whole bodies were compared. 72 symbols keep their bytes
  and only move; 20 differ only in RIP-relative or call displacements (and
  one absolute table address in `nested_frame_threshold`) and get their bytes
  per build. No value with a size differs, and the numbers in the functions
  around every symbol are the same apart from addresses, so the struct
  layouts written in the modules (`range_witness`, `trigger_intern`,
  `token_dict_profile`, `metascript_*`, `modifier_merge`, the ZIP entry) hold
  on both builds; the PDB's types agree (`CProvince` 0x2E10 bytes, `CMap`,
  the game root's province and country arrays). The offsets that do differ
  in that code (the application at `+0xB48`/`+0xB38`, the session's speed
  object at `+0x418`/`+0x408`) are not ones Bellows reads.
- **Profiles.** The shipped base layers are lists of files in the game
  folder, so `plugin/profiles/eu4-1.37.5.2-gog-win64.txt` and `.zip.txt` are
  the same lists under the GOG build's name. Files of DLCs a copy does not
  have are skipped, as on Steam.
- **Lock.** `anvil check plugin --exe` once with each build's `eu4.exe`
  writes `symbols.lock` format 2 with one check per build.
- **Live check (2026-11-02).** A copy of the GOG game (folders as junctions,
  without Steam's environment), Anvil 1.7.0 from its release, Bellows and
  Tongs from their branches, `range_witness = "verify"` and
  `save_compress = "verify"`, a private profile with `compress_autosave =
  yes`. Anvil: `game build: eu4-1.37.5.2-gog-win64`, 4 loaded, 0 failed, 0
  skipped; every start-up module ran (`open_warmup` with the GOG base layer:
  11 175 opened, 238 files of DLCs this copy lacks failed; `ram_prefetch`
  2 433 of 2 523 members failed for the same reason) with 0 mismatches in
  their verifications. The Russia 1719 save, loaded from the menu, ran at
  speed 5 to 1720-01-03 (292 days, 23.8 s): `range_witness` 96 453 calls,
  86 946 answers compared, 0 disagreements, 0 differing values;
  `modifier_merge` 63 verifications, 0 mismatches; the autosave of
  1720-01-01 (3 members, 61.6 MB gamestate) the same bytes as the game's own
  compression, and a valid zip.

## 7. Open questions (Q)

None at the moment. Work after 1.0 (the `army_search_memo` default, `save_compress_parallel`
on by default, the GOG build 1.37.5.0) is listed at the end of
[#90](https://github.com/happy-dwarf-team/bellows/issues/90).

Answered: since 0.11.0 `startup` is the default and `mimalloc` has stayed a
Bellows module. With `startup` the year was not separable from `off` (series
below), so the cost in play that would have called for a plugin of its own
was not found. The question and the series:

- Whether `mimalloc` should be its own plugin (it stays active during play,
  unlike the start-up-only modules). In the lab (Russia 1719, year 1720 at
  speed 5, interleaved), the year took 22.06 s with `mimalloc = on` and
  20.84 s with it off (+5.9%, n = 5 and 6). The `startup` mode hands new
  blocks back to the Windows heap at the game-ready event, before a save is
  loaded; frees and sizes still go to the block's owner, and a realloc of a
  mimalloc block moves it to the Windows heap. Whether that keeps the start-up
  gain without the cost in play is measured before the default changes.
  First lab run of `startup` (11 valid trials per arm, interleaved): the year at
  21.28 s (not separable from `off`), start-up 12.52 s against 13.58 s with
  `off`, but 0.3 GiB more commit than `on` from the first save load and
  +0.37 GiB at each further load. Nothing allocates from mimalloc after the
  switch, so nothing ran its delayed purge; `mi_collect(true)` on the main
  thread at the ready and session events now returns those pages (offline:
  261 → 20 MiB for blocks of live threads freed on another thread). With it
  the year was no longer slower than with `off` (21.23 against 21.69 s), but
  the first save loaded from the in-game menu still added 0.46 GiB:
  anvil-game 1.5 publishes no session event there. So the frees of
  mimalloc blocks after the switch are counted as well, and the next frame
  collects once 64 MiB were freed since the last collect. With that, commit
  stayed within 0.08 GiB of `on` through four in-game loads to 1740. A third
  series had `startup` 10.5% slower than `off` (p = 0.084); the deciding
  interleaved series (66 trials, with and without the frame handler, and
  `off` on the same DLL, with other processes' CPU load recorded per trial)
  did not repeat it: −0.4% (p = 0.60), and the handler +0.6% (p = 0.79).
  Over all series `startup` was +0.5% against `off` (n = 66 and 53,
  p = 0.42) and `on` +5.3% (p = 0.033). `startup` is the default since
  0.11; a `config.toml` of 0.7.0 that says `on` (its default then) migrates
  to the new default. The start-up gain (about 1.1 s against `off`) was
  measured on the first `startup` build, before the collects.
