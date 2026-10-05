# Iconger website

A single static page (`index.html`, `styles.css`, `app.js`), no build step. Published to
GitHub Pages by `.github/workflows/pages.yml` whenever `site/` changes on master.

- **Changelog:** written into `index.html` as plain HTML by `scripts/render_site.py` (between the
  `releases:start` / `releases:end` markers), so search engines and visitors without JavaScript see
  it. The Pages workflow runs the script before every deploy and after every release, so it
  stays current. Run `python scripts/render_site.py` from the repo root to refresh it locally.
- **Live data:** download count and stars are fetched from the GitHub API in the visitor's
  browser (cached 15 minutes in `localStorage`). If the API can't be reached, those two tiles
  are simply left out.
- **SEO:** `<title>`, description, canonical, Open Graph / Twitter tags and the
  `SoftwareApplication` JSON-LD are in the `<head>` of `index.html` (the JSON-LD
  `softwareVersion` is kept current by the script). `sitemap.xml` (its `lastmod` is also set by
  the script), `robots.txt` and `CNAME` (`iconger.egorz.com`) sit next to it. The link preview
  image is `img/og-image.jpg` (1200x630). Adding a page means adding it to `sitemap.xml`.
- **Icons:** [Iconsax](https://iconsax.io) free icons, bulk style, from the official
  `iconsax` npm package, as an SVG sprite in `icons.svg` (use `<svg><use href="icons.svg#i-NAME"/></svg>`).
- **Font:** [Geist](https://vercel.com/font) (SIL Open Font License, `fonts/OFL.txt`), self-hosted.
- **Library icons** in `img/libraries/` come from each icon library (licenses listed on the page).
- **Palette:** same tokens as the app (`src/ui/theme.h`), dark only.

## Preview locally

```
cd site
python -m http.server 8000
```

Then open http://localhost:8000 (opening `index.html` as a file breaks the icon sprite).

## Screenshots

`img/shots/` holds the app screenshots as WebP, 1920x1270, cropped to the window.

| File | Shows | Used in |
|---|---|---|
| `editor.webp` | Icon editor with a new icon previewed | Hero, "Icon editor" tab, link previews |
| `home.webp` | Pinned apps page | "Pinned apps" tab |
| `restore.webp` | Restore page | "Restore" tab |
| `settings.webp` | Settings page | "Settings" tab |

The tabs advance by themselves while the section is in view (hover pauses, a click stops it).
