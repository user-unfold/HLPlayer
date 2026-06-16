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
#include <TmpCleaner.h>

#ifdef BUILD_QML
#include <hlplayer/PreviewRenderer.h>
#endif

#ifdef _WIN32
#include <windows.h>
#endif

extern void qml_register_types_HLPlayer();

int main(int argc, char *argv[])
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif

    hlplayer::sdk::init();

    // Clean up leftover .hlv.tmp files from previous runs
    hlplayer::app::TmpCleaner::cleanupTmpFiles();

    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);

    QGuiApplication app(argc, argv);

    app.setWindowIcon(QIcon(":/icons/appIcon.ico"));

    QQuickStyle::setStyle("Basic");

    qml_register_types_HLPlayer();

    QQmlApplicationEngine engine;

    QString appDir = QCoreApplication::applicationDirPath();
    engine.addImportPath(appDir);
    engine.addImportPath(appDir + "/qml");

    // Expose app directory to QML (replaces hardcoded D:/HLPlayer)
    engine.rootContext()->setContextProperty("sourceRoot", appDir);
    engine.rootContext()->setContextProperty("appDir", appDir);

#ifdef BUILD_QML
    auto* cameraProvider = new hlplayer::CameraPreviewProvider();
    engine.addImageProvider("camera", cameraProvider);
    hlplayer::PreviewRenderer::setEngineProvider(cameraProvider);
#endif

    // Try QRC first (installed), then file path (dev build)
    QUrl mainUrl(QStringLiteral("qrc:/qt-project.org/imports/HLPlayer/HLPlayer/Main.qml"));
    QQmlComponent component(&engine, mainUrl);
    if (component.isError()) {
        // Fallback: load from filesystem for development
        QUrl devUrl = QUrl::fromLocalFile(appDir + "/../qml/HLPlayer/HLPlayer/Main.qml");
        component.loadUrl(devUrl);
    }
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
