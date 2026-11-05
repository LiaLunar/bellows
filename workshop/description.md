**Your game starts in about 13 seconds instead of 41. The forge gets hot sooner.**

Bellows makes EU4 reach the main menu much sooner and lets the years pass about a quarter sooner at speed 5. Your game stays exactly the same: Bellows only helps it do its work quicker.

## What you get
- **A much shorter start:** about 13 seconds instead of 41 on our test machine (10 mods). Big mod sets gain the most: Anbennar went from about 85 seconds to about 22.
- **Faster years at speed 5:** the year 1720 took about 20 seconds instead of 27 on our test machine, and the game still answers every click at once.
- **Quicker compressed autosaves,** with the same file the game would write.
- **Safe to keep on:** no game files or rules change, so ironman achievements stay available and multiplayer stays in sync.
- **Measure it yourself:** the Benchmark tab in Anvil Manager compares starts with and without Bellows on your computer.

![](https://i.imgur.com/Uf8Z21n.png)

## Install
Bellows runs on EU4 1.37.5 (Steam, Windows; the GOG build 1.37.5.2 too, see the FAQ). On any other version it politely stays out of the way and the game starts as usual.

1. **Install [Anvil](https://anvil.happydwarf.dev/) 1.7 or newer.** Download it, run it and click **Install Anvil**. If Windows warns, choose **More info**, then **Run anyway**. You do this once; the Workshop can't do it for you.
2. **Subscribe** and enable Bellows in your playset.
3. **Start the game.** Anvil says a plugin is waiting: press **Ctrl+Shift+F12**, open **Mods** and approve Bellows.
4. **Restart the game.** Enjoy the shorter wait.

Anvil asks again after an update; the version you approved keeps working until then.

![](https://i.imgur.com/Uf8Z21n.png)

## In the settings
In Anvil's panel (**Ctrl+Shift+F12**, Settings) or in Anvil Manager:
- **Frame pacing at speed 5** (on): while you just watch, the map is drawn about 16 instead of 22 times a second, so the years pass about 10% sooner.
- **Modifier merge index** (on): the monthly update of the units finds modifiers with an index instead of searching a list. Exactly the game's result, and a year passes about 5% sooner.
- **Province modifier view** (off): reads province modifiers in place instead of copying them first. About 12 million values matched the game's; off until we know it can't nudge the AI.
- **Save compression** (on, for compressed saves): an autosave in about 0.83 instead of 0.87 seconds, byte for byte the game's file. **Parallel save compression** (off) packs it on several cores in about 0.6 seconds: laid out a little differently, the same save.
- **Skip transfer compression** (on): Continue in single player, about 2 seconds sooner on a 64 MB text save.
- **PhysFS path checks after start-up** and **Dated lists while loading** (new, on): Continue another 2 seconds sooner with 10 mods (8.6 to 6.3 s in our tests), about 0.2 s on vanilla.

![](https://i.imgur.com/Uf8Z21n.png)

## Measure it yourself
Don't take our word for it. With Bellows enabled and approved, open Anvil Manager (`AnvilManager.exe` in the game folder), go to the **Benchmark** tab, pick Bellows and start the series. The game then starts several times on its own, without Bellows and with it in turn, and closes each time it reaches the main menu. At the end you see the typical start time of both, side by side. Your saves, mods and settings stay as they were.

Keep Steam running and leave the computer alone while it works: about ten minutes with the default settings. Afterwards you can send the report to us, after seeing exactly what it contains ([what we collect](https://github.com/happy-dwarf-team/anvil/blob/main/docs/benchmark-privacy.md)), or copy it into a [performance report](https://github.com/happy-dwarf-team/bellows/issues/new?template=performance_report.yml). A dwarf trusts the scales, not the boasting.

![](https://i.imgur.com/Uf8Z21n.png)

## What it doesn't touch
- No game files: the files the game checks stay the same, so ironman achievements stay available.
- No gameplay changes, so it's fine in multiplayer: the host's AI decides for everyone, and every player's game stays in sync.
- Unsubscribe and it's gone. Nothing is left in your saves.

![](https://i.imgur.com/Uf8Z21n.png)

## More from Happy Dwarf Team
- **[Anvil](https://anvil.happydwarf.dev/)**: the free loader that lets plugins like Bellows into EU4, with a Manager to approve and update them.
- **[Hamweard](https://steamcommunity.com/sharedfiles/filedetails/?id=3832650792)**: and once you play, Exit to Menu takes you back to the main menu without a restart.
- **[Mei](https://steamcommunity.com/sharedfiles/filedetails/?id=3832685486)**: Chinese, Japanese and Korean text in EU4, for the translation mod of your choice.
- **[Tongs](https://steamcommunity.com/sharedfiles/filedetails/?id=3826695093)**: for mod testers and tool makers, let your scripts and AI agents see the game and press its buttons.
- **[Etch](https://etch.happydwarf.dev/)**: for interface modders, see your `.gui` window as the game draws it, right in VS Code.
- Everything we make: [happydwarf.dev](https://happydwarf.dev/)

### Alternative download
[GitHub](https://github.com/happy-dwarf-team/bellows), also for the GOG build.

![](https://i.imgur.com/Uf8Z21n.png)

## FAQ

### Why do I have to approve it?
Bellows is a real program running inside your game, not just a pile of text files. Anvil makes sure nothing like that runs without your nod. Trust, but verify. Dwarf law.

### My game didn't get any faster.
Some of Bellows' tricks learn what your mod set needs during one start and pay off from the next, so give it two runs. Still slow? The **Mods** tab in Anvil's panel tells you if Bellows was skipped and why. For a fair number, run the benchmark from **Measure it yourself** above.

### Does it work with big overhaul mods?
Yes. It doesn't care what your mods contain, it only helps the game read them faster. Bigger mod sets usually gain the most: on our test machine, Anbennar with Proper 2K UI Enhanced went from about 85 seconds to about 22.

### Will it break my saves or my achievements?
No. It changes how fast the game starts and runs, not what the game does. When the game compresses a save, Bellows writes exactly the file the game would write, byte for byte (unless you turn on parallel save compression, which packs the same save a little differently).

### Will the AI play differently?
Only when it has two equally good choices, like two leaders with the same pips for one army. The game then picks by where they sit in memory, and Bellows moves things around in memory, so in single player the tie can go the other way: just as sound a choice, only the other one. In multiplayer the host's AI decides for everyone, so all players keep the same game (our test: 233 days, Bellows only on the host, no out-of-sync).

### I play on GOG. Can I use it?
Yes, the GOG build 1.37.5.2. GOG has no Workshop: install Bellows from the **Catalog** tab in Anvil Manager (Anvil 1.9 or newer) or from [GitHub](https://github.com/happy-dwarf-team/bellows/releases). Same plugin, same forge, different road.

### What is that little dwarf head on the thumbnail?
If you check the mod's GitHub page, you will notice the repository belongs to Happy Dwarf Team.

I like dwarves.
I like being happy.
I am, in fact, a happy dwarf myself.

![](https://i.imgur.com/Uf8Z21n.png)

## Credits
**Anvil** for letting a dwarf into the forge.
**mimalloc** by Microsoft for the faster memory handling.
**Everyone who waited 41 seconds for the main menu**: never again.
