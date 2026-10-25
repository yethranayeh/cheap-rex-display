import { existsSync, readFileSync } from "node:fs";
import { resolve } from "node:path";

export interface FirmwareFile {
  file: string;
  size: number;
  sha256: string;
}

// Written by scripts/fetch-firmware.py from a release's firmware.json
export interface Firmware {
  name: string;
  version: string;
  tag: string;
  /** What the install page flashes, served from `public/firmware/` */
  parts: (FirmwareFile & { offset: number })[];
  /** The whole flash as one image, for esptool, only on the release */
  merged: FirmwareFile;
  repo: string;
  commit: string;
  run: string;
}

// The build runs from docs/, and public/ is served at the site's root
const path = resolve("public/firmware/firmware.json");

// Undefined when no release has been fetched: the install page then points to building from source
export const firmware: Firmware | undefined = existsSync(path)
  ? JSON.parse(readFileSync(path, "utf8"))
  : undefined;

export const manifestPath = "/firmware/manifest.json";
