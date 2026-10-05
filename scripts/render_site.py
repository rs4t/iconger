#!/usr/bin/env python3
"""Fills the changelog of site/index.html with the real release notes, as plain HTML.

Search engines and visitors without JavaScript get the changelog from the HTML itself;
site/app.js only adds the open/close behaviour on top. Also keeps `softwareVersion` in
the JSON-LD block and `lastmod` in sitemap.xml current.

Run from the repository root:  python scripts/render_site.py
The pages workflow runs it before every deploy (and after every release).
Uses GITHUB_TOKEN if set (higher API rate limit).
"""
import datetime
import html
import json
import os
import re
import sys
import urllib.request

REPO = "rs4t/iconger"
SITE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "site")
SHOWN = 5  # newest releases shown; older ones are in the HTML but behind a button
MARKERS = ("<!-- releases:start -->", "<!-- releases:end -->")


def fetch_releases():
    req = urllib.request.Request(
        f"https://api.github.com/repos/{REPO}/releases?per_page=100",
        headers={"Accept": "application/vnd.github+json", "User-Agent": "iconger-site-build"},
    )
    token = os.environ.get("GITHUB_TOKEN")
    if token:
        req.add_header("Authorization", f"Bearer {token}")
    with urllib.request.urlopen(req, timeout=30) as r:
        return [x for x in json.load(r) if not x.get("draft")]


def inline(text):
    """The small Markdown subset the release notes use: `code`, **bold**, [links](https://...)."""
    t = html.escape(text, quote=False)
    t = re.sub(r"`([^`]+)`", r"<code>\1</code>", t)
    t = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", t)
    t = re.sub(r"\[([^\]]+)\]\((https?://[^)\s]+)\)", r'<a href="\2">\1</a>', t)
    return t


def note_lines(body):
    # that line is for the GitHub release page, where the .exe is attached
    return [l for l in body.replace("\r", "").split("\n")
            if not re.search(r"Download `?iconger\.exe`? below", l, re.I)]


def render_notes(body):
    out, para, in_list = [], [], False

    def flush():
        nonlocal para
        if para:
            out.append("<p>" + inline(" ".join(para)) + "</p>")
            para = []

    def close_list():
        nonlocal in_list
        if in_list:
            out.append("</ul>")
            in_list = False

    for raw in note_lines(body):
        line = raw.strip()
        m = None
        if not line:
            flush()
            close_list()
        elif (m := re.match(r"^#{1,6}\s+(.*)$", line)):
            flush()
            close_list()
            out.append("<h3>" + inline(m.group(1)) + "</h3>")
        elif (m := re.match(r"^[-*]\s+(.*)$", line)):
            flush()
            if not in_list:
                out.append("<ul>")
                in_list = True
            out.append("<li>" + inline(m.group(1)) + "</li>")
        else:
            close_list()
            para.append(line)
    flush()
    close_list()
    return "\n".join(out)


def summary_of(body):
    for l in note_lines(body):
        l = l.strip()
        if l and not l.startswith("#"):
            l = re.sub(r"^[-*]\s+", "", l)
            l = l.replace("**", "").replace("`", "")
            return re.sub(r"\[([^\]]+)\]\([^)]*\)", r"\1", l)
    return ""


def fmt_date(iso):
    d = datetime.datetime.strptime(iso, "%Y-%m-%dT%H:%M:%SZ")
    return d.strftime("%b ") + str(d.day) + d.strftime(", %Y")


def render_releases(releases):
    latest = next((r for r in releases if not r.get("prerelease")), None)
    rows = []
    for i, r in enumerate(releases):
        classes = "release" + (" reveal" if i > 0 else "")
        attrs = ' open' if i == 0 else ""
        if i >= SHOWN:
            attrs += " hidden"
        style = f' style="--i:{min(i, 6)}"' if 0 < i < SHOWN else ""
        tag = html.escape(r["tag_name"])
        date = ""
        if r.get("published_at"):
            date = (f'<time class="release-date" datetime="{html.escape(r["published_at"])}">'
                    f'{fmt_date(r["published_at"])}</time>')
        badge = '<span class="release-latest">Latest</span>' if latest and r["tag_name"] == latest["tag_name"] else ""
        body = r.get("body") or ""
        rows.append(
            f'        <details class="{classes}"{style}{attrs}>\n'
            f'          <summary><span class="release-version" translate="no">{tag}</span>{date}{badge}'
            f'<span class="release-summary">{inline(summary_of(body))}</span></summary>\n'
            f'          <div class="release-body">\n{render_notes(body)}\n          </div>\n'
            f'        </details>'
        )
    return "\n".join(rows)


def main():
    releases = fetch_releases()
    if not releases:
        sys.exit("no releases found")
    path = os.path.join(SITE, "index.html")
    page = open(path, encoding="utf-8").read()
    start = page.index(MARKERS[0]) + len(MARKERS[0])
    end = page.index(MARKERS[1])
    page = page[:start] + "\n" + render_releases(releases) + "\n" + page[end:]
    latest = next((r for r in releases if not r.get("prerelease")), releases[0])
    page = re.sub(r'("softwareVersion":\s*")[^"]*(")', lambda m: m.group(1) + latest["tag_name"].lstrip("v") + m.group(2), page)
    open(path, "w", encoding="utf-8").write(page)

    sm = os.path.join(SITE, "sitemap.xml")
    xml = open(sm, encoding="utf-8").read()
    today = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%d")
    open(sm, "w", encoding="utf-8").write(re.sub(r"<lastmod>[^<]*</lastmod>", f"<lastmod>{today}</lastmod>", xml))
    print(f"rendered {len(releases)} releases, latest {latest['tag_name']}")


if __name__ == "__main__":
    main()
