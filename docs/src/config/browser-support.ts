export type SupportLevel = "yes" | "no";

export interface BrowserRow {
  /** Matches the user agent to mark the visitor's browser */
  id: string;
  name: string;
  /** The first version that has it. Omitted when no version does */
  version?: string;
  support: SupportLevel;
}

export interface FeatureSupport {
  feature: string;
  /** The navigator property the feature lives on */
  api?: string;
  /** Completes "This browser can ___." */
  action: string;
  /** One sentence on how it works */
  description: string;
  caniuse: string;
  asOf: string;
  rows: BrowserRow[];
  footnote: string;
  /** The way out when the browser is incapable (thanks Apple) */
  fallback: { label: string; href: string };
}

export function supportedBrowserList(support: FeatureSupport): string {
  const names = support.rows.filter((row) => row.support === "yes").map((row) => row.name);

  if (names.length <= 1) {
    return names[0] ?? "";
  }

  return `${names.slice(0, -1).join(", ")} or ${names[names.length - 1]}`;
}

// caniuse.com/web-serial and MDN's browser-compat-data, October 2026
export const webSerial: FeatureSupport = {
  feature: "Web Serial",
  api: "serial",
  action: "flash the board",
  description:
    "The installer talks to the board over USB through Web Serial, an API built into the browser.",
  caniuse: "https://caniuse.com/web-serial",
  asOf: "October 2026",
  rows: [
    { id: "chrome", name: "Chrome", version: "89+", support: "yes" },
    { id: "edge", name: "Edge", version: "89+", support: "yes" },
    { id: "firefox", name: "Firefox", version: "151+", support: "yes" },
    { id: "opera", name: "Opera", version: "76+", support: "yes" },
    { id: "safari", name: "Safari", support: "no" },
  ],
  footnote:
    "Chrome on Android 148+ can too, with a USB adapter. iPhones and iPads can't, of course.",
  fallback: { label: "flash the image yourself", href: "/install/#going-back" },
};
