# Website

The project's website. Built with [Astro](https://astro.build) and [Tailwind](https://tailwindcss.com) and deployed to Cloudflare Pages at [cheap-rex-display.pages.dev](https://cheap-rex-display.pages.dev/)

```sh
cd docs
bun install
bun run dev       # http://127.0.0.1:4321
bun run build     # into dist/
bun run check     # types, contrast, then a build and its <head>
bun run deploy    # build, then wrangler pages deploy
```

Deployment configuration is in [wrangler.jsonc](wrangler.jsonc).

## Firmware

The install page (`/install/`) flashes the board with [ESP Web Tools](https://esphome.github.io/esp-web-tools/). ESP Web Tools fetches the manifest and the four parts it flashes from the page, and GitHub's release downloads send no CORS headers, so the site serves its own copy from `public/firmware/` (gitignored). Before a deploy, fetch a release built by `.github/workflows/release.yml`:

```sh
python3 scripts/fetch-firmware.py               # the latest release
python3 scripts/fetch-firmware.py v0.2.0-rc.1   # a prerelease, or any other tag
bun run deploy
```

It checks each part against the size and sha256 in the release's `firmware.json`. The merged image stays on the release, and the page links to it for esptool. `src/config/firmware.ts` reads that file at build time for the page's version, links and checksum. Without `public/firmware/`, the page points to building from source instead.

## Layout

- Pages: `/` (home), `/docs/` (contents of the docs pages), `/install/`, `/wifi/` and `/wifi/improv/`.
- `src/config/site.ts`: the name, URL, description and links, written once.
- `src/config/palettes.ts`: the palettes for the switcher, and the `localStorage` key.
- `src/styles/global.css`: the design tokens and the palettes.
- `src/config/site.ts`: also `mediaSrc()` for Cloudinary media and `readme()` for a README section on GitHub.
- `src/layouts/BaseLayout.astro`: `<head>` and the page shell.
- `src/layouts/DocsLayout.astro`: every docs page: the site header, breadcrumbs under Docs, and the footer. Each docs page also gets a line on `/docs/` (`src/pages/docs/index.astro`).
- `src/components/ui/`: reusable generic elements: `Link` (`offsite` adds ↗), `Breadcrumbs`, `Thumbnail` (a screenshot beside a step), `ImageFigure` (a captioned image in the flow), `PreviewCard`, `Highlight`, `Badge`, `Button`.
- `src/components/feature/`: CRD's own elements: `Notes` and `NoteLink` (numbered media endnotes), `BrowserSupport`, `VideoFigure`, `SiteHeader`, `SiteFooter`.
- `public/prototypes/settings-ui.js`: the settings page the board's `/settings` loader fetches, see below.
- A page's media lives beside it in `_media.ts` (the underscore keeps it out of the routes).

## Tokens

Pages and components use Tailwind utilities made from the tokens in `global.css` (`bg-background`, `text-muted-foreground`, `border-border`) and never raw colours; Tailwind's own palette is turned off. The names follow shadcn where one fits, so its components can be added later as islands.

| Token                                                                                      | Use                                                                                    |
| ------------------------------------------------------------------------------------------ | -------------------------------------------------------------------------------------- |
| `background`, `foreground`                                                                 | the page and its text                                                                  |
| `surface`, `surface-raised`                                                                | panels on the page; `card`, `popover`, `muted`, `secondary` and `accent` point at them |
| `border`                                                                                   | lines; `input` points at it                                                            |
| `muted-foreground`, `faint`                                                                | secondary and quiet text                                                               |
| `primary`, `primary-foreground`, `ring`                                                    | mint, the text on it, focus rings                                                      |
| `mint`, `lavender`, `coral`, `amber`, `pink`, `orange`                                     | the accents, inks on paper                                                             |
| `ink`                                                                                      | near-black for fills behind media                                                      |
| `code`, `code-foreground`                                                                  | code blocks, dark on every palette                                                     |
| `state-working`, `-done`, `-permission`, `-question`, `-auth`, `-error`, `-idle`, `-stale` | agent states, following the board's LED                                                |

Type is a 1.25 scale from 16px (`text-xs` 12 to `text-4xl` 49, with `text-sm` 14), spacing goes in 4px steps, and the radii are `rounded-sm` 4, `rounded-md` 8, `rounded-lg` 14 and `rounded-full`.

## Palettes

**Lavender shadow** is the default. **plum**, **graphite**, **indigo** and **paper** (the light one) override the tokens by `data-theme` on `<html>`. The choice is saved in `localStorage` under `crd-palette`, and an inline script at the top of `<head>` applies it before any stylesheet loads, so a saved palette never flashes the default first.

To add one, add its block to `global.css` and its id to `palettes.ts`, then run `python3 scripts/check-contrast.py`.

## Checks

- `python3 scripts/check-contrast.py` checks every palette's contrast: text 16:1, muted 8:1, faint and the accents 4.5:1 on the dark grounds, 4.5:1 for everything on paper. Code blocks are held to the dark floors on every palette.
- `python3 scripts/check-firmware.py` checks the built firmware: the manifest flashes the parts in `firmware.json` at their offsets, each with its size and sha256, and the install page has the button and shows every sha256.
- `python3 scripts/check-head.py` checks the built pages: the palette script comes first in `<head>`, and the title, description, canonical and social tags are there.

## Settings page

The board serves a loader at `/settings` that runs `public/prototypes/settings-ui.js` from this site, pinned by the sha384 in `SETTINGS_UI_SRI` in the firmware. Any change to the file, even formatting, changes the hash. After a deploy, copy the hash of the deployed file into the firmware and reflash, or boards will refuse the script.

## Social image

The Open Graph and Twitter tags point at `public/social.png` (1280×640).
