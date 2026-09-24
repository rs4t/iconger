<img src="assets/iconger-256.png" width="96" alt="Iconger logo">

# Iconger

Change the icons of the apps pinned to your Windows 10/11 taskbar.

![Iconger's icon editor](assets/screenshot.png)

- Pick from the app's own alternative icons, or your own `.png` / `.jpg` / `.ico` / `.exe` / `.dll` (drag and drop works).
- Images are padded to a square and saved as a multi-size `.ico` (16-256 px) in `%LOCALAPPDATA%\Iconger\icons`, so they stay sharp and keep working after you delete the original.
- Every original icon is backed up before the first change. Restore one app or all of them from the Restore page.
- Finds the "leftover" shortcuts Windows leaves behind when you re-pin an app (`App.lnk` next to `App (2).lnk`). Editing those does nothing, so Iconger shows which one the taskbar actually uses.
- New icons show up on the taskbar right away. If one ever gets stuck, Settings has a one-click Explorer restart (via Restart Manager, so open folder windows come back) that can also clear the icon cache.

## Download

Grab `iconger.exe` from the [latest release](https://github.com/rs4t/iconger/releases/latest). It's a single portable file: no installer, no admin rights. Settings, backups and converted icons are kept in `%LOCALAPPDATA%\Iconger`.

Not supported yet: Store/UWP apps pinned to the taskbar (Settings, Calculator, ...), because they have no shortcut file to edit.

## Build

Requires Visual Studio 2022 (C++ workload) and CMake 3.20+.

```powershell
cmake -B build
cmake --build build --config Release
.\build\Release\iconger.exe
```

Dear ImGui, stb and the Lucide icon font are fetched automatically (pinned versions).

Run the core tests with `ctest --test-dir build -C Release`.

The app icon is generated: edit `assets/make_icon.py` and run `python assets/make_icon.py` (needs Pillow). The in-app logo (`ui::Logo`) draws the same shape, and the palette lives in `src/ui/theme.h`.

## Command line

```
iconger.exe --open "Firefox"                        open the icon editor for a pinned app
iconger.exe --open "Firefox" --icon "C:\iconsox.png"  ...with that icon already previewed (not applied)
iconger.exe --open "Firefox" --icon "firefox.exe,14"  ...or icon #14 of an .exe/.dll
iconger.exe --page restore                          start on the Restore or Settings page
```

## Keyboard

`F5` reload, `Ctrl+F` search, `Ctrl+O` browse for an icon, `Ctrl+S` apply, `Esc` back.

## Versioning

`MAJOR.MINOR.PATCH`, e.g. `0.2.1`:

- **MAJOR**: `0` while in beta, `1` for the first official release. Bumped again only for rare, huge changes.
- **MINOR**: updates such as new features and bigger changes.
- **PATCH**: small updates and fixes.

## License

MIT. Lucide icons are ISC licensed.
