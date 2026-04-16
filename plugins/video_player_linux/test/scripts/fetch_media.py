#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2024 Toyota Connected North America
"""Fetch the video_player_linux test media corpus.

Reads ../media/manifest.json, downloads each entry into the media directory,
and verifies the SHA-256 against the manifest. Files are not committed to
the repository; this script is the source of truth for populating them.

Exit codes:
  0  all streams fetched and verified
  1  manifest / IO error
  2  hash mismatch or download failure for at least one stream
  3  no streams had a usable URL (all placeholders)

Typical use:
  # First-time populate + pin hashes into the manifest
  scripts/fetch_media.py --learn

  # Subsequent runs: verify against pinned hashes (CI)
  scripts/fetch_media.py

  # Fetch only a subset
  scripts/fetch_media.py --only h264_720p_bt709 --only vp9_1080p
"""

import argparse
import hashlib
import json
import os
import sys
import urllib.error
import urllib.request


CHUNK = 1 << 20  # 1 MiB
SHA256_TBD = "TBD"


def sha256(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as fh:
        for chunk in iter(lambda: fh.read(CHUNK), b""):
            h.update(chunk)
    return h.hexdigest()


def fetch_one(entry: dict, media_dir: str, learn: bool) -> str:
    """Return 'ok', 'skip', or 'fail' for a single manifest entry."""
    name = entry["name"]
    url = entry.get("url")
    if not url:
        print(f"[{name}] SKIP — url is null (placeholder)")
        return "skip"

    target = os.path.join(media_dir, entry["file"])
    expected = entry.get("sha256", SHA256_TBD)

    if os.path.exists(target):
        got = sha256(target)
        if expected == SHA256_TBD:
            if learn:
                entry["sha256"] = got
                print(f"[{name}] learned sha256={got}")
            else:
                print(f"[{name}] present, manifest hash TBD (actual={got})")
            return "ok"
        if got == expected:
            print(f"[{name}] OK (cached)")
            return "ok"
        print(
            f"[{name}] HASH MISMATCH cached={got} expected={expected}",
            file=sys.stderr,
        )
        return "fail"

    print(f"[{name}] fetching {url}")
    try:
        os.makedirs(media_dir, exist_ok=True)
        urllib.request.urlretrieve(url, target)  # noqa: S310 — manifest is trusted
    except (urllib.error.URLError, urllib.error.HTTPError, OSError) as exc:
        print(f"[{name}] DOWNLOAD FAILED: {exc}", file=sys.stderr)
        return "fail"

    got = sha256(target)
    if expected == SHA256_TBD:
        if learn:
            entry["sha256"] = got
            print(f"[{name}] fetched, learned sha256={got}")
        else:
            print(
                f"[{name}] fetched, sha256={got}"
                " (manifest TBD — re-run with --learn to pin)"
            )
        return "ok"
    if got != expected:
        print(
            f"[{name}] HASH MISMATCH got={got} expected={expected}",
            file=sys.stderr,
        )
        os.remove(target)
        return "fail"
    print(f"[{name}] fetched, sha256 verified")
    return "ok"


def main() -> int:
    here = os.path.dirname(os.path.abspath(__file__))
    default_manifest = os.path.abspath(
        os.path.join(here, "..", "media", "manifest.json")
    )

    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument(
        "--manifest",
        default=default_manifest,
        help=f"manifest path (default: {default_manifest})",
    )
    p.add_argument(
        "--media-dir",
        default=None,
        help="directory to fetch into (default: directory containing the manifest)",
    )
    p.add_argument(
        "--learn",
        action="store_true",
        help="record computed sha256 hashes back into the manifest",
    )
    p.add_argument(
        "--only",
        action="append",
        default=[],
        help="restrict to these stream names (may be repeated)",
    )
    args = p.parse_args()

    if not os.path.exists(args.manifest):
        print(f"manifest not found: {args.manifest}", file=sys.stderr)
        return 1
    media_dir = args.media_dir or os.path.dirname(args.manifest)

    with open(args.manifest) as fh:
        manifest = json.load(fh)

    streams = manifest.get("streams", [])
    if args.only:
        wanted = set(args.only)
        streams = [s for s in streams if s["name"] in wanted]
        missing = wanted - {s["name"] for s in streams}
        if missing:
            print(f"unknown stream names: {sorted(missing)}", file=sys.stderr)
            return 1

    if not streams:
        print("no streams matched", file=sys.stderr)
        return 3

    counts = {"ok": 0, "skip": 0, "fail": 0}
    for entry in streams:
        counts[fetch_one(entry, media_dir, learn=args.learn)] += 1

    if args.learn:
        with open(args.manifest, "w", encoding="utf-8") as fh:
            json.dump(manifest, fh, indent=2, ensure_ascii=False)
            fh.write("\n")
        print(f"updated manifest: {args.manifest}")

    print(
        f"summary: ok={counts['ok']} skipped={counts['skip']} failed={counts['fail']}"
    )
    if counts["fail"]:
        return 2
    if counts["ok"] == 0:
        return 3
    return 0


if __name__ == "__main__":
    sys.exit(main())
