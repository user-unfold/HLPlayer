#ifndef HLPLAYER_PLAYLISTMODEL_H
#define HLPLAYER_PLAYLISTMODEL_H

#include <QAbstractListModel>
#include <QUrl>
#include <QStringList>
#include <qqmlregistration.h>

namespace hlplayer {
namespace qml {

class PlaylistModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)

public:
    enum Roles {
        TitleRole = Qt::UserRole + 1,
        UrlRole,
        DurationRole,
        IsPlayingRole,
        IsEncryptedRole
    };

    explicit PlaylistModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const;
    int currentIndex() const;
    void setCurrentIndex(int idx);

    Q_INVOKABLE void add(const QString& url);
    Q_INVOKABLE void addFiles(const QStringList& urls);
    Q_INVOKABLE void remove(int index);
    Q_INVOKABLE void clear();
    Q_INVOKABLE QString getUrl(int index) const;
    Q_INVOKABLE QString getTitle(int index) const;

signals:
    void countChanged();
    void currentIndexChanged();

private:
    struct Entry {
        QString url;
        QString title;
        double duration = 0.0;
    };

    QList<Entry> entries_;
    int currentIndex_ = -1;

    static QString extractTitle(const QString& url);
};

} // namespace qml
} // namespace hlplayer

#endif // HLPLAYER_PLAYLISTMODEL_H
