#include "tester/QmlDebug.h"
#include <QDebug>
#include <QQmlDebuggingEnabler>
#include <QString>
#include <QStringList>
#include <optional>

namespace tester {

namespace {

    // Qt accepts both "-qmljsdebugger=<value>" and "-qmljsdebugger <value>",
    // with one or two leading dashes.
    auto qmlDebuggerValue(int argc, char* argv[]) -> std::optional<QString>
    {
        for (int i = 1; i < argc; ++i) {
            auto arg = QString::fromLocal8Bit(argv[i]);
            if (arg.startsWith(QStringLiteral("--")))
                arg.remove(0, 1);
            if (arg.startsWith(QStringLiteral("-qmljsdebugger=")))
                return arg.mid(qsizetype(sizeof("-qmljsdebugger=") - 1));
            if (arg == QStringLiteral("-qmljsdebugger") && i + 1 < argc)
                return QString::fromLocal8Bit(argv[i + 1]);
        }
        return std::nullopt;
    }

    auto isLoopback(const QString& host) -> bool
    {
        return host.compare(QStringLiteral("localhost"), Qt::CaseInsensitive) == 0
            || host.startsWith(QStringLiteral("127."))
            || host == QStringLiteral("::1");
    }

} // namespace

auto enableQmlDebuggingFromArguments(int argc, char* argv[]) -> bool
{
    const auto value = qmlDebuggerValue(argc, argv);
    if (!value)
        return false;

    bool tcp = false;
    QString host;
    for (const auto& part : value->split(QLatin1Char(','))) {
        if (part.startsWith(QStringLiteral("port:")))
            tcp = true;
        else if (part.startsWith(QStringLiteral("host:")))
            host = part.mid(5);
    }

    if (tcp && !isLoopback(host)) {
        qWarning() << "[tester] Not enabling QML debugging: -qmljsdebugger needs host:127.0.0.1"
                   << "(without it Qt listens on every network interface)";
        return false;
    }

    QQmlDebuggingEnabler::enableDebugging(true);
    qInfo() << "[tester] QML debugging enabled:" << *value;
    return true;
}

} // namespace tester
