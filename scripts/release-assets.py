#!/usr/bin/env python3
# Usage: python3 scripts/release-assets.py TAG [BUILD_DIR BOOT_APP0 OUT_DIR]
#
# With only TAG, checks that the tag is "v" plus FIRMWARE_VERSION from the
# firmware, and exits non-zero if not. The board reports that version over
# Improv, and ESP Web Tools compares it with the manifest's to offer an update,
# so the two must agree.
#
# With BUILD_DIR (arduino-cli's --output-dir), BOOT_APP0 (the core's
# tools/partitions/boot_app0.bin) and OUT_DIR, also writes the release assets
# into OUT_DIR:
#   cheap-rex-display-VERSION-bootloader.bin  at 0x1000
#   cheap-rex-display-VERSION-partitions.bin  at 0x8000
#   cheap-rex-display-VERSION-boot_app0.bin   at 0xe000
#   cheap-rex-display-VERSION-app.bin         at 0x10000
#   cheap-rex-display-VERSION-merged.bin      all of them as one 4 MB image, for esptool at 0
#   manifest.json                             the ESP Web Tools manifest, which flashes the four parts
#   firmware.json                             where and from what they were built, and each one's sha256
# The four parts skip NVS at 0x9000, where the board keeps its Wi-Fi, so an
# update from the install page leaves it alone. The merged image writes the
# whole flash, NVS included. The release workflow runs this; the GITHUB_*
# variables it reads are set there.

import hashlib
import json
import os
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SKETCH = ROOT / "firmware" / "cheap-rex-display" / "cheap-rex-display.ino"

# The offsets the ESP32 core 3.3.7 uploads to and merges at, see its platform.txt
PARTS = (
	("bootloader", "cheap-rex-display.ino.bootloader.bin", 0x1000),
	("partitions", "cheap-rex-display.ino.partitions.bin", 0x8000),
	("boot_app0", None, 0xE000),
	("app", "cheap-rex-display.ino.bin", 0x10000),
)
MERGED = "cheap-rex-display.ino.merged.bin"


def define(source, name):
	m = re.search(rf'^#define {name} "([^"]+)"', source, flags=re.M)
	if not m:
		sys.exit(f"no {name} in {SKETCH}")
	return m.group(1)


def check_tag(tag):
	source = SKETCH.read_text()
	name, version = define(source, "FIRMWARE_NAME"), define(source, "FIRMWARE_VERSION")
	if tag != "v" + version:
		sys.exit(f"tag {tag} doesn't match FIRMWARE_VERSION {version}: tag v{version}, or change the version first")
	return name, version


def provenance():
	server = os.environ.get("GITHUB_SERVER_URL", "https://github.com")
	repo = os.environ.get("GITHUB_REPOSITORY", "")
	run = os.environ.get("GITHUB_RUN_ID", "")
	return {
		"repo": f"{server}/{repo}" if repo else "",
		"commit": os.environ.get("GITHUB_SHA", ""),
		"run": f"{server}/{repo}/actions/runs/{run}" if repo and run else "",
	}


def copy(source, out, file):
	if not source.is_file() or source.stat().st_size == 0:
		sys.exit(f"no {source}")
	shutil.copyfile(source, out / file)
	data = (out / file).read_bytes()
	return {"file": file, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}, data


def write_assets(tag, name, version, build, boot_app0, out):
	out.mkdir(parents=True, exist_ok=True)
	merged, image = copy(build / MERGED, out, f"{name}-{version}-merged.bin")

	parts = []
	for label, built, offset in PARTS:
		part, data = copy(build / built if built else boot_app0, out, f"{name}-{version}-{label}.bin")
		# The parts must be what the merged image holds, so both installs flash the same firmware
		if image[offset : offset + len(data)] != data:
			sys.exit(f"{part['file']} isn't what {merged['file']} holds at {offset:#x}")
		parts.append({"offset": offset} | part)
	for part, after in zip(parts, parts[1:]):
		if part["offset"] + part["size"] > after["offset"]:
			sys.exit(f"{part['file']} runs into {after['file']}")

	manifest = {
		"name": name,
		"version": version,
		# A new install erases the flash first, an update writes only the parts
		"new_install_prompt_erase": False,
		"builds": [{"chipFamily": "ESP32", "parts": [{"path": p["file"], "offset": p["offset"]} for p in parts]}],
	}
	firmware = {
		"name": name,
		"version": version,
		"tag": tag,
		"parts": parts,
		"merged": merged,
	} | provenance()

	for path, value in ((out / "manifest.json", manifest), (out / "firmware.json", firmware)):
		path.write_text(json.dumps(value, indent=2) + "\n")
	for part in parts + [merged]:
		print(f"{part.get('offset', 0):#08x} {part['file']}: {part['size']} bytes, sha256 {part['sha256']}")


def main():
	if len(sys.argv) not in (2, 5):
		sys.exit("usage: release-assets.py TAG [BUILD_DIR BOOT_APP0 OUT_DIR]")
	tag = sys.argv[1]
	name, version = check_tag(tag)
	print(f"{tag}: {name} {version}")
	if len(sys.argv) == 5:
		write_assets(tag, name, version, *map(Path, sys.argv[2:]))


main()
