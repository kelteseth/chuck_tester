#include "Todo.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

Todo::Todo(QObject *parent)
    : QRangeModel(std::ref(m_items), parent)
{
    // Determine a portable save path
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    m_savePath = dir + "/todos.json";

    // Role names are available even before items exist (derived from Q_GADGET)
    resolveRoles();

    // Load persisted data
    load();

    connect(this, &QAbstractItemModel::rowsInserted, this, &Todo::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved,  this, &Todo::countChanged);
    connect(this, &QAbstractItemModel::modelReset,   this, &Todo::countChanged);
}

// ---------------------------------------------------------------------------
// Role helpers
// ---------------------------------------------------------------------------

void Todo::resolveRoles()
{
    const auto roles = roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it) {
        if (it.value() == "title")
            m_titleRole = it.key();
        else if (it.value() == "done")
            m_doneRole = it.key();
    }
}

// ---------------------------------------------------------------------------
// Q_INVOKABLE methods
// ---------------------------------------------------------------------------

int Todo::count() const
{
    return rowCount();
}

void Todo::add(const QString &title)
{
    const int row = rowCount();
    insertRow(row);
    setData(index(row, 0), title, m_titleRole);
    setData(index(row, 0), false, m_doneRole);
}

void Todo::remove(int index)
{
    if (index < 0 || index >= rowCount())
        return;
    removeRow(index);
}

void Todo::toggle(int index)
{
    if (index < 0 || index >= rowCount())
        return;
    const auto mi = this->index(index, 0);
    const bool current = data(mi, m_doneRole).toBool();
    setData(mi, !current, m_doneRole);
}

void Todo::setTitle(int index, const QString &title)
{
    if (index < 0 || index >= rowCount())
        return;
    setData(this->index(index, 0), title, m_titleRole);
}

QString Todo::titleAt(int index) const
{
    if (index < 0 || index >= rowCount())
        return {};
    return data(this->index(index, 0), m_titleRole).toString();
}

bool Todo::doneAt(int index) const
{
    if (index < 0 || index >= rowCount())
        return false;
    return data(this->index(index, 0), m_doneRole).toBool();
}

// ---------------------------------------------------------------------------
// Persistence (simple JSON)
// ---------------------------------------------------------------------------

void Todo::save()
{
    QJsonArray arr;
    for (const auto &item : m_items) {
        arr.append(QJsonObject{
            {"title", item.title},
            {"done",  item.done},
        });
    }

    QFile f(m_savePath);
    const auto dir = QFileInfo(f).absolutePath();
    QDir().mkpath(dir);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
}

void Todo::load()
{
    QFile f(m_savePath);
    if (!f.open(QIODevice::ReadOnly))
        return;

    const auto arr = QJsonDocument::fromJson(f.readAll()).array();
    if (arr.isEmpty())
        return;

    // Clear existing items through the model API
    if (rowCount() > 0)
        removeRows(0, rowCount());

    // Insert loaded items
    for (const auto &val : arr) {
        const auto obj = val.toObject();
        const int row = rowCount();
        insertRow(row);
        setData(index(row, 0), obj["title"].toString(), m_titleRole);
        setData(index(row, 0), obj["done"].toBool(false), m_doneRole);
    }
}
