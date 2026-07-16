#!/usr/bin/env python3
"""Bulk-upload MIDI songs to a Via Lucis device over its REST API.

The device-recovery half of BUGFIX-PLAN-2026-07-15 §1.5: after a reflash,
optionally format the wedged filesystem, then re-upload every .mid found
under a source folder (recursively). Stdlib only.

Usage:
  python tools/bulk_upload.py --src "C:/Users/omega/Downloads/midis fo Via Lucis" \
      [--device http://192.168.1.191] [--format] [--dry-run]

Behavior:
  - GET /api/songs first; a file whose sanitized name is already on the device
    with the same byte size is skipped (name+size is an imperfect key — pass
    --force to re-upload everything).
  - --format sends POST /api/storage/format {"confirm":"ERASE"} and polls
    GET /api/status until the filesystem reports healthy and empty.
  - Uploads are whole-file with up to 3 attempts each; every upload is
    verified by re-reading the song list and comparing sizes.
  - A 507 (out of space) stops the run — the device set is full; curate.
"""

import argparse
import json
import sys
import time
import unicodedata
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

ALLOWED = set("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_ .")
MAX_SONG_BYTES = 256 * 1024


def sanitize_name(raw: str):
    """Mirror webui sanitization: NFD-transliterate, .mid lowercase ext,
    charset [A-Za-z0-9-_ .], length 5-64. Returns (name, reason)."""
    name = unicodedata.normalize("NFD", raw.strip())
    name = "".join(c for c in name if not unicodedata.combining(c))
    lower = name.lower()
    if lower.endswith(".midi"):
        name = name[:-5] + ".mid"
    elif lower.endswith(".mid"):
        name = name[:-4] + ".mid"
    else:
        return None, "not a .mid file"
    if any(c not in ALLOWED for c in name):
        bad = sorted({c for c in name if c not in ALLOWED})
        return None, "disallowed characters: %r" % (bad,)
    if not 5 <= len(name) <= 64:
        return None, "name length %d not in 5-64" % len(name)
    return name, None


def api(device: str, method: str, path: str, body: bytes = None, timeout: float = 15.0):
    req = urllib.request.Request(device + path, data=body, method=method)
    if body is not None:
        req.add_header("Content-Type", "application/octet-stream")
    with urllib.request.urlopen(req, timeout=timeout) as r:
        text = r.read().decode("utf-8", "replace")
    return json.loads(text) if text else None


def get_songs(device: str):
    return {s["name"]: s["size"] for s in api(device, "GET", "/api/songs")}


def do_format(device: str):
    print("Formatting device filesystem (POST /api/storage/format)...")
    api(device, "POST", "/api/storage/format",
        json.dumps({"confirm": "ERASE"}).encode())
    deadline = time.time() + 60
    while time.time() < deadline:
        time.sleep(2)
        try:
            st = api(device, "GET", "/api/status")
        except (urllib.error.URLError, OSError):
            continue  # device busy formatting
        if st.get("fs") == "ok" and not get_songs(device):
            print("Format complete: fs ok, song list empty, %d KB free."
                  % (st.get("fsFree", 0) // 1024))
            return True
    print("ERROR: format did not complete within 60 s", file=sys.stderr)
    return False


def upload_one(device: str, name: str, data: bytes, attempts: int = 3):
    q = urllib.parse.quote(name)
    for attempt in range(1, attempts + 1):
        try:
            api(device, "POST", "/api/songs?name=" + q, data, timeout=60)
            on_device = get_songs(device)
            if on_device.get(name) == len(data):
                return "ok"
            print("  verify mismatch (attempt %d): device has %s, want %d"
                  % (attempt, on_device.get(name), len(data)))
        except urllib.error.HTTPError as e:
            detail = e.read().decode("utf-8", "replace")
            if e.code == 507:
                return "full"
            if e.code == 409:
                return "loaded"
            print("  HTTP %d (attempt %d): %s" % (e.code, attempt, detail.strip()))
        except (urllib.error.URLError, OSError) as e:
            print("  network error (attempt %d): %s" % (attempt, e))
        time.sleep(1.5 * attempt)
    return "failed"


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--device", default="http://192.168.1.191")
    p.add_argument("--src", required=True, help="folder scanned recursively for .mid/.midi")
    p.add_argument("--format", action="store_true", help="format device FS first (ERASES ALL)")
    p.add_argument("--force", action="store_true", help="upload even if name+size already match")
    p.add_argument("--dry-run", action="store_true")
    args = p.parse_args()
    device = args.device.rstrip("/")

    files = sorted(q for q in Path(args.src).rglob("*")
                   if q.suffix.lower() in (".mid", ".midi") and q.is_file())
    if not files:
        print("No .mid files under %s" % args.src, file=sys.stderr)
        return 1

    st = api(device, "GET", "/api/status")
    print("Device %s: fs=%s, %d KB free of %d KB"
          % (device, st.get("fs"), st.get("fsFree", 0) // 1024,
             st.get("fsTotal", 0) // 1024))

    if args.format and not args.dry_run:
        if not do_format(device):
            return 1

    on_device = {} if (args.format and not args.dry_run) else get_songs(device)
    ok = skipped = failed = 0
    for f in files:
        name, reason = sanitize_name(f.name)
        if name is None:
            print("SKIP %-50s (%s)" % (f.name, reason))
            failed += 1
            continue
        data = f.read_bytes()
        if len(data) > MAX_SONG_BYTES:
            print("SKIP %-50s (%d KB > 256 KB device cap)" % (name, len(data) // 1024))
            skipped += 1
            continue
        if not args.force and on_device.get(name) == len(data):
            skipped += 1
            continue
        if args.dry_run:
            print("WOULD UPLOAD %-42s %6d bytes" % (name, len(data)))
            ok += 1
            continue
        print("UPLOAD %-48s %6d bytes ... " % (name, len(data)), end="", flush=True)
        result = upload_one(device, name, data)
        if result == "ok":
            print("ok")
            ok += 1
        elif result == "full":
            print("507 OUT OF SPACE — stopping (curate the device set)")
            failed += 1
            break
        elif result == "loaded":
            print("409 currently loaded — unload it in the UI, rerun to retry")
            failed += 1
        else:
            print("FAILED after retries")
            failed += 1

    st = api(device, "GET", "/api/status")
    print("\nDone: %d uploaded, %d skipped, %d failed. Device: %d songs, %d KB free."
          % (ok, skipped, failed, len(get_songs(device)),
             st.get("fsFree", 0) // 1024))
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
