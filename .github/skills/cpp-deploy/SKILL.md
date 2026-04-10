---
name: cpp-deploy
description: 'Deploy Qt runtime dependencies so built apps can launch. Use when: app crashes on startup with missing DLL/so/dylib errors, first run after a build, running windeployqt/macdeployqt, setting up LD_LIBRARY_PATH on Linux.'
---

# Deploying Qt Dependencies

After building, Qt apps need their runtime libraries (DLLs, shared objects, QML plugins) copied next to the executable or made discoverable via path. Without this step the app will crash on startup with missing library errors.

## Windows — windeployqt

`windeployqt` scans the executable and copies all required Qt DLLs, plugins, and QML imports into the executable's directory.

```powershell
# From the repo root — deploy a single example
C:/Qt/6.11.0/msvc2022_64/bin/windeployqt.exe `
    build/MSVC_64bit_Debug/examples/text_editor/text_editor.exe `
    --qmldir examples/text_editor
```

The `--qmldir` flag tells windeployqt where to scan for QML imports so it copies the right QML plugins.

### General pattern

```powershell
<QT_PREFIX>/bin/windeployqt.exe <path-to-exe> --qmldir <qml-source-dir>
```

Where `<QT_PREFIX>` matches `CMAKE_PREFIX_PATH` from the build preset (default: `C:/Qt/6.11.0/msvc2022_64`).

### VS Developer Shell alternative

If `windeployqt` isn't on PATH, either use the full path above or activate the VS Developer Shell first:

```powershell
& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -SkipAutomaticLocation
windeployqt build/MSVC_64bit_Debug/examples/text_editor/text_editor.exe --qmldir examples/text_editor
```

## macOS — macdeployqt

```bash
$(QT_PREFIX)/bin/macdeployqt build/clang_64_Debug/examples/text_editor/text_editor.app -qmldir=examples/text_editor
```

Default `QT_PREFIX`: `build/Qt/6.11.0/clang_64` (or wherever Qt is installed).

## Linux — LD_LIBRARY_PATH

Linux builds typically don't bundle libraries. Instead, point `LD_LIBRARY_PATH` at the Qt libs:

```bash
export LD_LIBRARY_PATH=$(QT_PREFIX)/lib:$LD_LIBRARY_PATH
./build/gcc_64_Debug/examples/text_editor/text_editor
```

Default `QT_PREFIX`: `build/Qt/6.11.0/gcc_64`.

For distribution, use `linuxdeployqt` or CMake's `qt_generate_deploy_app_script()`.

## When to Deploy

- **First build** — always run deploy before launching.
- **After adding new Qt modules** — e.g. adding `QtCharts` means new DLLs are needed.
- **After Qt upgrade** — DLLs change between versions.
- **Not needed** when only C++/QML source changed — existing deployed DLLs still work.

## Quick Reference

| Platform | Tool | Qt prefix (from presets) |
|----------|------|------------------------|
| Windows | `windeployqt.exe` | `C:/Qt/6.11.0/msvc2022_64` |
| macOS | `macdeployqt` | `build/Qt/6.11.0/clang_64` |
| Linux | `LD_LIBRARY_PATH` or `linuxdeployqt` | `build/Qt/6.11.0/gcc_64` |
