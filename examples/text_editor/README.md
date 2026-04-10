# Text Editor

Barebone Qt 6 / QML skeleton with tester wired in. Build a Material Design text editor with a C++ `FileManager` backend class (open, save, save-as) exposed to QML via `QML_ELEMENT`, a `MenuBar` with File actions, a `TextArea` for editing, and a status bar showing the file name and modified state. Use `QtQuick.Dialogs` for file dialogs. Give every interactive element an `objectName` so chuck can find it.

This example uses [chuck_tester](https://github.com/kelteseth/chuck_tester) for AI-driven UI testing. Run `uv sync` in this folder to install the chuck dependency from the parent repo, then use `uv run pytest` for tests.

See `.github/skills/cpp-build/SKILL.md` for building, `.github/skills/chuck-testing/SKILL.md` for writing tests, and `.github/skills/tester-integration/SKILL.md` for objectName conventions and how to register C++ types.
