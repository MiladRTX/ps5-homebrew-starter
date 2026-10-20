#!/usr/bin/env python3
"""Makes the starter yours: gives the app your name and title ID everywhere
it appears, in one go (docs/10-make-it-yours.md).

    tools/new-app.py "My App" PPSA12345
    tools/new-app.py "My App" PPSA12345 --dry-run     only show what would change

What it changes:
  - app/sce_sys/param.json: titleName, titleId, contentId, conceptId
  - the name on screen, in notifications and in the simulator's window
  - the data folder in /data (from the name: "My App" -> /data/my-app), in the
    app, the service and the simulator
  - the service's process name ("my-app-service.elf")
  - app/sce_sys/icon0.png: a placeholder with your app's initials
Run it again to change them again: it reads what they are now.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import json
import pathlib
import re
import sys

root = pathlib.Path(__file__).resolve().parent.parent
PARAM = root / "app/sce_sys/param.json"


def fail(text):
    sys.exit(f"new-app: {text}")


def current_identity():
    param = json.loads(PARAM.read_text(encoding="utf-8"))
    hello = (root / "app/src/hello.cpp").read_text(encoding="utf-8")
    service = (root / "payload/service/service.c").read_text(encoding="utf-8")
    slug = re.search(r'data_dir = STARTER_DATA "/([a-z0-9-]+)"', hello)
    name = re.search(r'#define NAME "([a-z0-9-]+)"', service)
    if not slug or not name:
        fail("couldn't find the data folder or the service name: were they edited by hand?")
    return {
        "name": param["localizedParameters"]["en-US"]["titleName"],
        "title_id": param["titleId"],
        "slug": slug.group(1),
        "service": name.group(1),
    }


def slug_of(name):
    return re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")


def content_label(name):
    """The 16 capitals or digits at the end of the content ID: "Hello PS5" ->
    HELLOPS500000001, the same pattern as the starter's."""
    base = re.sub(r"[^A-Z0-9]", "", name.upper())[:8] or "APP"
    return base + "0" * (15 - len(base)) + "1"


def edits(old, new):
    """(file, old text, new text, how many times it must appear)."""
    o, n = old, new
    return [
        ("app/src/hello.cpp", f'STARTER_DATA "/{o["slug"]}', f'STARTER_DATA "/{n["slug"]}', 2),
        ("app/src/hello.cpp", f'c.text(margin, 118, "{o["name"]}"', f'c.text(margin, 118, "{n["name"]}"', 1),
        ("app/src/main.cpp", f'"{o["name"]}: the app\'s fonts are missing"',
         f'"{n["name"]}: the app\'s fonts are missing"', 1),
        ("app/src/page_play.cpp", f'"Hello from {o["name"]}!"', f'"Hello from {n["name"]}!"', 1),
        ("app/src/page_settings.cpp", f'"saved in /data/{o["slug"]}"', f'"saved in /data/{n["slug"]}"', 1),
        ("app/src/screen.cpp", f'halt("{o["name"]}: ', f'halt("{n["name"]}: ', 6),
        ("app/src/link.cpp", f'its own log in data/{o["slug"]} anyway', f'its own log in data/{n["slug"]} anyway', 1),
        ("app/sim/screen_sim.cpp", f'"{o["name"]} - PC simulator"', f'"{n["name"]} - PC simulator"', 1),
        ("app/sim/run.sh", f'data/{o["slug"]}/service.lock', f'data/{n["slug"]}/service.lock', 1),
        ("app/sim/run.sh", f"""-DDATA_DIR='"data/{o["slug"]}"'""", f"""-DDATA_DIR='"data/{n["slug"]}"'""", 1),
        ("app/sim/run.sh", f'echo "{o["name"]} on your PC.', f'echo "{n["name"]} on your PC.', 1),
        ("payload/service/service.c", f'#define DATA_DIR "/data/{o["slug"]}"', f'#define DATA_DIR "/data/{n["slug"]}"', 1),
        ("payload/service/service.c", f'#define NAME "{o["service"]}"', f'#define NAME "{n["service"]}"', 1),
    ]


