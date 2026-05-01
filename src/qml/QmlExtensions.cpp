#include "QmlExtensions.h"

#include <QStandardPaths>

namespace hlplayer {
namespace qml {

QmlExtensions::QmlExtensions(QObject* parent)
    : QObject(parent)
{
}

QmlExtensions::~QmlExtensions() = default;

QString QmlExtensions::getDocumentsPath() const {
    QString path = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (path.isEmpty()) {
        path = ".";
    }
    return path;
}

} // namespace qml
} // namespace hlplayer
