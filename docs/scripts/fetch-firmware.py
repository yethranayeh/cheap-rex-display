#!/usr/bin/env python3
# Usage: python3 scripts/fetch-firmware.py [--repo OWNER/NAME] [TAG]
#
# Downloads a release's firmware into public/firmware/ so the site serves it
# itself: ESP Web Tools fetches the manifest and the parts from the page, and
# GitHub's release downloads send no CORS headers. Takes the latest release
# without TAG (prereleases need their tag), checks each part against the size
# and sha256 in firmware.json, and replaces whatever public/firmware/ held.
# The merged image stays on the release, for esptool: the page links to it.
# Set GITHUB_TOKEN to read a private repository's releases.

import hashlib
import json
import os
import shutil
import sys
import urllib.error
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT = HERE.parent / "public" / "firmware"
REPO = "yethranayeh/cheap-rex-display"
API = "https://api.github.com/repos"


def get(url, accept):
	req = urllib.request.Request(url, headers={"Accept": accept, "User-Agent": "cheap-rex-display"})
	token = os.environ.get("GITHUB_TOKEN")
	if token:
		# Asset downloads redirect to storage that refuses a second credential
		req.add_unredirected_header("Authorization", f"Bearer {token}")
	try:
		with urllib.request.urlopen(req) as res:
			return res.read()
	except urllib.error.HTTPError as e:
		sys.exit(f"{url}: {e.code} {e.reason}")


def main():
	args = sys.argv[1:]
	repo = REPO
	if args[:1] == ["--repo"] and len(args) >= 2:
		repo, args = args[1], args[2:]
	if len(args) > 1 or (args and args[0].startswith("-")):
		sys.exit("usage: fetch-firmware.py [--repo OWNER/NAME] [TAG]")

	which = f"tags/{args[0]}" if args else "latest"
	release = json.loads(get(f"{API}/{repo}/releases/{which}", "application/vnd.github+json"))
	assets = {a["name"]: a["url"] for a in release["assets"]}

	def asset(name):
		if name not in assets:
			sys.exit(f"{release['tag_name']} has no {name}; was it built by the release workflow?")
		return get(assets[name], "application/octet-stream")

	firmware = json.loads(asset("firmware.json"))
	manifest = json.loads(asset("manifest.json"))
	flashed = [(p["path"], p["offset"]) for b in manifest["builds"] for p in b["parts"]]
	if flashed != [(p["file"], p["offset"]) for p in firmware["parts"]]:
		sys.exit(f"manifest.json flashes {flashed}, not the parts in firmware.json")
	images = {}
	for part in firmware["parts"]:
		data = asset(part["file"])
		if len(data) != part["size"] or hashlib.sha256(data).hexdigest() != part["sha256"]:
			sys.exit(f"{part['file']} doesn't match the size and sha256 in firmware.json")
		images[part["file"]] = data

	shutil.rmtree(OUT, ignore_errors=True)
	OUT.mkdir(parents=True)
	for file, data in images.items():
		(OUT / file).write_bytes(data)
	for name, value in (("manifest.json", manifest), ("firmware.json", firmware)):
		(OUT / name).write_text(json.dumps(value, indent=2) + "\n")
	size = sum(len(data) for data in images.values())
	print(f"{release['tag_name']}: {len(images)} parts ({size} bytes) into {OUT}")


main()
