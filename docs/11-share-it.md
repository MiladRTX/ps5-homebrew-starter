# 11. Share it

Your app works on your console. This chapter gets it onto other people's, as
a GitHub release they can download and install.

## The license

This starter builds on GPL-3.0 code, so your app is GPL-3.0 too. In practice:
publish its source, keep the `LICENSE` file, and keep the credits. Putting the
code on GitHub covers it.

## A GitHub release

1. Put your project on GitHub: everything except `build/` (`.gitignore`
   already leaves it out).
2. Raise `contentVersion` in `app/sce_sys/param.json` and commit it.
3. Build from that commit: `app/build.sh`.
4. Create a release with a tag like `v1.0`, and attach:
   - `build/app/<TITLE ID>.zip`: the app. Keep the file name, and the folder
     at the top of the ZIP, exactly as the build makes them.
   - your payload's `.elf`, if it's useful on its own.
5. Write short release notes: what's new, and how to install it.

With the GitHub CLI, steps 4 and 5 are one command:

```bash
gh release create v1.0 build/app/PPSA01234.zip --title "My App 1.0" --notes "First release"
```

**Never replace a release's file after you've shared it.** Everyone who
downloaded 1.0 should have the same 1.0. Fixed something? Make a new release.

## Installing it

People install your app the way you did in chapter 5: unzip it, copy the
folder to `/data/homebrew/` on the console, and wait a few seconds for it to
appear on the home screen. Put those steps in your README and in your release
notes.

## Updating

Every update is the same:

1. Raise `contentVersion` in `app/sce_sys/param.json` (chapter 10).
2. Build, and make a new release with a new tag.

Users copy the new folder over the old one, after closing the app. If your app
carries a payload, have it replace the old one that's still running (chapter 8).

Next: [Troubleshooting](12-troubleshooting.md)
