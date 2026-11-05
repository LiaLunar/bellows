![](https://anvil.happydwarf.dev/workshop/bellows-hero.png)

Bellows makes EU4 reach the main menu much sooner and lets the years pass about a quarter sooner at speed 5. Your game stays exactly the same.

![](https://anvil.happydwarf.dev/workshop/bellows-install.png)

![](https://anvil.happydwarf.dev/workshop/bellows-steps.png)

Needs [Anvil](https://anvil.happydwarf.dev/) 1.7 or newer and EU4 1.37.5 (Steam; the GOG build 1.37.5.2 too, see the FAQ). If Windows warns about Anvil, choose **More info**, then **Run anyway**. On any other version Bellows politely stays out of the way.

![](https://anvil.happydwarf.dev/workshop/bellows-tiles-title.png)

![](https://anvil.happydwarf.dev/workshop/bellows-tiles.png)

[![](https://anvil.happydwarf.dev/workshop/bellows-docs.png)](https://anvil.happydwarf.dev/plugins/bellows/) [![](https://anvil.happydwarf.dev/workshop/bellows-github.png)](https://github.com/happy-dwarf-team/bellows)

![](https://anvil.happydwarf.dev/workshop/bellows-faq.png)

### Why do I have to approve it?
Bellows is a real program running inside your game, not just a pile of text files. Anvil makes sure nothing like that runs without your nod. Trust, but verify. Dwarf law.

### My game didn't get any faster.
Some of Bellows' tricks learn what your mod set needs during one start and pay off from the next, so give it two runs. Still slow? The **Mods** tab in Anvil's panel tells you if Bellows was skipped and why. For a fair number, run the **Benchmark** tab in Anvil Manager.

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

![](https://anvil.happydwarf.dev/workshop/bellows-credits.png)

**Anvil** for letting a dwarf into the forge.
**mimalloc** by Microsoft for the faster memory handling.
**Everyone who waited 41 seconds for the main menu**: never again.

[![](https://anvil.happydwarf.dev/workshop/collection-en.png)](https://steamcommunity.com/sharedfiles/filedetails/?id=3833899418)
