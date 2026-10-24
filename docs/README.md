# Website

The project's website. Built with [Astro](https://astro.build) and deployed to Cloudflare Pages at cheap-rex-display.pages.dev.

```sh
cd docs
bun install
bun run dev       # http://127.0.0.1:4321
bun run build     # into dist/
bun run deploy    # build, then wrangler pages deploy
```

Deployment configuration is in [wrangler.jsonc](wrangler.jsonc).
