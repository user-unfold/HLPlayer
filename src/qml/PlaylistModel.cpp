#include "PlaylistModel.h"

#include <QFileInfo>

#include "HlvHeader.h"

namespace hlplayer {
namespace qml {

PlaylistModel::PlaylistModel(QObject* parent)
    : QAbstractListModel(parent) {}

int PlaylistModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return entries_.size();
}

QVariant PlaylistModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= entries_.size())
        return {};

    const Entry& entry = entries_[index.row()];
    switch (role) {
        case TitleRole: return entry.title;
        case UrlRole: return entry.url;
        case DurationRole: return entry.duration;
        case IsPlayingRole: return index.row() == currentIndex_;
        case IsEncryptedRole:
            return hlplayer::crypto::hasHlvExtension(entry.url.toStdString());
        default: return {};
    }
}

QHash<int, QByteArray> PlaylistModel::roleNames() const {
    return {
        {TitleRole, "title"},
        {UrlRole, "url"},
        {DurationRole, "duration"},
        {IsPlayingRole, "isPlaying"},
        {IsEncryptedRole, "isEncrypted"}
    };
}

int PlaylistModel::count() const {
    return entries_.size();
}

int PlaylistModel::currentIndex() const {
    return currentIndex_;
}

void PlaylistModel::setCurrentIndex(int idx) {
    if (idx == currentIndex_) return;
    int oldIndex = currentIndex_;
    currentIndex_ = idx;
    emit currentIndexChanged();
    if (oldIndex >= 0 && oldIndex < entries_.size())
        emit dataChanged(QAbstractListModel::index(oldIndex), QAbstractListModel::index(oldIndex), {IsPlayingRole});
    if (currentIndex_ >= 0 && currentIndex_ < entries_.size())
        emit dataChanged(QAbstractListModel::index(currentIndex_), QAbstractListModel::index(currentIndex_), {IsPlayingRole});
}

void PlaylistModel::add(const QString& url) {
    beginInsertRows(QModelIndex(), entries_.size(), entries_.size());
    Entry entry;
    entry.url = url;
    entry.title = extractTitle(url);
    entries_.append(entry);
    endInsertRows();
    emit countChanged();
}

void PlaylistModel::addFiles(const QStringList& urls) {
    beginInsertRows(QModelIndex(), entries_.size(), entries_.size() + urls.size() - 1);
    for (const auto& url : urls) {
        Entry entry;
        entry.url = url;
        entry.title = extractTitle(url);
        entries_.append(entry);
    }
    endInsertRows();
    emit countChanged();
}

void PlaylistModel::remove(int index) {
    if (index < 0 || index >= entries_.size()) return;
    beginRemoveRows(QModelIndex(), index, index);
    entries_.removeAt(index);
    endRemoveRows();
    if (currentIndex_ == index) {
        currentIndex_ = -1;
        emit currentIndexChanged();
    } else if (currentIndex_ > index) {
        currentIndex_--;
        emit currentIndexChanged();
    }
    emit countChanged();
}

void PlaylistModel::clear() {
    if (entries_.isEmpty()) return;
    beginResetModel();
    entries_.clear();
    currentIndex_ = -1;
    endResetModel();
    emit countChanged();
    emit currentIndexChanged();
}

QString PlaylistModel::getUrl(int index) const {
    if (index < 0 || index >= entries_.size()) return {};
    return entries_[index].url;
}

QString PlaylistModel::getTitle(int index) const {
    if (index < 0 || index >= entries_.size()) return {};
    return entries_[index].title;
}

QString PlaylistModel::extractTitle(const QString& url) {
    QString path = url;
    if (path.startsWith("file:///"))
        path = QUrl(url).toLocalFile();
    return QFileInfo(path).fileName();
}

} // namespace qml
} // namespace hlplayer
