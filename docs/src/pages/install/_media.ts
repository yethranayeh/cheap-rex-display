// The leading underscore keeps Astro's router from treating this as an endpoint.
import type { Note } from "../../components/feature/notes";
import { mediaSrc } from "../../config/site";

export const sameVersionNote: Note = {
  n: 1,
  ref: "running-1",
  id: "note-1",
  title: "Same version",
  image: {
    src: mediaSrc("v1792937091/same-version_gf304p.png"),
    alt: "The ESP Web Tools dialog on a board already running cheap-rex-display 0.2.0, with Connect to Wi-Fi, Logs & Console and Erase User Data.",
    width: 682,
    height: 746,
  },
  preview: "Wi-Fi, the logs, or erase. Nothing is written until you pick one.",
  caption:
    "The installer found the board already running this version. It offers Wi-Fi setup, the logs, or erasing the user data; it writes nothing until you pick one.",
  label: "Note 1: the menu on a board already running this version",
};

export const installNotes: Note[] = [sameVersionNote];

/** The installing state, shown beside the step that asks you to wait. */
export const installThumbnail = {
  src: mediaSrc("v1792937092/web-serial-installing_ifhok1.png"),
  alt: "The ESP Web Tools installer writing the firmware at 49%, with an estimate of two minutes and a note to keep the page visible.",
  width: 649,
  height: 396,
  title: "Installing",
  caption:
    "While it writes, the installer shows its progress and an estimate. Keep this page visible; a background tab can slow the write down.",
  label: "Preview the installer while it writes",
};
