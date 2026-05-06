#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QDir>
#include <QIcon>
#include <QDebug>
#include <QUrl>
#include <cstdio>

#include <hlplayer/HLPlayer.h>

#ifdef BUILD_QML
#include <hlplayer/PreviewRenderer.h>
#endif

extern void qml_register_types_HLPlayer();

int main(int argc, char *argv[])
{
    hlplayer::sdk::init();

    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);

    QGuiApplication app(argc, argv);

    app.setWindowIcon(QIcon(":/icons/appIcon.ico"));

    QQuickStyle::setStyle("Basic");

    qml_register_types_HLPlayer();

    QQmlApplicationEngine engine;

    QString appDir = QCoreApplication::applicationDirPath();
    engine.addImportPath(appDir);
    engine.addImportPath(appDir + "/qml");
    engine.addImportPath(appDir + "/../qml");
    engine.addImportPath("D:/HLPlayer/build/src/qml");

    // Expose source root so QML can reference files outside the build tree
    engine.rootContext()->setContextProperty("sourceRoot", "D:/HLPlayer");

#ifdef BUILD_QML
    auto* cameraProvider = new hlplayer::CameraPreviewProvider();
    engine.addImageProvider("camera", cameraProvider);
    hlplayer::PreviewRenderer::setEngineProvider(cameraProvider);
#endif

    const QString mainQmlPath = appDir + "/../qml/HLPlayer/HLPlayer/Main.qml";
    QQmlComponent component(&engine, QUrl::fromLocalFile(mainQmlPath));
    if (component.isError()) {
        fprintf(stderr, "FATAL: QML component errors:\n");
        for (const auto& e : component.errors())
            fprintf(stderr, "  %s\n", e.toString().toUtf8().constData());
        return -1;
    }

    QObject* root = component.create();
    if (!root) {
        fprintf(stderr, "FATAL: QML create() failed\n");
        for (const auto& e : component.errors())
            fprintf(stderr, "  %s\n", e.toString().toUtf8().constData());
        return -1;
    }

    if (auto window = qobject_cast<QQuickWindow*>(root)) {
        window->show();
    }

    int ret = app.exec();

    hlplayer::sdk::shutdown();

    return ret;
}
