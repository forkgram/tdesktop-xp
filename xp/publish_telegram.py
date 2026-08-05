#!/usr/bin/env python3
"""
Publish the XP auto-update package to the Telegram update channels.

The client reads the feed channel's latest message over MTProto - a JSON map of
platform -> channel -> type -> "<version>:<files channel>#<message id>" - and
downloads the referenced document. So: upload the update file to the files
channel, then post one feed message pointing at the upload.

Two things this must get right, because the feed is SHARED with the Windows 7+
and Linux releases published from forkgram/tdesktop:

  * the platform keys are `winxp` and `winxp64`, which only this port asks for
    (see update_checker.cpp) - a Windows 7+ build reads `win`/`win64` and never
    sees these packages, and an XP build never sees theirs. The two XP builds
    are separate downloads: an x86 package runs on XP x64 through WOW64, so
    offering it there would silently move that machine onto the 32-bit line;
  * the message carries every platform at once, so the previous feed JSON is
    MERGED, never replaced. Dropping their keys would stop updates for everyone
    else until their next release.

Whichever of the two packages the artifacts directory holds gets published -
both, when a run built both - and they go out in ONE feed message.

  TG_SESSION, TG_FEED_CHANNEL, TG_FILES_CHANNEL, TG_API_ID, TG_API_HASH
  ARTIFACTS_DIR   where to look for txpupd/txp64upd<version> (default: artifacts)
  TG_ENTRY_KEY    released (default) | testing - testing leaves released users put
  TG_SCHEDULE_DAYS  >0 sends everything that many days into the future: a real
                    send that appears nowhere yet, for rehearsing the whole path
  TG_DRY_RUN=1    resolve, print the feed JSON, upload and post nothing
"""
import os
import re
import sys
import json
import glob
import asyncio
from datetime import datetime, timedelta, timezone

from telethon import TelegramClient
from telethon.sessions import StringSession

FEED = os.environ["TG_FEED_CHANNEL"]
FILES = os.environ["TG_FILES_CHANNEL"]
ARTIFACTS_DIR = os.environ.get("ARTIFACTS_DIR", "artifacts")
ENTRY_KEY = os.environ.get("TG_ENTRY_KEY", "released")
DRY_RUN = os.environ.get("TG_DRY_RUN", "") == "1"
SCHEDULE_DAYS = int(os.environ.get("TG_SCHEDULE_DAYS", "0") or "0")

# Platform::AutoUpdateKey() says "win" for any x86 build, so the XP port does not
# use it - update_checker.cpp asks the feed for one of these keys instead, chosen
# at compile time. Packer writes the matching name (see packer.cpp -target).
PLATFORMS = (
    ("winxp", re.compile(r"^txpupd(\d+)$")),
    ("winxp64", re.compile(r"^txp64upd(\d+)$")),
)


def find_update_files(root):
    """Return [(platform, version:int, path)] for the packages under root.

    Zero packages is a failure - the caller asked for a publish. Two packages
    for the SAME platform is also a failure: it means the artifacts of two runs
    got mixed, and picking one silently would publish an unknown version.
    """
    found = []
    for platform, pattern in PLATFORMS:
        matches = []
        for path in sorted(glob.glob(os.path.join(root, "**", "*"), recursive=True)):
            if not os.path.isfile(path):
                continue
            m = pattern.match(os.path.basename(path))
            if m:
                matches.append((int(m.group(1)), path))
        if len(matches) > 1:
            sys.exit(f"More than one {platform} update file: "
                     + ", ".join(p for _, p in matches))
        if matches:
            found.append((platform, matches[0][0], matches[0][1]))
    if not found:
        names = "/".join(p for p, _ in PLATFORMS)
        sys.exit(f"No {names} package found under {ARTIFACTS_DIR!r}.")
    return found


def load_previous_feed(text):
    if not text:
        return {}
    try:
        data = json.loads(text)
    except json.JSONDecodeError:
        # Refuse to guess: replacing an unreadable feed would drop every other
        # platform's entry. A human decides what happened.
        sys.exit("The latest feed message is not JSON - refusing to overwrite it.")
    if not isinstance(data, dict):
        sys.exit("The latest feed message is not a JSON object - refusing.")
    return data


async def main():
    packages = find_update_files(ARTIFACTS_DIR)
    for platform, version, path in packages:
        size = os.path.getsize(path) / 1048576
        print(f"{platform}: version {version}, {size:.0f} MiB, {path}")

    api_id = int(os.environ["TG_API_ID"])
    api_hash = os.environ["TG_API_HASH"]
    session = os.environ["TG_SESSION"]

    async with TelegramClient(StringSession(session), api_id, api_hash) as client:
        feed = await client.get_entity(FEED)
        files = await client.get_entity(FILES)

        previous = await client.get_messages(feed, limit=1)
        prev_msg = previous[0] if previous else None
        merged = load_previous_feed(prev_msg.message if prev_msg else "")
        publishing = {p for p, _, _ in packages}
        others = sorted(k for k in merged if k not in publishing)
        print("platforms already in the feed: " + (", ".join(others) or "none"))
        print("publishing: " + ", ".join(sorted(publishing)))

        when = None
        if SCHEDULE_DAYS > 0:
            when = datetime.now(timezone.utc) + timedelta(days=SCHEDULE_DAYS)
            print(f"Scheduling for {when:%Y-%m-%d} ({SCHEDULE_DAYS} days out); "
                  "nothing appears in the channels now.")

        # Four keys per platform (beta/stable x released/testing). A released
        # build lands in all of them; a testing build only in the testing keys,
        # which leaves released users on the previous version.
        if ENTRY_KEY == "testing":
            targets = [("beta", "testing"), ("stable", "testing")]
        else:
            targets = [(chan, key)
                       for chan in ("beta", "stable")
                       for key in ("released", "testing")]

        for platform, version, path in packages:
            if DRY_RUN:
                entry = f"{version}:{FILES}#<dry-run>"
                print(f"[dry-run] would upload {path}")
            else:
                msg = await client.send_file(
                    files, path,
                    force_document=True,
                    caption="",
                    schedule=when)
                entry = f"{version}:{FILES}#{msg.id}"
                print(f"uploaded {platform}: {entry}")

            entry_map = merged.setdefault(platform, {})
            for chan, key in targets:
                entry_map.setdefault(chan, {})[key] = entry

        text = json.dumps(merged, separators=(",", ":"), sort_keys=True)
        print("\nFeed JSON:")
        print(text)

        # A safety net for the one thing that must never happen: losing another
        # platform's entry. Compare against what we read, not against a guess.
        for key in others:
            if key not in merged:
                sys.exit(f"BUG: platform {key} disappeared from the merged feed.")

        if DRY_RUN:
            print("\n[dry-run] not posting the feed message.")
            return

        # XP releases go out on their own schedule, far apart from the Windows 7+
        # ones, so there is no "same release wave" to edit into: always post a
        # fresh message. It carries every platform because of the merge above,
        # and the client only ever reads the latest one.
        posted = await client.send_message(feed, text, schedule=when)
        print(f"\n{'scheduled' if when else 'posted'} feed message "
              f"#{posted.id} to {FEED}.")


if __name__ == "__main__":
    asyncio.run(main())
