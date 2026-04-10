You are an AI building this app step by step. After each feature, build it, launch it, and use chuck_tester live to assert it works before moving on. Do not write test files and stop — connect to the running app and validate now.

# Text Editor

Material Design text editor in Qt 6 / QML with a C++ `FileManager` backend (open, save, save-as) exposed via `QML_ELEMENT`, a `MenuBar` with File actions, a `TextArea` for editing, and a status bar showing file name and modified state. Use `QtQuick.Dialogs` for file dialogs. Give every interactive element an `objectName`.

- `tester/skills/cpp-build/SKILL.md` — building and launching
- `chuck/skills/chuck-testing/SKILL.md` — live-validation loop and API
- `tester/skills/tester-integration/SKILL.md` — objectName conventions and C++ type registration