def make_icon(name, path):
    """A placeholder icon: the app's initials on the starter's colours."""
    try:
        from PIL import Image, ImageDraw, ImageFont
    except ImportError:
        print("  (no Pillow: kept the old icon; replace app/sce_sys/icon0.png yourself)")
        return
    words = [w for w in re.split(r"[^A-Za-z0-9]+", name) if w]
    initials = ("".join(w[0] for w in words[:2]) if len(words) > 1 else name[:2]).upper() or "?"
    s = 4  # draw big, scale down: smooth edges
    im = Image.new("RGB", (512 * s, 512 * s), (11, 13, 16))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((116 * s, 116 * s, 396 * s, 396 * s), radius=56 * s, fill=(142, 162, 255))
    font = ImageFont.truetype(str(root / "app/fonts/Inter-Bold.ttf"), 140 * s)
    box = d.textbbox((0, 0), initials, font=font)
    w, h = box[2] - box[0], box[3] - box[1]
    d.text(((512 * s - w) / 2 - box[0], (512 * s - h) / 2 - box[1]), initials, font=font, fill=(11, 13, 16))
    im.resize((512, 512), Image.LANCZOS).save(path, optimize=True)
    print(f"  icon: a placeholder with \"{initials}\" (replace it with your own 512x512 PNG)")


def main():
    parser = argparse.ArgumentParser(description="Give the starter your app's name and title ID.")
    parser.add_argument("name", help='your app\'s name, e.g. "My App"')
    parser.add_argument("title_id", help="four capital letters and five digits, e.g. PPSA12345")
    parser.add_argument("--dry-run", action="store_true", help="show the changes, change nothing")
    args = parser.parse_args()

    name = args.name.strip()
    if not 1 <= len(name) <= 40 or not all(32 <= ord(ch) < 127 for ch in name) or set(name) & set('"\\'):
        fail("the name: 1 to 40 plain characters (letters, digits, spaces...), no quotes or backslashes")
    if not slug_of(name):
        fail("the name needs at least one letter or digit")
    if not re.fullmatch(r"[A-Z]{4}\d{5}", args.title_id):
        fail("the title ID: four capital letters and five digits, like PPSA12345")
    if args.title_id == "PPSA99999":
        fail("PPSA99999 is the kit's example ID, reserved in the homebrew catalog: pick your own")

    old = current_identity()
    new = {"name": name, "title_id": args.title_id, "slug": slug_of(name),
           "service": slug_of(name) + "-service"}
    if args.title_id == "PPSA99901":
        print("note: PPSA99901 is the starter's own ID; everyone who builds the starter has it")

    # Check every edit first: change nothing unless all of them fit.
    planned = {}
    for rel, before, after, count in edits(old, new):
        text = planned.get(rel) or (root / rel).read_text(encoding="utf-8")
        found = text.count(before)
        if found != count:
            fail(f"{rel}: expected {count}x {before!r}, found {found}x. Edited by hand? Change it there yourself.")
        planned[rel] = text.replace(before, after)

    param = json.loads(PARAM.read_text(encoding="utf-8"))
    param["titleId"] = new["title_id"]
    param["conceptId"] = new["title_id"][4:]
    param["contentId"] = f"UP9000-{new['title_id']}_00-{content_label(name)}"
    param["localizedParameters"]["en-US"]["titleName"] = name

    print(f"{old['name']} ({old['title_id']}) -> {name} ({new['title_id']})")
    print(f"  data folder: /data/{old['slug']} -> /data/{new['slug']}")
    print(f"  service:     {old['service']}.elf -> {new['service']}.elf")
    print(f"  content ID:  {param['contentId']}")
    for rel in sorted(planned):
        print(f"  {rel}")
    if args.dry_run:
        print("dry run: nothing changed")
        return

    for rel, text in planned.items():
        (root / rel).write_text(text, encoding="utf-8", newline="\n")
    PARAM.write_text(json.dumps(param, indent=2, ensure_ascii=False) + "\n", encoding="utf-8", newline="\n")
    print("  app/sce_sys/param.json")
    make_icon(name, root / "app/sce_sys/icon0.png")
    print(f"\nDone. Build it with app/build.sh: the app folder is build/app/{new['title_id']}.")
    print("Pick a title ID nobody else uses: see docs/10-make-it-yours.md.")


if __name__ == "__main__":
    main()
