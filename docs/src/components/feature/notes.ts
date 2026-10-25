export interface NoteImage {
  src: string;
  alt: string;
  width: number;
  height: number;
}

export interface Note {
  n: number;
  /** id on the source element, for the note's back link */
  ref: string;
  /** id on the endnote, for the marker's link */
  id: string;
  title: string;
  image: NoteImage;
  /** Short copy inside the hover preview */
  preview: string;
  /** Fuller copy in the endnote, so the jump is worth it */
  caption: string;
  /** Accessible name for the marker */
  label: string;
}
