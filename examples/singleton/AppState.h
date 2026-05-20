#pragma once

#include <QObject>
#include <QString>
#include <QtQml/qqmlregistration.h>

class AppState : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(int counter READ counter WRITE setCounter NOTIFY counterChanged)
    Q_PROPERTY(QString message READ message WRITE setMessage NOTIFY messageChanged)

public:
    using QObject::QObject;

    int counter() const { return m_counter; }
    void setCounter(int v)
    {
        if (m_counter == v)
            return;
        m_counter = v;
        emit counterChanged();
    }

    QString message() const { return m_message; }
    void setMessage(const QString& v)
    {
        if (m_message == v)
            return;
        m_message = v;
        emit messageChanged();
    }

    Q_INVOKABLE void increment() { setCounter(m_counter + 1); }

signals:
    void counterChanged();
    void messageChanged();

private:
    int m_counter = 0;
    QString m_message = QStringLiteral("Hello from a singleton");
};
