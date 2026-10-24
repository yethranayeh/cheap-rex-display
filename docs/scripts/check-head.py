#!/usr/bin/env python3
# Usage: python3 scripts/check-head.py [dist]
#
# Checks the <head> of every built page, so run it after `bun run build`.
# The palette script has to be the first thing in <head> that isn't a <meta>
# or <title>: the browser runs an inline script as soon as it parses it, so
# data-theme is set before any stylesheet is even requested and the saved
# palette is there for the first paint. Also checks the title, description,
# canonical link and the Open Graph and Twitter tags.

import sys
from html.parser import HTMLParser
from pathlib import Path

HERE = Path(__file__).resolve().parent
DIST = HERE.parent / "dist"
REQUIRED_META = [
	("name", "description"),
	("property", "og:title"),
	("property", "og:description"),
	("property", "og:image"),
	("name", "twitter:card"),
	("name", "twitter:image"),
]


class Head(HTMLParser):
	def __init__(self):
		super().__init__()
		self.in_head = False
		self.html = {}
		self.order = []  # (tag, attrs) in <head>, in document order
		self.scripts = []  # inline script bodies in <head>
		self.title = ""
		self._tag = None

	def handle_starttag(self, tag, attrs):
		attrs = dict(attrs)
		if tag == "html":
			self.html = attrs
		elif tag == "head":
			self.in_head = True
		elif self.in_head:
			self.order.append((tag, attrs))
			self._tag = tag

	def handle_endtag(self, tag):
		if tag == "head":
			self.in_head = False
		self._tag = None

	def handle_data(self, data):
		if self._tag == "script":
			self.scripts.append(data)
		elif self._tag == "title":
			self.title += data


def check(path):
	head = Head()
	head.feed(path.read_text())
	problems = []

	if not head.html.get("data-theme"):
		problems.append("<html> has no data-theme, so pages without JavaScript get no palette")

	first = next(((t, a) for t, a in head.order if t not in ("meta", "title")), None)
	if not first or first[0] != "script" or "src" in first[1]:
		problems.append(f"the first thing in <head> after <meta> and <title> is {first}, not the inline palette script")
	elif not head.scripts or "crd-palette" not in head.scripts[0] or "dataset.theme" not in head.scripts[0]:
		problems.append("the first inline script in <head> doesn't read crd-palette into data-theme")

	if not head.title.strip():
		problems.append("no <title>")
	metas = {(k, a.get(k)) for t, a in head.order if t == "meta" for k in ("name", "property")}
	for key in REQUIRED_META:
		if key not in metas:
			problems.append(f"no <meta {key[0]}='{key[1]}'>")
	noindex = ("name", "robots") in metas
	canonical = any(t == "link" and a.get("rel") == "canonical" for t, a in head.order)
	if not noindex and not canonical:
		problems.append("neither a canonical link nor noindex")
	return problems


def main():
	dist = Path(sys.argv[1]) if len(sys.argv) > 1 else DIST
	pages = sorted(dist.rglob("*.html"))
	if not pages:
		sys.exit(f"no pages in {dist}; run `bun run build` first")
	failed = False
	for page in pages:
		problems = check(page)
		print(f"{'FAIL' if problems else 'ok  '} {page.relative_to(dist)}")
		for problem in problems:
			print(f"     {problem}")
		failed = failed or bool(problems)
	sys.exit(1 if failed else 0)


main()
