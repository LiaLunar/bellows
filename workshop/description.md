![](https://anvil.happydwarf.dev/workshop/bellows-hero.png)

Bellows makes EU4 reach the main menu much sooner and lets the years pass more than a third sooner at speed 5. The rules and your game files stay the same.

![](https://anvil.happydwarf.dev/workshop/bellows-install.png)

![](https://anvil.happydwarf.dev/workshop/bellows-steps.png)

Get Anvil Manager from [anvil.happydwarf.dev](https://anvil.happydwarf.dev/). Bellows needs Anvil 1.7 or newer (take the latest) and EU4 1.37.5 (Steam; GOG's 1.37.5.2 and 1.37.5.0 too, see the FAQ). If Windows warns about Anvil Manager, choose **More info**, then **Run anyway**. On any other version Bellows politely stays out of the way.

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
It hasn't in our tests. It changes how fast the game starts and runs, not its rules. A new ironman game with Bellows still says achievements can be earned, but we haven't earned one in a full campaign yet. When the game compresses a save, Bellows writes the very same file the game would. (The optional parallel save compression packs it a little differently.)

### Will the AI play differently?
Sometimes. If you load the same save twice, the AI can make a different choice the second time, and from there the two games go their own ways. That happens without Bellows too, just less often: when the AI weighs two equally good options, a tiny difference in timing can tip it either way. The rules stay the same, and both are normal games. In multiplayer the host's AI decides for everyone, so all players get the same choices; in our tests with Bellows on the host, the game stayed in sync for about 200 game days.

### Can it go faster while the game is minimized?
Yes, with **Full speed while minimized** in Bellows' **Simulation** settings, off by default. A minimized game normally waits a tenth of a second every frame; with the setting on, a single-player game at speed 5 skips that wait. On our test machine a minimized year took about 60% less time without mods and about 40% less with Anbennar, and the game used more processor time meanwhile. With the window in front nothing changes, and in multiplayer the setting does nothing.

### Anything else that is off by default?
**Checksum after the databases** in the **Loading** settings takes about another second off the start of a big mod set such as Anbennar, and changes nothing else. Press **Ctrl+Shift+F12**, turn it on and restart the game.

### I play on GOG. Can I use it?
Yes, the GOG builds 1.37.5.2 and 1.37.5.0, with the latest Anvil. GOG has no Workshop: install Bellows from the **Catalog** tab in Anvil Manager or from [GitHub](https://github.com/happy-dwarf-team/bellows/releases). Same plugin, same forge, different road.

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
