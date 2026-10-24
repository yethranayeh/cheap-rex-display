// @ts-check
import { defineConfig, fontProviders } from "astro/config";
import sitemap from "@astrojs/sitemap";
import tailwindcss from "@tailwindcss/vite";
import { site } from "./src/config/site.ts";

export default defineConfig({
	site: site.url,
	output: "static",
	devToolbar: { enabled: false },
	integrations: [sitemap()],
	vite: {
		plugins: [tailwindcss()]
	},
	fonts: [
		{
			provider: fontProviders.google(),
			name: "Recursive",
			cssVariable: "--font-recursive",
			weights: ["300 1000"],
			styles: ["normal"],
			subsets: ["latin"],
			// Without these Google serves only the wght axis, and the casual and mono styles are gone
			options: {
				experimental: {
					variableAxis: { CASL: [["0", "1"]], MONO: [["0", "1"]] }
				}
			},
			fallbacks: ["ui-sans-serif", "system-ui", "sans-serif"]
		}
	]
});
