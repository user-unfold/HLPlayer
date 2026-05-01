#include "UrlHistory.h"
#include <spdlog/spdlog.h>

namespace hlplayer {
namespace qml {

UrlHistory::UrlHistory(QObject* parent)
    : QAbstractListModel(parent) {
    loadAsync();
}

UrlHistory::~UrlHistory() = default;

int UrlHistory::rowCount(const QModelIndex& parent) const {
    if (parent.isValid())
        return 0;
    return entries_.size();
}

QVariant UrlHistory::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= entries_.size())
        return QVariant();

    const Entry& entry = entries_[index.row()];

    switch (role) {
    case UrlRole:
        return entry.url;
    case LabelRole:
        return entry.label;
    case TimestampRole:
        return entry.timestamp;
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> UrlHistory::roleNames() const {
    QHash<int, QByteArray> roles;
    roles[UrlRole] = "url";
    roles[LabelRole] = "label";
    roles[TimestampRole] = "timestamp";
    return roles;
}

void UrlHistory::addUrl(const QString& url, const QString& label) {
    QString trimmedUrl = url.trimmed();
    if (trimmedUrl.isEmpty())
        return;

    int existingIndex = findUrlIndex(trimmedUrl);
    Entry newEntry{trimmedUrl, label, QDateTime::currentSecsSinceEpoch()};

    if (existingIndex >= 0) {
        beginMoveRows(QModelIndex(), existingIndex, existingIndex, QModelIndex(), 0);
        entries_.removeAt(existingIndex);
        entries_.prepend(newEntry);
        endMoveRows();
        spdlog::info("UrlHistory: Updated existing URL: {}", trimmedUrl.toStdString());
    } else {
        beginInsertRows(QModelIndex(), 0, 0);
        entries_.prepend(newEntry);
        endInsertRows();

        if (entries_.size() > MAX_ENTRIES) {
            beginRemoveRows(QModelIndex(), MAX_ENTRIES, entries_.size() - 1);
            entries_.remove(MAX_ENTRIES, entries_.size() - MAX_ENTRIES);
            endRemoveRows();
            spdlog::info("UrlHistory: Evicted old entries, now has {} entries", entries_.size());
        } else {
            spdlog::info("UrlHistory: Added new URL: {}", trimmedUrl.toStdString());
        }
    }

    save();
}

void UrlHistory::removeUrl(int index) {
    if (index < 0 || index >= entries_.size())
        return;

    beginRemoveRows(QModelIndex(), index, index);
    QString removedUrl = entries_[index].url;
    entries_.removeAt(index);
    endRemoveRows();

    spdlog::info("UrlHistory: Removed URL: {}", removedUrl.toStdString());
    save();
}

void UrlHistory::clearAll() {
    if (entries_.isEmpty())
        return;

    beginResetModel();
    entries_.clear();
    endResetModel();

    spdlog::info("UrlHistory: Cleared all entries");
    save();
}

QJsonArray UrlHistory::getUrls() const {
    QJsonArray result;
    for (const Entry& entry : entries_) {
        QJsonObject obj;
        obj["url"] = entry.url;
        obj["label"] = entry.label;
        obj["timestamp"] = entry.timestamp;
        result.append(obj);
    }
    return result;
}

void UrlHistory::loadAsync() {
    QTimer::singleShot(0, this, [this]() {
        if (isLoading_)
            return;
        isLoading_ = true;
        loadInternal();
        isLoading_ = false;
    });
}

void UrlHistory::save() {
    QTimer::singleShot(0, this, [this]() {
        saveInternal();
    });
}

void UrlHistory::loadInternal() {
    QString filePath = getFilePath();
    QFile file(filePath);
    if (!file.exists()) {
        spdlog::info("UrlHistory: History file does not exist, starting fresh: {}", filePath.toStdString());
        return;
    }

    if (!file.open(QIODevice::ReadOnly)) {
        spdlog::error("UrlHistory: Failed to open history file: {}", filePath.toStdString());
        return;
    }

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        spdlog::error("UrlHistory: Failed to parse JSON: {}", parseError.errorString().toStdString());
        return;
    }

    QJsonArray array = doc.array();
    if (array.isEmpty()) {
        spdlog::info("UrlHistory: History file is empty");
        return;
    }

    beginResetModel();
    entries_.clear();

    for (const QJsonValue& value : array) {
        QJsonObject obj = value.toObject();
        Entry entry;
        entry.url = obj["url"].toString();
        entry.label = obj["label"].toString();
        entry.timestamp = obj["timestamp"].toVariant().toLongLong();
        entries_.append(entry);
    }

    endResetModel();
    spdlog::info("UrlHistory: Loaded {} entries from file", entries_.size());
}

void UrlHistory::saveInternal() {
    QString filePath = getFilePath();
    QFileInfo fileInfo(filePath);
    QDir dir = fileInfo.absoluteDir();
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            spdlog::error("UrlHistory: Failed to create directory: {}", dir.absolutePath().toStdString());
            return;
        }
    }

    QJsonArray array;
    for (const Entry& entry : entries_) {
        QJsonObject obj;
        obj["url"] = entry.url;
        obj["label"] = entry.label;
        obj["timestamp"] = entry.timestamp;
        array.append(obj);
    }

    QJsonDocument doc(array);
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        spdlog::error("UrlHistory: Failed to open file for writing: {}", filePath.toStdString());
        return;
    }

    file.write(doc.toJson());
    file.close();

    spdlog::info("UrlHistory: Saved {} entries to file", entries_.size());
}

QString UrlHistory::getFilePath() const {
    QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return dataPath + "/hlplayer/url_history.json";
}

int UrlHistory::findUrlIndex(const QString& url) const {
    for (int i = 0; i < entries_.size(); ++i) {
        if (entries_[i].url == url) {
            return i;
        }
    }
    return -1;
}

} // namespace qml
} // namespace hlplayer
