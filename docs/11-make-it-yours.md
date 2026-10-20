# 11. Make it yours

Before your app is anything more than an experiment, give it its own identity.
Do it early: the title ID ends up in folder names and saved files, and
changing it later means users end up with two copies.

## The quick way

Pick a name and a title ID (see below for choosing one), then:

```bash
tools/new-app.py "Fan Tools" PPSA12345
```

```
Hello PS5 (PPSA99901) -> Fan Tools (PPSA12345)
  data folder: /data/ps5-starter -> /data/fan-tools
  service:     service.elf -> fan-tools-service.elf
  content ID:  UP9000-PPSA12345_00-FANTOOLS00000001
  ...
  icon: a placeholder with "FT" (replace it with your own 512x512 PNG)
```

In one go, it changes everything that names the app:

- `param.json`: the name, title ID, content ID and concept ID
- the name on screen, in notifications and in the simulator's window
- the data folder in `/data`, made from the name (`/data/fan-tools`), in the
  app, the service and the simulator
- the service's process name (`fan-tools-service.elf`)
- the icon: a placeholder with your initials, until you make a real one

Add `--dry-run` to see the changes first. Run it again any time to rename
again. It checks every change before making any, so a file you edited by hand
can't leave it half done: it stops and tells you which one.

The rest of this chapter explains what it changes and why, and the parts it
leaves to you: the version number, the icon and art, and your code's own names.

## The title ID

The title ID is the app's permanent identity: its folder name on the console
(`/data/homebrew/PPSA99901`) and the `titleId` in `param.json`.

- **Format:** four capital letters and five digits, like `PPSA01234`. PS5 apps
  use `PPSA`.
- **Pick one nobody uses.** Two apps with the same title ID can't both be on
  one console. The
  [ps5-homebrew-catalog](https://github.com/darkxex/ps5-homebrew-catalog/tree/main/apps)
  lists the IDs of many homebrew apps: if `apps/<your ID>.json` exists there,
  pick another.
- **Don't keep a template's ID.** `PPSA99999` is the kit's example, and
  `PPSA99901` is this starter's: everyone who builds them has those.

## param.json

[`app/sce_sys/param.json`](../app/sce_sys/param.json) describes the app to the
console. The fields that make it yours (`tools/new-app.py` sets the first four):

| Field | Starter | Yours |
| --- | --- | --- |
| `titleId` | `PPSA99901` | your title ID |
| `contentId` | `UP9000-PPSA99901_00-HELLOPS500000001` | `UP9000-` + title ID + `_00-` + **exactly 16** capital letters or digits |
| `conceptId` | `99901` | the title ID's five digits |
| `titleName` (under `en-US`) | `Hello PS5` | your app's name |
| `contentVersion` | `01.000.000` | see below |

Leave the other fields as they are: they're the values that work for a
homebrew app.

The build names the app folder and the ZIP after `titleId` by itself.

## Version numbers

`contentVersion` is how a console tells an update from what's installed. It's
always `NN.NNN.NNN`, compared number by number: `01.000.009` < `01.000.010` <
`01.001.000`.

**Raise it in every release**, or consoles won't see the update. A simple
scheme that maps your own version onto it: version 1.4.2 → `01.004.002`, 1.5 →
`01.005.000`.

## The icon and the art

| File | What | Format | Needed |
| --- | --- | --- | --- |
| `sce_sys/icon0.png` | the home screen tile | 512 x 512 PNG | yes |
| `sce_sys/pic0.dds` | background while the app is selected | 3840 x 2160, BC7 DDS, no mipmaps | optional, both or neither |
| `sce_sys/pic1.dds` | background while it starts | same | optional, both or neither |
| `sce_sys/snd0.at9` | music while it's selected | ATRAC9, 48 kHz, up to 2 MB | optional |

The icon is the one that matters: replace `app/sce_sys/icon0.png` with your own
512 x 512 PNG, and keep important things away from its edges.

For the backgrounds and the music, the kit has converters for ordinary images
and audio. They're documented in
[`kit/docs/PRESENTATION_ASSETS.md`](../kit/docs/PRESENTATION_ASSETS.md). The build checks every file's format, so a
wrong size fails the build instead of showing a broken tile.

## Your own names everywhere

`tools/new-app.py` renames what identifies the app. What it leaves to you:

- **The service's port:** 8080 is popular, so pick something less common.
  Change `PORT` in `service.c` and `service_port` in `hello.hpp`.
- **The code:** rename `hello.cpp` and the `starter::` namespace to whatever
  your app is. The app build takes every file in `app/src`; the preview lists
  its files in `app/preview/run.sh`, so add new ones there.
- **The rest of the text on screen,** obviously.

Next: [Share it](12-share-it.md)
