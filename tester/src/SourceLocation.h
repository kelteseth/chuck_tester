#pragma once

#include <QString>

class QObject;

namespace tester {

// Where the object is declared in QML as "<url>:<line>:<column>", e.g.
// "qrc:/qt/qml/MyApp/Main.qml:42:9". For an instance of a custom component
// this is the line that instantiates it, not the component's own file.
// Empty for objects created in C++, or when tester was built without
// Qt6::QmlPrivate.
auto sourceLocation(const QObject* object) -> QString;

// Whether sourceLocation() can return anything in this build.
auto hasSourceLocations() -> bool;

} // namespace tester
