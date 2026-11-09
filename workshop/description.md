![](https://anvil.happydwarf.dev/workshop/bellows-hero.png)

Bellows makes EU4 reach the main menu much sooner and lets the years pass more than a third sooner at speed 5. The rules and your game files stay the same.

![](https://anvil.happydwarf.dev/workshop/bellows-install.png)

![](https://anvil.happydwarf.dev/workshop/bellows-steps.png)

Needs [Anvil](https://anvil.happydwarf.dev/) 1.7 or newer and EU4 1.37.5 (Steam; the GOG build 1.37.5.2 too, see the FAQ). If Windows warns about Anvil, choose **More info**, then **Run anyway**. On any other version Bellows politely stays out of the way.

![](https://anvil.happydwarf.dev/workshop/bellows-tiles-title.png)

![](https://anvil.happydwarf.dev/workshop/bellows-tiles.png)

[![](https://anvil.happydwarf.dev/workshop/bellows-docs.png)](https://anvil.happydwarf.dev/plugins/bellows/) [![](https://anvil.happydwarf.dev/workshop/bellows-github.png)](https://github.com/happy-dwarf-team/bellows)

[![](https://anvil.happydwarf.dev/workshop/discord-en.png)](https://discord.happydwarf.dev/)

![](https://anvil.happydwarf.dev/workshop/bellows-faq.png)

### Why do I have to approve it?
Bellows is a real program running inside your game, not just a pile of text files. Anvil makes sure nothing like that runs without your nod. Trust, but verify. Dwarf law.

### My game didn't get any faster.
Some of Bellows' tricks learn what your mod set needs during one start and pay off from the next, so give it two runs. Still slow? The **Mods** tab in Anvil's panel tells you if Bellows was skipped and why. For a fair number, run the **Benchmark** tab in Anvil Manager.

### Does it work with big overhaul mods?
Yes. It doesn't care what your mods contain, it only helps the game read them faster. Bigger mod sets usually gain the most: on our test machine, Anbennar with Proper 2K UI Enhanced went from about 85 seconds to about 22.

### Will it break my saves or my achievements?
It hasn't in our tests. It changes how fast the game starts and runs, not its rules. A new ironman game with Bellows still says achievements can be earned, but we haven't earned one in a full campaign yet. When the game compresses a save, Bellows writes exactly the file the game would write, byte for byte (unless you turn on parallel save compression, which packs the same save a little differently).

### Will the AI play differently?
Your AI can occasionally play out differently. In a planned test on one save, that happened in 11 of 30 runs with Bellows 1.2 and 1 of 30 without it; the four simulation modules that 1.4 turns on were not part of that test. The rules stay the same, and both are normal games. In multiplayer the host's AI decides for everyone (our tests: Bellows on the host alone, one save, no out-of-sync in 233 days with 0.17.0, 231 days with the code of 1.3.0 and 203 days with the defaults of 1.4).

### I turned a module off before 1.4. Is it still off?
Maybe not. Anvil only remembers settings that differ from the default, so if you turned **Port table**, **Province modifier view**, **AI army search memo** or **Online service pacing** off while off was already the default, 1.4 turns it on. Press **Ctrl+Shift+F12**, set it to **Off** in Bellows' **Simulation** settings and restart the game.

### I play on GOG. Can I use it?
Yes, the GOG build 1.37.5.2. GOG has no Workshop: install Bellows from the **Catalog** tab in Anvil Manager (Anvil 1.9 or newer) or from [GitHub](https://github.com/happy-dwarf-team/bellows/releases). Same plugin, same forge, different road.

### Something's wrong. Where do I ask?
In **#help-and-bugs** on our [Discord server](https://discord.happydwarf.dev/) (pick the Bellows tag), or in a [GitHub issue](https://github.com/happy-dwarf-team/bellows/issues/new/choose). What we confirm on Discord goes to GitHub too.

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
