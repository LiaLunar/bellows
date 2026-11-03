# Contributing to Bellows

The conventions are the same as in
[Anvil](https://github.com/happy-dwarf-team/anvil/blob/main/CONTRIBUTING.md);
this file lists what differs.

## Pull requests and titles

Every change goes to `main` through a squash-merged pull request whose title is
a [Conventional Commit](https://www.conventionalcommits.org/) (checked by the
`pr-title` workflow); the title becomes the commit on `main` and the changelog
entry. Types and their effect on the version:

| Type                                  | Release | Changelog                          |
| ------------------------------------- | ------- | ---------------------------------- |
| `feat`                                | minor   | Features                           |
| `fix`, `perf`, `revert`               | patch   | Bug Fixes / Performance / Reverts  |
| `build`                               | patch   | Build                              |
| `docs`, `refactor`, `test`, `ci`, `chore` | none | hidden                           |

A release is proposed only when the changelog would get a visible entry;
commits of hidden types alone never start one and ship with the next release.

Scopes: the module name (`ram_prefetch`, `attr_cache`, `open_warmup`,
`exists_index`, `trigger_intern`, `metascript_subst`, `mimalloc_heap`,
`verifypath_listing`, `zip_reader`, …), `settings`, `symbols`, `plugin`
(manifest, packaging), `profiles`. A module that is off by default and changes
game behaviour when enabled is still a `feat`; say so in the description.
Breaking changes (a setting renamed or removed, a value that means something
else, a higher Anvil requirement) take `!` and a `BREAKING CHANGE:` paragraph;
before 1.0.0 they bump the minor version, from 1.0.0 the major. What counts is
the list in the README, [Compatibility](README.md#compatibility): the module
settings and their values, `ram_prefetch_budget_mb`, `frame_pacing_*`,
`save_compress_parallel` and the Anvil requirement. The diagnostics
(`zip_profile`, `token_dict_profile`, `range_profile`, `tick_profile`,
`army_search_profile`, `score_probe`, `monthly_probe`), log lines, counters,
Benchmark fields and `.tsv` files are outside it: changing them is a `feat`
or `fix` without `!`. A new default for a module is not breaking either; say
it in the title. The migrations from 0.7.0 and earlier (`config.toml`, the
`eu4-startup-boost` folder, `EU4SB_*` variables) stay in 1.x and go only in
2.0.

Performance claims in a pull request follow the measurement rules of the lab:
same save/mod set, interleaved runs, median and spread, and what was not
measured.

## Versions and releases

[release-please](https://github.com/googleapis/release-please) maintains a
`chore(main): release x.y.z` pull request that bumps `version.txt`, the
`version` in `plugin/plugin.toml` and `workshop/descriptor.mod`, and
`CHANGELOG.md`. Merging it tags `vX.Y.Z` and creates a **draft** GitHub
release; the release workflow then builds the tag and attaches
`bellows-windows-x64.zip` (the `bellows/` plugin folder) and `SHA256SUMS.txt`
with a build provenance attestation to the draft. Once they are attached and
the release has passed the checks in [docs/release-gate.md](docs/release-gate.md),
publish the draft on GitHub: that makes the release public, rebuilds
anvil.happydwarf.dev and happydwarf.dev (`website.yml`) and, when it is turned
on, publishes the Steam Workshop mod (see the README,
[Steam Workshop](README.md#steam-workshop)).

Do not edit `version.txt`, the manifest or descriptor `version` or `CHANGELOG.md` by hand;
CMake refuses to configure when `version.txt` and `plugin/plugin.toml`
disagree. To force a version, put `Release-As: x.y.z` in a commit body.

## Building and testing

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The build uses the Anvil SDK's CMake package: `anvil_add_plugin` reads
`plugin/plugin.toml`, runs `anvil gen` on `plugin/symbols.toml`
(`symbols.gen.hpp`, `sym::<name>`) and `plugin/settings.toml`
(`settings.gen.hpp`, `cfg::<name>`), and lays out `build/plugin/bellows/`.
The package is the SDK archive of the Anvil release `SB_ANVIL_VERSION`,
checked against `SB_ANVIL_SDK_SHA256` (`CMakeLists.txt`); move to a newer
release on purpose, in its own pull request. `-DAnvilPlugin_DIR=<sdk>/cmake`
builds against an unpacked SDK archive instead, or against an SDK installed
from a local Anvil checkout (`cmake --install <anvil build> --component sdk`).

The code uses the C++ wrapper (`anvil/anvil.hpp`): typed symbols, so a
detour that does not match the `signature` in `symbols.toml` does not
compile, and `anvil::Error` for a refused change, which `sb::Undo` turns into
"this module off" (see `src/common.hpp`). Detours the game calls directly
must not throw.

`ctest` also loads `bellows.dll` against a stand-in host
(`tests/test_plugin_load.cpp`) and runs `anvil check plugin` (the schemas and
`symbols.lock`).

## Game addresses

Every address, expected byte, pointer target and export the modules use is
declared once in `plugin/symbols.toml`; a module refers to it as
`sym::<name>`. Anvil verifies the whole file before it loads the DLL and skips
the plugin on any mismatch, so a symbol is declared only with bytes validated
in the lab. Prefer `values` on code that reads a global or an offset over
writing the number in C++ again.

After a change to `plugin/symbols.toml`, run the check against the real game
(it only reads `eu4.exe`) and commit the rewritten `plugin/symbols.lock`:

```powershell
build\_deps\anvil-build\Release\anvil.exe check plugin --exe "<EU4>\eu4.exe"
```

(with `-DAnvilPlugin_DIR`, `anvil.exe` is in the SDK's `bin/`;
`build/anvil-tool-Release.txt` names the one the build uses). CI fails when
`symbols.lock` does not match `symbols.toml`.
