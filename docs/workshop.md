# Publishing to the Steam Workshop

For maintainers. The mod is the `workshop/` folder ([`workshop.toml`](../workshop.toml)):
`descriptor.mod`, `thumbnail.png` and `anvil/bellows/`, the plugin folder of a
release, which is not in Git. [`workshop/description.md`](../workshop/description.md)
is the item's description: every publish replaces the description on Steam with
it. `checksum = "keep"` makes the publish fail if the mod would change the game's
checksum.

- Every pull request runs a dry run of
  [paradox-workshop-publish](https://github.com/happy-dwarf-team/paradox-workshop-publish)
  (`pdx-ws`) in `build.yml`: the job summary shows the files, the descriptor and
  the rendered description. It needs no secrets.
- [`workshop-publish.yml`](../.github/workflows/workshop-publish.yml) runs when a
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

## Setting it up, once

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
[CONTRIBUTING.md](../CONTRIBUTING.md#versions-and-releases)) also publishes the
Workshop mod. When the Steam session expires, the job fails with exit code 3:
export a new one into the organization secret, for every repository that uses
it (Git Bash or PowerShell 7.4+, so the pipe passes the session unchanged):

```sh
pdx-ws session export --user my_steam_name | gh secret set STEAM_SESSION --org happy-dwarf-team --visibility selected --repos bellows,tongs,eu4-anbennar-widescreen-loading-screens
```
