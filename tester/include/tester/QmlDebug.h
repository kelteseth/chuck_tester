#pragma once

namespace tester {

// Enables QML debugging and profiling when argv carries Qt's own
// -qmljsdebugger=... argument, so qmlprofiler, Qt Creator or chuck can attach.
// Without that argument nothing changes: no port opens and no warning prints.
//
// Call it first thing in main(), before the QGuiApplication and before any
// QQmlEngine exists. Qt reads the argument when the first engine starts and
// only engines created after that are visible to the debugger.
//
// A TCP connector must be bound to a loopback host (host:127.0.0.1, localhost
// or ::1), because Qt otherwise listens on every interface. Local sockets
// (file:) and the native connector are always accepted.
//
// Returns true if debugging was enabled.
auto enableQmlDebuggingFromArguments(int argc, char* argv[]) -> bool;

} // namespace tester
