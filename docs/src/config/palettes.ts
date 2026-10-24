// Each id has a [data-theme] block in src/styles/global.css
export const palettes = [
  { id: "lavender-shadow", name: "Lavender shadow" },
  { id: "plum", name: "Plum" },
  { id: "graphite", name: "Graphite" },
  { id: "indigo", name: "Indigo" },
  { id: "paper", name: "Paper" },
] as const;

export type PaletteId = (typeof palettes)[number]["id"];

export const defaultPalette: PaletteId = "lavender-shadow";

// The localStorage key that holds the choice
export const paletteKey = "crd-palette";
