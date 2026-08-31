# Iconger

Change pinned taskbar shortcut icons on Windows 11.

```powershell
cmake -B build && cmake --build build --config Release
.\build\Release\iconger.exe
```

Select a taskbar entry -> pick a new icon (.ico, .png, or .exe/.dll) -> Apply. Optionally restart Explorer to see the change immediately.

Dependencies (Dear ImGui, stb_image) are fetched automatically via CMake FetchContent - no vcpkg or submodules needed.

## License

MIT