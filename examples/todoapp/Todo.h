#pragma once

#include <QObject>
#include <QRangeModel>
#include <QString>
#include <QList>
#include <QtQml/qqmlregistration.h>

// ---------------------------------------------------------------------------
// TodoEntry — lightweight value type for a single todo item
// ---------------------------------------------------------------------------

class TodoEntry
{
    Q_GADGET
    Q_PROPERTY(QString title MEMBER title)
    Q_PROPERTY(bool done MEMBER done)

public:
    QString title;
    bool done = false;
};

// Tag as multi-role so QML ListView sees "title" and "done" as role names
template <>
struct QRangeModel::RowOptions<TodoEntry>
{
    static constexpr auto rowCategory = QRangeModel::RowCategory::MultiRoleItem;
};

// ---------------------------------------------------------------------------
// Todo — the single backend class exposed to QML
// ---------------------------------------------------------------------------

class Todo : public QRangeModel
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    explicit Todo(QObject *parent = nullptr);

    int count() const;

    Q_INVOKABLE void add(const QString &title);
    Q_INVOKABLE void remove(int index);
    Q_INVOKABLE void toggle(int index);
    Q_INVOKABLE void setTitle(int index, const QString &title);
    Q_INVOKABLE QString titleAt(int index) const;
    Q_INVOKABLE bool doneAt(int index) const;
    Q_INVOKABLE void save();
    Q_INVOKABLE void load();

signals:
    void countChanged();

private:
    void resolveRoles();

    QList<TodoEntry> m_items;
    QString m_savePath;
    int m_titleRole = -1;
    int m_doneRole  = -1;
};
