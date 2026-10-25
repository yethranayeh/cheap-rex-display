// Settings UI prototype: reads and writes one setting, compact rows.
// The board's own page, http://crd.local/settings, loads it into its #crd
//  element, so it calls the board's /api/settings from the same origin.
// The firmware pins this file by its hash, so it must not change:
//  a new UI goes in a new file.

const root = document.getElementById("crd");

async function call(init) {
	const response = await fetch("/api/settings", init);
	if (!response.ok) {
		throw new Error(`${response.status} ${await response.text()}`);
	}

	return response.json();
}

function render(settings) {
	const label = document.createElement("label");
	const box = document.createElement("input");

	box.type = "checkbox";
	box.checked = settings.rows_compact;
	box.addEventListener("change", async () => {
		box.disabled = true;
		try {
			const body = new URLSearchParams({ rows_compact: String(box.checked) });
			render(await call({ method: "POST", body }));
		} catch (error) {
			root.replaceChildren(`Could not save: ${error.message}`);
		}
	});
	label.append(box, " Compact rows");
	root.replaceChildren(label);
}

root.replaceChildren("Asking the board...");
call()
	.then(render)
	.catch((error) => root.replaceChildren(`Could not read the settings: ${error.message}`));
