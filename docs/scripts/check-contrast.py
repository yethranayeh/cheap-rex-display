#!/usr/bin/env python3
# Usage: python3 scripts/check-contrast.py [path/to/global.css]
#
# Checks the WCAG contrast of every palette in src/styles/global.css and exits
# non-zero if one falls short. On each palette's background: text 16:1 and
# muted 8:1 on the dark ones, faint and every accent and state 4.5:1 on all,
# and 4.5:1 for everything on the light one. The same inside code blocks,
# which stay dark, and 4.5:1 for primary-foreground on primary. Faint and the
# accents are listed when they fall short on a surface, but not enforced.

import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
CSS = HERE.parent / "src" / "styles" / "global.css"
PALETTES_TS = HERE.parent / "src" / "config" / "palettes.ts"

ACCENTS = ["mint", "lavender", "coral", "amber", "pink", "orange", "primary"]
STATES = ["working", "done", "permission", "question", "auth", "error", "idle", "stale"]


def floors(dark):
	"""Token and minimum ratio against the ground."""
	rows = [("foreground", 16 if dark else 4.5), ("muted-foreground", 8 if dark else 4.5), ("faint", 4.5)]
	rows += [(a, 4.5) for a in ACCENTS]
	rows += [("state-" + s, 4.5) for s in STATES]
	return rows


def strip_comments(css):
	return re.sub(r"/\*.*?\*/", "", css, flags=re.S)


def declarations(body):
	return dict(re.findall(r"(--[\w-]+)\s*:\s*([^;]+);", body)) | dict(
		re.findall(r"(color-scheme)\s*:\s*([^;]+);", body)
	)


def block(css, opener):
	"""The body of a block without nested braces, like @theme { ... }."""
	m = re.search(re.escape(opener) + r"\s*\{([^{}]*)\}", css)
	if not m:
		sys.exit(f"no '{opener}' block in {CSS}")
	return m.group(1)


def layer_rules(css):
	"""Selector -> declarations for the rules inside @layer base."""
	start = css.index("@layer base")
	depth = 0
	for i in range(css.index("{", start), len(css)):
		depth += {"{": 1, "}": -1}.get(css[i], 0)
		if depth == 0:
			break
	inner = css[css.index("{", start) + 1 : i]
	rules = {}
	for selectors, body in re.findall(r"([^{}]+)\{([^{}]*)\}", inner):
		for sel in selectors.split(","):
			rules[sel.strip()] = rules.get(sel.strip(), {}) | declarations(body)
	return rules


def resolve(tokens, name, seen=()):
	value = tokens.get("--color-" + name)
	if value is None:
		sys.exit(f"--color-{name} is not defined")
	m = re.fullmatch(r"var\(--color-([\w-]+)\)", value.strip())
	if m:
		if m.group(1) in seen:
			sys.exit(f"--color-{name} refers to itself")
		return resolve(tokens, m.group(1), seen + (name,))
	return value.strip()


def luminance(hex_colour):
	h = hex_colour.lstrip("#")
	if len(h) == 3:
		h = "".join(c * 2 for c in h)
	if not re.fullmatch(r"[0-9a-fA-F]{6}", h):
		sys.exit(f"not a hex colour: {hex_colour}")
	channels = []
	for i in (0, 2, 4):
		c = int(h[i : i + 2], 16) / 255
		channels.append(c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4)
	r, g, b = channels
	return 0.2126 * r + 0.7152 * g + 0.0722 * b


def ratio(a, b):
	hi, lo = sorted([luminance(a), luminance(b)], reverse=True)
	return (hi + 0.05) / (lo + 0.05)


def check(label, tokens, dark, ground, name, failures, notes):
	"""The ratio of one token in one scope, and its floor; records shortfalls."""
	if name == "primary-foreground on primary":
		fg, bg, floor = "primary-foreground", "primary", 4.5
	elif name == "code-foreground on code":
		fg, bg, floor = "code-foreground", "code", 16 if dark else 4.5
	else:
		fg, bg, floor = name, ground, dict(floors(dark))[name]
	r = ratio(resolve(tokens, fg), resolve(tokens, bg))
	if r < floor:
		failures.append(f"{label}: {fg} is {r:.2f}:1 on {bg}, needs {floor}")
	# Text on a surface is checked when there is some; the states repeat the accents
	if bg == "background" and floor == 4.5 and not fg.startswith("state-"):
		for surface in ("surface", "surface-raised"):
			rs = ratio(resolve(tokens, fg), resolve(tokens, surface))
			if rs < floor:
				notes.append(f"{label}: {fg} is {rs:.2f}:1 on {surface}")
	return r, floor


def main():
	path = Path(sys.argv[1]) if len(sys.argv) > 1 else CSS
	css = strip_comments(path.read_text())
	defaults = declarations(block(css, "@theme")) | declarations(block(css, "@theme inline"))
	rules = layer_rules(css)
	ids = re.findall(r'\[data-theme="([\w-]+)"\]$', "\n".join(rules), flags=re.M)
	failures = []

	known = re.findall(r'id: "([\w-]+)"', PALETTES_TS.read_text())
	if sorted(known) != sorted(ids):
		failures.append(f"palettes.ts has {known}, global.css has {ids}")

	# The lavender shadow block has to repeat the @theme defaults exactly
	for name, value in rules.get('[data-theme="lavender-shadow"]', {}).items():
		if name.startswith("--") and defaults.get(name) != value:
			failures.append(f"lavender-shadow {name} is {value}, @theme has {defaults.get(name)}")

	scopes = []
	for pid in ids:
		tokens = defaults | rules[f'[data-theme="{pid}"]']
		dark = tokens.get("color-scheme", "dark").strip() == "dark"
		scopes.append((pid, tokens, dark, "background"))
		code = tokens | rules.get(f'[data-theme="{pid}"] pre', {})
		scopes.append((pid + " code", code, True, "code"))

	names = [n for n, _ in floors(True)] + ["primary-foreground on primary", "code-foreground on code"]
	notes = []
	for ground in ("background", "code"):
		group = [s for s in scopes if s[3] == ground]
		print(f"\nOn {ground}" + " " * (29 - len(ground)) + "".join(f"{s[0].split()[0]:>16}" for s in group))
		for name in names:
			if ground == "background" and name.startswith("code-"):
				continue
			cells = ""
			for label, tokens, dark, _ in group:
				r, floor = check(label, tokens, dark, ground, name, failures, notes)
				cells += f"{r:>15.2f}{'!' if r < floor else ' '}"
			print(f"{name:32}{cells}")

	if notes:
		print("\nBelow the floor on a surface (fyi):")
		for note in notes:
			print("  " + note)
	if failures:
		print("\nFailed:")
		for failure in failures:
			print("  " + failure)
		sys.exit(1)
	print(f"\nAll {len(ids)} palettes pass.")


main()
