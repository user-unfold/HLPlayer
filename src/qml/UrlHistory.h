#ifndef HLPLAYER_URLHISTORY_H
#define HLPLAYER_URLHISTORY_H

#include <QAbstractListModel>
#include <QString>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QStandardPaths>
#include <QDir>
#include <QJsonDocument>
#include <QTimer>
#include <qqmlregistration.h>

namespace hlplayer {
namespace qml {

class UrlHistory : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT

public:
    enum Roles {
        UrlRole = Qt::UserRole + 1,
        LabelRole,
        TimestampRole
    };

    explicit UrlHistory(QObject* parent = nullptr);
    ~UrlHistory() override;

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void addUrl(const QString& url, const QString& label = "");
    Q_INVOKABLE void removeUrl(int index);
    Q_INVOKABLE void clearAll();
    Q_INVOKABLE QJsonArray getUrls() const;
    Q_INVOKABLE void loadAsync();
    Q_INVOKABLE void save();

private:
    struct Entry {
        QString url;
        QString label;
        qint64 timestamp;
    };

    void saveInternal();
    void loadInternal();
    QString getFilePath() const;
    int findUrlIndex(const QString& url) const;

    QList<Entry> entries_;
    static constexpr int MAX_ENTRIES = 20;
    bool isLoading_ = false;
};

} // namespace qml
} // namespace hlplayer

#endif // HLPLAYER_URLHISTORY_H
