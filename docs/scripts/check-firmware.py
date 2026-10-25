#!/usr/bin/env python3
# Usage: python3 scripts/check-firmware.py [dist]
#
# Checks the firmware the built site serves, so run it after `bun run build`:
# the manifest flashes the parts in firmware.json at their offsets, each part
# is there with its size and sha256, and the install page has the button for
# that manifest and shows every sha256, the merged image's included.
# Without firmware/ in the build the install page points to building from
# source instead, which passes with a note.

import hashlib
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
DIST = HERE.parent / "dist"


def check(dist):
	firmware_dir = dist / "firmware"
	page = (dist / "install" / "index.html").read_text()
	if not firmware_dir.exists():
		if "<esp-web-install-button" in page:
			return ["no firmware/ in the build, yet the install page has the button"]
		print("no firmware/ in the build: the install page points to building from source")
		return []

	problems = []
	firmware = json.loads((firmware_dir / "firmware.json").read_text())
	manifest = json.loads((firmware_dir / "manifest.json").read_text())

	flashed = [(p["path"], p["offset"]) for b in manifest["builds"] for p in b["parts"]]
	parts = [(p["file"], p["offset"]) for p in firmware["parts"]]
	if flashed != parts:
		problems.append(f"manifest.json flashes {flashed}, not {parts}")
	if manifest["name"] != firmware["name"] or manifest["version"] != firmware["version"]:
		problems.append("manifest.json and firmware.json name different firmware")

	for part in firmware["parts"]:
		image = firmware_dir / part["file"]
		if not image.exists():
			problems.append(f"no {part['file']}")
			continue
		data = image.read_bytes()
		if len(data) != part["size"] or hashlib.sha256(data).hexdigest() != part["sha256"]:
			problems.append(f"{part['file']} doesn't match the size and sha256 in firmware.json")

	if 'manifest="/firmware/manifest.json"' not in page:
		problems.append("the install page has no button for /firmware/manifest.json")
	for part in firmware["parts"] + [firmware["merged"]]:
		if part["sha256"] not in page:
			problems.append(f"the install page doesn't show the sha256 of {part['file']}")
	if not problems:
		print(f"ok   firmware/ has {len(parts)} parts ({firmware['tag']})")
	return problems


def main():
	dist = Path(sys.argv[1]) if len(sys.argv) > 1 else DIST
	if not (dist / "install" / "index.html").exists():
		sys.exit(f"no install page in {dist}; run `bun run build` first")
	problems = check(dist)
	for problem in problems:
		print(f"FAIL {problem}")
	sys.exit(1 if problems else 0)


main()
