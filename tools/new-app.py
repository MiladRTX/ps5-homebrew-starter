#!/usr/bin/env python3
"""Makes the starter yours: gives the app your name and title ID everywhere
it appears, in one go (docs/11-make-it-yours.md).

    tools/new-app.py "My App" PPSA12345
    tools/new-app.py "My App" PPSA12345 --dry-run     only show what would change

What it changes:
  - app/sce_sys/param.json: titleName, titleId, contentId, conceptId
  - the name on screen, in notifications and in the simulator's window
  - the data folder in /data (from the name: "My App" -> /data/my-app), in the
    app, the service and the simulator
  - the service's name ("my-app-service", shortened for long names: process
    lists show 19 characters)
  - app/sce_sys/icon0.png: a placeholder with your app's initials, unless you
    made your own (--new-icon makes a placeholder anyway)
Run it again to change them again: it reads what they are now. Run it before
you rename the starter's files: it looks for them by name.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import hashlib
import json
import os
import pathlib
import re
import signal
import sys

root = pathlib.Path(__file__).resolve().parent.parent
PARAM = root / "app/sce_sys/param.json"
ICON = root / "app/sce_sys/icon0.png"
# The icon the starter comes with: replaced like a placeholder.
STARTER_ICON_SHA256 = "4b1ad2ad97e4b94dd3db1b8343cd3fa94d4a017639a07281437e418aee75e1fb"
# Where the code says where its data folder is, for the check after the edits.
CODE = ["app/src", "app/sim", "app/preview", "payload"]


def fail(text):
    sys.exit(f"new-app: {text}")


def read(rel):
    try:
        return (root / rel).read_text(encoding="utf-8")
    except OSError:
        fail(f"{rel} is missing or unreadable: renamed or moved? Change the names there yourself.")


def current_identity():
    param = json.loads(read("app/sce_sys/param.json"))
    slug = re.search(r'#define STARTER_APP_DATA STARTER_DATA "/([a-z0-9-]+)"', read("app/src/paths.hpp"))
    name = re.search(r'#define SERVICE_NAME "([a-z0-9-]+)"', read("payload/service/service_version.h"))
    if not slug or not name:
        fail("couldn't find the data folder (paths.hpp) or the service name (service_version.h): "
             "were they edited by hand?")
    return {
        "name": param["localizedParameters"]["en-US"]["titleName"],
        "title_id": param["titleId"],
        "slug": slug.group(1),
        "service": name.group(1),
    }


def slug_of(name):
    return re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")


def service_of(slug):
    """The service's name: with ".elf", at most 19 characters, which is all a
    process list shows (FreeBSD's limit)."""
    return slug + "-service" if len(slug) <= 7 else slug[:15].rstrip("-")


def content_label(name):
    """The 16 capitals or digits at the end of the content ID: "Hello PS5" ->
    HELLOPS500000001, the same pattern as the starter's."""
    base = re.sub(r"[^A-Z0-9]", "", name.upper())[:8] or "APP"
    return base + "0" * (15 - len(base)) + "1"


def edits(old, new):
    """(file, old text, new text, how many times it must appear)."""
    o, n = old, new
    return [
        ("app/src/paths.hpp", f'STARTER_DATA "/{o["slug"]}"', f'STARTER_DATA "/{n["slug"]}"', 1),
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
        ("payload/service/service_version.h", f'#define SERVICE_NAME "{o["service"]}"',
         f'#define SERVICE_NAME "{n["service"]}"', 1),
    ]


def leftovers(slug, planned):
    """Code that still names the old data folder after the edits: a file this
    script doesn't know, such as a page you added."""
    found = []
    for folder in CODE:
        for path in sorted((root / folder).rglob("*")):
            if path.suffix not in (".c", ".h", ".cpp", ".hpp", ".sh", ".py") or path.name.startswith("stb_"):
                continue
            rel = path.relative_to(root).as_posix()
            text = planned.get(rel) or path.read_text(encoding="utf-8", errors="replace")
            for number, line in enumerate(text.splitlines(), 1):
                if f"data/{slug}" in line or f'STARTER_DATA "/{slug}' in line:
                    found.append(f"{rel}:{number}: {line.strip()}")
    return found


def stop_pc_service(slug):
    """Stops the simulator's service before its folder is renamed: after the
    rename, app/sim/run.sh stop couldn't find it, and it would keep port 8080."""
    lock = root / "build/sim/data" / slug / "service.lock"
    try:
        import fcntl
        with open(lock, "r+") as f:
            pid = int(f.read(16).strip() or 0)
            try:
                fcntl.flock(f, fcntl.LOCK_EX | fcntl.LOCK_NB)
                return  # nobody holds the lock: it isn't running
            except OSError:
                pass
        if pid > 1:
            os.kill(pid, signal.SIGTERM)
            print(f"  stopped the old PC service (pid {pid})")
    except (OSError, ValueError, ImportError):
        pass


def placeholder(path):
    """Is the icon one to replace: missing, the starter's own, or one this
    script made? Anything else is yours, and is kept."""
    if not path.exists():
        return True
    if hashlib.sha256(path.read_bytes()).hexdigest() == STARTER_ICON_SHA256:
        return True
    try:
        from PIL import Image
        with Image.open(path) as im:
            return im.info.get("new-app") == "placeholder"
    except Exception:
        return False


def make_icon(name, path):
    """A placeholder icon: the app's initials on the starter's colours."""
    try:
        from PIL import Image, ImageDraw, ImageFont, PngImagePlugin
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
    mark = PngImagePlugin.PngInfo()
    mark.add_text("new-app", "placeholder")  # so a later run knows it may replace it
    im.resize((512, 512), Image.LANCZOS).save(path, optimize=True, pnginfo=mark)
    print(f"  icon: a placeholder with \"{initials}\" (replace it with your own 512x512 PNG)")


def main():
    parser = argparse.ArgumentParser(description="Give the starter your app's name and title ID.")
    parser.add_argument("name", help='your app\'s name, e.g. "My App"')
    parser.add_argument("title_id", help="PPSA and five digits, e.g. PPSA12345")
    parser.add_argument("--dry-run", action="store_true", help="show the changes, change nothing")
    parser.add_argument("--new-icon", action="store_true", help="make a placeholder icon even over your own")
    args = parser.parse_args()

    name = args.name.strip()
    # The name goes into C strings and a shell script: nothing they'd read as code.
    if not 1 <= len(name) <= 40 or not all(32 <= ord(ch) < 127 for ch in name) or set(name) & set('"\\$`'):
        fail("the name: 1 to 40 plain characters (letters, digits, spaces...), "
             "no quotes, backslashes, $ or backticks")
    if not slug_of(name):
        fail("the name needs at least one letter or digit")
    if not re.fullmatch(r"PPSA\d{5}", args.title_id):
        fail("the title ID: PPSA and five digits, like PPSA12345 (the kit only builds PPSA IDs)")
    if args.title_id == "PPSA99999":
        fail("PPSA99999 is the kit's example ID, reserved in the homebrew catalog: pick your own")

    old = current_identity()
    new = {"name": name, "title_id": args.title_id, "slug": slug_of(name), "service": service_of(slug_of(name))}
    if args.title_id == "PPSA99901":
        print("note: PPSA99901 is the starter's own ID; everyone who builds the starter has it")

    # Check every edit first: change nothing unless all of them fit.
    planned = {}
    for rel, before, after, count in edits(old, new):
        text = planned.get(rel) or read(rel)
        found = text.count(before)
        if found != count:
            fail(f"{rel}: expected {count}x {before!r}, found {found}x. Edited by hand? Change it there yourself.")
        planned[rel] = text.replace(before, after)
    if old["slug"] != new["slug"]:
        left = leftovers(old["slug"], planned)
        if left:
            fail("these still name the old data folder; make them use STARTER_APP_DATA (paths.hpp), "
                 "then run this again:\n  " + "\n  ".join(left))

    param = json.loads(read("app/sce_sys/param.json"))
    param["titleId"] = new["title_id"]
    param["conceptId"] = new["title_id"][4:]
    param["contentId"] = f"UP9000-{new['title_id']}_00-{content_label(name)}"
    param["localizedParameters"]["en-US"]["titleName"] = name
    new_icon = args.new_icon or placeholder(ICON)

    print(f"{old['name']} ({old['title_id']}) -> {name} ({new['title_id']})")
    print(f"  data folder: /data/{old['slug']} -> /data/{new['slug']}")
    print(f"  service:     {old['service']}.elf -> {new['service']}.elf")
    print(f"  content ID:  {param['contentId']}")
    for rel in sorted(planned):
        print(f"  {rel}")
    print("  app/sce_sys/param.json")
    print("  app/sce_sys/icon0.png (a new placeholder)" if new_icon else "  icon: yours, kept")
    if args.dry_run:
        print("dry run: nothing changed")
        return

    stop_pc_service(old["slug"])
    for rel, text in planned.items():
        (root / rel).write_text(text, encoding="utf-8", newline="\n")
    PARAM.write_text(json.dumps(param, indent=2, ensure_ascii=False) + "\n", encoding="utf-8", newline="\n")
    if new_icon:
        make_icon(name, ICON)
    print(f"\nDone. Build it with app/build.sh: the app folder is build/app/{new['title_id']}.")
    print(f"On the console, the old {old['service']}.elf keeps running until you restart the console")
    print("or stop it in Payload Manager: until then it holds port 8080, and the app says so.")
    print("Pick a title ID nobody else uses: see docs/11-make-it-yours.md.")


if __name__ == "__main__":
    main()
