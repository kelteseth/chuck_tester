# Text Editor

Barebone Qt 6 / QML skeleton with tester wired in. Build a Material Design text editor with a C++ `FileManager` backend class (open, save, save-as) exposed to QML via `QML_ELEMENT`, a `MenuBar` with File actions, a `TextArea` for editing, and a status bar showing the file name and modified state. Use `QtQuick.Dialogs` for file dialogs. Give every interactive element an `objectName` so chuck can find it.

See `.github/skills/tester-integration/SKILL.md` for objectName conventions and how to register C++ types.
