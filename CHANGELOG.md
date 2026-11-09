# Changelog

## [1.5.0](https://github.com/happy-dwarf-team/bellows/compare/v1.4.1...v1.5.0) (2026-11-09)

### Features

* **symbols:** Bellows runs on the GOG build 1.37.5.0, the version GOG Galaxy installs when you roll EU4 back. On that build it loads with Anvil 1.15.0 or later, which knows the build. Every address Bellows uses is the same as on GOG 1.37.5.2. Tested in the lab on a copy of a GOG 1.37.5.0 install with Tongs, Hamweard and Mei: a new campaign and one year of a 1719 save at speed 5. That was one run and not a measurement; the speed figures in the README are from Steam 1.37.5. Nothing changes on Steam or on GOG 1.37.5.2. ([#169](https://github.com/happy-dwarf-team/bellows/issues/169))

## [1.4.1](https://github.com/happy-dwarf-team/bellows/compare/v1.4.0...v1.4.1) (2026-11-09)


### Bug Fixes

* **settings:** list each group's settings together ([#167](https://github.com/happy-dwarf-team/bellows/issues/167)) ([c9d7711](https://github.com/happy-dwarf-team/bellows/commit/c9d771162301e6c093d9f0786d2716d9d3b98367))

## [1.4.0](https://github.com/happy-dwarf-team/bellows/compare/v1.3.0...v1.4.0) (2026-11-09)


### Features

* **frame_pacing:** change the interval and the input mode while the game runs ([#152](https://github.com/happy-dwarf-team/bellows/issues/152)) ([8dab270](https://github.com/happy-dwarf-team/bellows/commit/8dab2706abc860f24e29616a1cb50172d0714a41))
* **settings:** port_table, province_modifier_view, army_search_memo and pops_pacing on by default ([#142](https://github.com/happy-dwarf-team/bellows/issues/142)) ([8e68638](https://github.com/happy-dwarf-team/bellows/commit/8e6863837f51721c34a287c53dfc628d54ceb330))


### Bug Fixes

* **profiles:** drop learned entries whose file or ZIP member is gone ([#144](https://github.com/happy-dwarf-team/bellows/issues/144)) ([ee1b5b2](https://github.com/happy-dwarf-team/bellows/commit/ee1b5b214ba16c414fe2229873e1a9ca1ef15000))

## [1.3.0](https://github.com/happy-dwarf-team/bellows/compare/v1.2.0...v1.3.0) (2026-11-06)


### Features

* **army_search_memo:** more scopes, and the army's speed modifier ([#128](https://github.com/happy-dwarf-team/bellows/issues/128)) ([2468a59](https://github.com/happy-dwarf-team/bellows/commit/2468a59b52df2df191eabb28506bb3b3feedc8a6))
* **develop_search_memo:** one search per kind in the AI's walk over its provinces ([#132](https://github.com/happy-dwarf-team/bellows/issues/132)) ([7e7ab19](https://github.com/happy-dwarf-team/bellows/commit/7e7ab199453edbc82af09d22b53829daeca33ec0))
* **monthly_probe:** one row per monthly tick in monthly_probe_ticks.tsv ([#140](https://github.com/happy-dwarf-team/bellows/issues/140)) ([6bd5883](https://github.com/happy-dwarf-team/bellows/commit/6bd588349eac5c5cf28d2838aebf83ef8a770fea))
* **port_table:** the nearest-port table from one list of ports ([#139](https://github.com/happy-dwarf-team/bellows/issues/139)) ([9635e12](https://github.com/happy-dwarf-team/bellows/commit/9635e123cfc304e80a073f89bb285a69060f0585))


### Bug Fixes

* **army_search_memo:** take back the first change when the second is refused ([#127](https://github.com/happy-dwarf-team/bellows/issues/127)) ([6561395](https://github.com/happy-dwarf-team/bellows/commit/6561395a440f1bf46639937d45736f7d63f5263d))
* **history_add:** keep the game's walk for an older entry after a key that is no date ([#126](https://github.com/happy-dwarf-team/bellows/issues/126)) ([720c86d](https://github.com/happy-dwarf-team/bellows/commit/720c86d01ececb0ec9019c16de868c2538103c8d))
* **plugin:** a failed migration or report no longer costs the plugin or the game ([#138](https://github.com/happy-dwarf-team/bellows/issues/138)) ([dd5e874](https://github.com/happy-dwarf-team/bellows/commit/dd5e874dce9fb17581bfa8d3dff99c8294c9d7f3))
* read a -userdir whose path alone is in quotes ([#130](https://github.com/happy-dwarf-team/bellows/issues/130)) ([a196bd2](https://github.com/happy-dwarf-team/bellows/commit/a196bd2388ec10fa0d355c58b41df404e9cac2d2))


### Reverts

* **develop_search_memo:** the search's answer can change during a walk ([#133](https://github.com/happy-dwarf-team/bellows/issues/133)) ([b72ee5f](https://github.com/happy-dwarf-team/bellows/commit/b72ee5f839ffd53a0b64244b3f9a280a7309ff60))

## [1.2.0](https://github.com/happy-dwarf-team/bellows/compare/v1.1.0...v1.2.0) (2026-11-05)


### Features

* **army_search_memo:** the army's zone-of-control flag once per AI army search (off by default) ([#89](https://github.com/happy-dwarf-team/bellows/issues/89)) ([f07ce5f](https://github.com/happy-dwarf-team/bellows/commit/f07ce5f44d418bf3cdf51f8a95fbad1941f256ca))
* **history_add:** no walk over a dated list for an entry just read ([#121](https://github.com/happy-dwarf-team/bellows/issues/121)) ([a06f2ca](https://github.com/happy-dwarf-team/bellows/commit/a06f2ca6b7ea5c581a3f92066693866e2e3fc2ba))
* **pops_pacing:** the online-service pump every 500 ms at the fastest speed ([#119](https://github.com/happy-dwarf-team/bellows/issues/119)) ([e84eba5](https://github.com/happy-dwarf-team/bellows/commit/e84eba58255e193dcea0014aff418858397e2dc8))
* **verifypath_listing:** verifypath_session keeps the listings past the main menu ([#120](https://github.com/happy-dwarf-team/bellows/issues/120)) ([5221060](https://github.com/happy-dwarf-team/bellows/commit/5221060a06478ab7c66271b6fe93384ce560e3b3))


### Bug Fixes

* read the mods from the folder the game was started with ([#115](https://github.com/happy-dwarf-team/bellows/issues/115)) ([ee28ea7](https://github.com/happy-dwarf-team/bellows/commit/ee28ea7815a03e43acdab2d4383986b9200c13c6))

## [1.1.0](https://github.com/happy-dwarf-team/bellows/compare/v1.0.0...v1.1.0) (2026-11-04)


### Features

* **transfer_skip:** load a text save in single player without its transfer copy ([#112](https://github.com/happy-dwarf-team/bellows/issues/112)) ([31e1ae3](https://github.com/happy-dwarf-team/bellows/commit/31e1ae3511246d9cb8543bef789c5566f4564324))


### Bug Fixes

* read the mods of a GOG install from its own user folder ([#111](https://github.com/happy-dwarf-team/bellows/issues/111)) ([f3f7901](https://github.com/happy-dwarf-team/bellows/commit/f3f7901b93086afb347f4548a571c07e64e8036a))

## [1.0.0](https://github.com/happy-dwarf-team/bellows/compare/v0.19.0...v1.0.0) (2026-11-04)


### Bug Fixes

* **ram_prefetch:** wake every waiting worker when prefetch stops at ready ([#102](https://github.com/happy-dwarf-team/bellows/issues/102)) ([24972e5](https://github.com/happy-dwarf-team/bellows/commit/24972e5f931d16010d95b9e0186e605f4726522c))


### Documentation

* say Bellows is 1.0 in the README status and the design notes ([#104](https://github.com/happy-dwarf-team/bellows/issues/104)) ([ca61697](https://github.com/happy-dwarf-team/bellows/commit/ca61697c8f2ea939e1792d8146af2025e46a653b))

## [0.19.0](https://github.com/happy-dwarf-team/bellows/compare/v0.18.0...v0.19.0) (2026-11-02)


### ⚠ BREAKING CHANGES

* **symbols:** Bellows needs Anvil 1.7.0 or newer (anvil = ">=1.7.0"); an older Anvil rejects the `builds` key and skips the plugin.

### Features

* **symbols:** run on the GOG build 1.37.5.2 ([#94](https://github.com/happy-dwarf-team/bellows/issues/94)) ([c5d0530](https://github.com/happy-dwarf-team/bellows/commit/c5d053069d94d715cfe13dbc3a2f42963310c210))

## [0.18.0](https://github.com/happy-dwarf-team/bellows/compare/v0.17.0...v0.18.0) (2026-11-02)


### Features

* **save_compress:** compress saves without the game's byte-at-a-time CRC (on by default), optionally in parallel ([#85](https://github.com/happy-dwarf-team/bellows/issues/85)) ([95de7c6](https://github.com/happy-dwarf-team/bellows/commit/95de7c695d10151f11f83a4e6d04d89b5a38b1b7))

## [0.17.0](https://github.com/happy-dwarf-team/bellows/compare/v0.16.0...v0.17.0) (2026-11-01)


### Features

* **monthly_probe:** the first day of a month: segments, daily country parts and CalculateModifier sites ([#80](https://github.com/happy-dwarf-team/bellows/issues/80)) ([2ff0c4c](https://github.com/happy-dwarf-team/bellows/commit/2ff0c4c0bb115e901842bfde906fc3e9401e77ba))
* **province_modifier_view:** read a province's modifier value without copying its list ([#82](https://github.com/happy-dwarf-team/bellows/issues/82)) ([07fcb65](https://github.com/happy-dwarf-team/bellows/commit/07fcb6573784a9dcde98e35816775d51c1b56761))

## [0.16.0](https://github.com/happy-dwarf-team/bellows/compare/v0.15.0...v0.16.0) (2026-11-01)


### Features

* **modifier_merge:** index the large modifier-list merges of 0x8EA420, on by default ([#78](https://github.com/happy-dwarf-team/bellows/issues/78)) ([44a841f](https://github.com/happy-dwarf-team/bellows/commit/44a841f678664701d1cd0346f7e8f04e407c0165))
* **monthly_probe:** diagnostic counters for the monthly tick ([#76](https://github.com/happy-dwarf-team/bellows/issues/76)) ([f3d136a](https://github.com/happy-dwarf-team/bellows/commit/f3d136a1bef439d5f2c4998571d5276232f276db))

## [0.15.0](https://github.com/happy-dwarf-team/bellows/compare/v0.14.0...v0.15.0) (2026-10-31)


### Features

* **frame_pacing:** on by default ([#73](https://github.com/happy-dwarf-team/bellows/issues/73)) ([cc7e4e0](https://github.com/happy-dwarf-team/bellows/commit/cc7e4e0221ba72f54f3131911ae30947b01f92c2))

## [0.14.0](https://github.com/happy-dwarf-team/bellows/compare/v0.13.0...v0.14.0) (2026-10-28)


### Features

* **metascript_blocks:** resolve [[param] ...] blocks of scripted effects in one pass ([#69](https://github.com/happy-dwarf-team/bellows/issues/69)) ([f44ca47](https://github.com/happy-dwarf-team/bellows/commit/f44ca47dffa8377fa1e4827a5ea91dbb4ce927da))


### Performance

* **metascript_blocks:** move a sampled comparison off slow calls ([#71](https://github.com/happy-dwarf-team/bellows/issues/71)) ([d1d0668](https://github.com/happy-dwarf-team/bellows/commit/d1d0668b31f7bb82dcb719fde79ec946a0f41e3f))

## [0.13.0](https://github.com/happy-dwarf-team/bellows/compare/v0.12.0...v0.13.0) (2026-10-28)


### Features

* **frame_pacing:** react at once to a waiting key press or click ([#67](https://github.com/happy-dwarf-team/bellows/issues/67)) ([1d16602](https://github.com/happy-dwarf-team/bellows/commit/1d166023b1b83bf3bc65cb2da9d156516b8b56d1))

## [0.12.0](https://github.com/happy-dwarf-team/bellows/compare/v0.11.0...v0.12.0) (2026-10-27)


### Features

* **army_search_profile:** diagnostic counters of the AI army search ([#65](https://github.com/happy-dwarf-team/bellows/issues/65)) ([b511467](https://github.com/happy-dwarf-team/bellows/commit/b511467dd55b215e474f0b79bb8f7863e0814f0a))
* **frame_pacing:** draw frames inside the daily tick less often at the fastest speed ([#61](https://github.com/happy-dwarf-team/bellows/issues/61)) ([a4614ee](https://github.com/happy-dwarf-team/bellows/commit/a4614ee41183282b80602a131c822718c5996091))
* **range_witness:** write the first verify mismatches to range_witness_mismatches.tsv ([#62](https://github.com/happy-dwarf-team/bellows/issues/62)) ([e232667](https://github.com/happy-dwarf-team/bellows/commit/e232667add6c27f6759ed47493f969d3f7052450))
* **score_probe:** diagnostic memo counters for the AI scoring functions ([#66](https://github.com/happy-dwarf-team/bellows/issues/66)) ([8d8edbf](https://github.com/happy-dwarf-team/bellows/commit/8d8edbf9401b1887f04e0c53b7dded82c536dbbd))
* **tick_profile:** diagnostic profile of the daily tick's stages ([#64](https://github.com/happy-dwarf-team/bellows/issues/64)) ([3c71a45](https://github.com/happy-dwarf-team/bellows/commit/3c71a4507ef1bfb8067112a98b0ec7b8dd4c2dde))

## [0.11.0](https://github.com/happy-dwarf-team/bellows/compare/v0.10.0...v0.11.0) (2026-10-26)


### Features

* **mimalloc:** use mimalloc only until the main menu by default ([#59](https://github.com/happy-dwarf-team/bellows/issues/59)) ([943206b](https://github.com/happy-dwarf-team/bellows/commit/943206b0c684d87b45f034d1db9e761e56802b18))

## [0.10.0](https://github.com/happy-dwarf-team/bellows/compare/v0.9.0...v0.10.0) (2026-10-26)


### Features

* report Bellows' fields to Anvil's start-up benchmark ([#51](https://github.com/happy-dwarf-team/bellows/issues/51)) ([261eed0](https://github.com/happy-dwarf-team/bellows/commit/261eed00640bcabf5cbd7e89df974984d822c7b7))


### Build

* take the Anvil SDK from the 1.4.0 release ([#49](https://github.com/happy-dwarf-team/bellows/issues/49)) ([e0205e7](https://github.com/happy-dwarf-team/bellows/commit/e0205e772b47d103da07b9a86ddd3df69910d774))

## [0.9.0](https://github.com/happy-dwarf-team/bellows/compare/v0.8.0...v0.9.0) (2026-10-21)


### Features

* publish Bellows as a Steam Workshop mod ([#32](https://github.com/happy-dwarf-team/bellows/issues/32)) ([e7280ef](https://github.com/happy-dwarf-team/bellows/commit/e7280efd3e19db5bf789a188ef553a7ecad971c6))

## [0.8.0](https://github.com/happy-dwarf-team/bellows/compare/v0.7.0...v0.8.0) (2026-10-20)


### ⚠ BREAKING CHANGES

* Bellows now needs Anvil 1.0 (plugin API 2, anvil >= 1.0.0, the anvil-game plugin); Anvil 0.2 no longer loads it. config.toml and the EU4SB_<NAME> variables are no longer read: settings live in anvil/data/bellows/settings.toml (migrated from config.toml on the first start) and the overrides are ANVIL_BELLOWS_<NAME>.

### Features

* migrate to Anvil plugin API 2 ([#29](https://github.com/happy-dwarf-team/bellows/issues/29)) ([49fbd69](https://github.com/happy-dwarf-team/bellows/commit/49fbd698e2f3fb1b3fb51d5775e96e55a292c1df)), closes [#27](https://github.com/happy-dwarf-team/bellows/issues/27)


### Build

* take the Anvil SDK from the 1.0.0 release ([#31](https://github.com/happy-dwarf-team/bellows/issues/31)) ([7112b43](https://github.com/happy-dwarf-team/bellows/commit/7112b43dcc60742ada166d8a071fa8431e6f8b13)), closes [#28](https://github.com/happy-dwarf-team/bellows/issues/28)

## [0.7.0](https://github.com/happy-dwarf-team/bellows/compare/v0.6.0...v0.7.0) (2026-10-18)


### Features

* **range_profile:** diagnostic counters for the country range check ([#22](https://github.com/happy-dwarf-team/bellows/issues/22)) ([2d3cb86](https://github.com/happy-dwarf-team/bellows/commit/2d3cb86beff06a5191b6d519f29c16f26325abec))
* **range_witness:** on by default ([#25](https://github.com/happy-dwarf-team/bellows/issues/25)) ([5193824](https://github.com/happy-dwarf-team/bellows/commit/5193824ce35686a2f761e5144381ec997427ab26))


### Performance

* **range_witness:** answer the country range check from the last in-range province ([#24](https://github.com/happy-dwarf-team/bellows/issues/24)) ([2ea17d4](https://github.com/happy-dwarf-team/bellows/commit/2ea17d4bc525042a57f9ea51e67c90b43ecb9eb5))

## [0.6.0](https://github.com/happy-dwarf-team/bellows/compare/v0.5.0...v0.6.0) (2026-10-16)


### ⚠ BREAKING CHANGES

* the plugin id, folder and DLL are now bellows; delete anvil/plugins/eu4-startup-boost/ when updating.

### Features

* rename EU4 Startup Boost to Bellows ([#20](https://github.com/happy-dwarf-team/bellows/issues/20)) ([e505902](https://github.com/happy-dwarf-team/bellows/commit/e50590242b03874a41d9745dd9395a0eff37ba8e))

## [0.5.0](https://github.com/happy-dwarf-team/eu4-startup-boost/compare/v0.4.0...v0.5.0) (2026-10-16)


### Features

* **token_dict_cache:** cache the tokenizer's keyword lookups, on by default ([#18](https://github.com/happy-dwarf-team/eu4-startup-boost/issues/18)) ([b505985](https://github.com/happy-dwarf-team/eu4-startup-boost/commit/b5059854e6e171db18d1346c28bdc105829e380c))
* **token_dict_profile:** diagnostic counters for the tokenizer's keyword dictionary ([#17](https://github.com/happy-dwarf-team/eu4-startup-boost/issues/17)) ([6c8bc1c](https://github.com/happy-dwarf-team/eu4-startup-boost/commit/6c8bc1ccdb4900aeae2eadbf8e5d31bab332be66))

## [0.4.0](https://github.com/happy-dwarf-team/eu4-startup-boost/compare/v0.3.0...v0.4.0) (2026-10-16)


### Features

* **trigger_intern:** enable by default ([#14](https://github.com/happy-dwarf-team/eu4-startup-boost/issues/14)) ([d42d1f2](https://github.com/happy-dwarf-team/eu4-startup-boost/commit/d42d1f21ea34f2cf47dcf6484d895f262a6cd966))


### Performance

* **trigger_intern:** cut the hook's per-call overhead ([#13](https://github.com/happy-dwarf-team/eu4-startup-boost/issues/13)) ([474bca5](https://github.com/happy-dwarf-team/eu4-startup-boost/commit/474bca51543dc8bd090dc3160e23a797b03d5cba))
* **trigger_intern:** faster index for a non-empty map ([#12](https://github.com/happy-dwarf-team/eu4-startup-boost/issues/12)) ([a57751e](https://github.com/happy-dwarf-team/eu4-startup-boost/commit/a57751ea41184d9d450c33bfc52c53cb0b048a3f))

## [0.3.0](https://github.com/happy-dwarf-team/eu4-startup-boost/compare/v0.2.0...v0.3.0) (2026-10-16)


### Features

* **ram_prefetch:** configurable memory budget, default 1 GB ([#9](https://github.com/happy-dwarf-team/eu4-startup-boost/issues/9)) ([a6f6679](https://github.com/happy-dwarf-team/eu4-startup-boost/commit/a6f6679d8a6aa16ee5a4228648a4216234141c32))

## [0.2.0](https://github.com/happy-dwarf-team/eu4-startup-boost/compare/v0.1.0...v0.2.0) (2026-10-15)


### Features

First public release. Each module is `on`, `off` or `verify` in
`anvil/data/eu4-startup-boost/config.toml` (created on first start):

* **mimalloc:** faster memory allocator for the game's C runtime
* **verifypath_listing:** answers PhysFS path checks from directory listings
* **exists_index:** answers "does this file exist" for the modifier icon check
* **metascript_subst:** single-pass `$parameter$` substitution in scripted effects
* **trigger_intern:** alternative trigger interning (off by default: slower than the game's own code with the shipped trigger profile)
* **open_warmup:** opens the files the game will need ahead of it on parallel threads, from a profile learned per mod set
* **ram_prefetch:** reads and unpacks the DLC archive members the game will need into RAM on worker threads (learned per mod set; up to ~1.5 GB RAM during start-up)
* **attr_cache:** remembers "file not found" answers for game and mod files during start-up (off by default)

Lab measurement (EU4 1.37.5 Steam, 10 UI/graphics/music mods, warm start to the main menu, 3 interleaved starts each): 34.96 s without plugins, 15.40 s with the first five modules; 12.24 s median with `ram_prefetch` in a separate series (15.16 s with it off). Game logs identical with and without the plugin; a verify run compared 3.6 million answers with the game's own code, 0 differences. Requires Anvil 0.2.0 or newer; supported game build: EU4 1.37.5 (Steam, Windows 64-bit), other builds are skipped by Anvil.


### Build

* **plugin:** require Anvil 0.2.0 or newer ([#3](https://github.com/happy-dwarf-team/eu4-startup-boost/issues/3)) ([c52b80d](https://github.com/happy-dwarf-team/eu4-startup-boost/commit/c52b80db70cdf3e913354942f032d740fc4a3b04))
* read the version from version.txt only ([#1](https://github.com/happy-dwarf-team/eu4-startup-boost/issues/1)) ([12de7a0](https://github.com/happy-dwarf-team/eu4-startup-boost/commit/12de7a0c57e89e8dd3b8986c924e93c30e925a8d))


### CI

* automate releases with release-please and check PR titles ([#2](https://github.com/happy-dwarf-team/eu4-startup-boost/issues/2)) ([1ce1600](https://github.com/happy-dwarf-team/eu4-startup-boost/commit/1ce1600390bb60ea0371953c573396633d1897b4))
