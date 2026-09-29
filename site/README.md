# Iconger website

A single static page (`index.html`, `styles.css`, `app.js`), no build step. Published to
GitHub Pages by `.github/workflows/pages.yml` whenever `site/` changes on master.

- **Live data:** versions, changelog, download count and stars are fetched from the GitHub
  API in the visitor's browser (cached 15 minutes in `localStorage`). New releases show up
  without touching the site. If the API can't be reached, those parts fall back to links.
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
