#include "SourceLocation.h"

// The source location of a QML object is only kept in private QML data, the
// same place Qt Creator's QML inspector reads it from. This is the only file
// that touches private Qt API; without Qt6::QmlPrivate it compiles to stubs.
#ifdef TESTER_HAS_QML_PRIVATE
#include <private/qqmlcontextdata_p.h>
#include <private/qqmldata_p.h>
#endif

namespace tester {

auto sourceLocation(const QObject* object) -> QString
{
#ifdef TESTER_HAS_QML_PRIVATE
    const auto* data = QQmlData::get(object);
    if (!data || !data->outerContext || data->lineNumber == 0)
        return {};
    return QStringLiteral("%1:%2:%3").arg(data->outerContext->urlString()).arg(data->lineNumber).arg(data->columnNumber);
#else
    Q_UNUSED(object)
    return {};
#endif
}

auto hasSourceLocations() -> bool
{
#ifdef TESTER_HAS_QML_PRIVATE
    return true;
#else
    return false;
#endif
}

} // namespace tester
