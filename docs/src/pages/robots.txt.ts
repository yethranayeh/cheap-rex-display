import type { APIRoute } from "astro";

export const GET: APIRoute = ({ site }) => {
  const sitemap = new URL("sitemap-index.xml", site);
  return new Response(`User-agent: *\nDisallow: /prototypes/\n\nSitemap: ${sitemap}\n`);
};
