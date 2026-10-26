// Settings UI for firmware 0.3.0: compact rows, the header counts and the
//  calm screen.
// The board's own page, http://crd.local/settings, loads it into its #crd
//  element, so it calls the board's /api/settings from the same origin.
// The firmware pins this file by its hash. Once 0.3.0 is released it must not
//  change: the next release gets its own file.

const root = document.getElementById("crd");

const SETTINGS = [
	["rows_compact", "Compact rows"],
	["header_counts", "Counts in the header over the list"],
	["calm_screen", "Calm screen while nothing waits"]
];

async function call(init) {
	const response = await fetch("/api/settings", init);
	if (!response.ok) {
		throw new Error(`${response.status} ${await response.text()}`);
	}

	return response.json();
}

function render(settings) {
	const boxes = [];

	const labels = SETTINGS.filter(([key]) => key in settings).map(([key, text]) => {
		const label = document.createElement("label");
		const box = document.createElement("input");

		box.type = "checkbox";
		box.checked = settings[key];
		box.addEventListener("change", async () => {
			boxes.forEach((b) => (b.disabled = true));
			try {
				const body = new URLSearchParams({ [key]: String(box.checked) });
				render(await call({ method: "POST", body }));
			} catch (error) {
				root.replaceChildren(`Could not save: ${error.message}`);
			}
		});
		boxes.push(box);
		label.append(box, ` ${text}`);
		label.style.display = "block";
		return label;
	});
	root.replaceChildren(...labels);
}

root.replaceChildren("Asking the board...");
call()
	.then(render)
	.catch((error) => root.replaceChildren(`Could not read the settings: ${error.message}`));
