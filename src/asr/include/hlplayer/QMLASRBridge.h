#pragma once

#ifndef HLPLAYER_ASR_BRIDGE_API
#ifdef _WIN32
    #if defined(HLPLAYER_QML_EXPORTS)
        #define HLPLAYER_ASR_BRIDGE_API __declspec(dllexport)
    #elif defined(HLPLAYER_ASR_EXPORTS)
        #define HLPLAYER_ASR_BRIDGE_API __declspec(dllexport)
    #else
        #define HLPLAYER_ASR_BRIDGE_API __declspec(dllimport)
    #endif
#else
    #define HLPLAYER_ASR_BRIDGE_API
#endif
#endif

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QTimer>
#include <memory>
#include <string>
#include <vector>

#include <hlplayer/ASRTypes.h>

namespace hlplayer {

class EventBus;

namespace asr {

class HLPLAYER_ASR_BRIDGE_API QMLASRBridge : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(QString currentSubtitleText READ currentSubtitleText NOTIFY currentSubtitleTextChanged)
    Q_PROPERTY(QString translatedSubtitleText READ translatedSubtitleText NOTIFY translatedSubtitleTextChanged)
    Q_PROPERTY(bool isRunning READ isRunning NOTIFY isRunningChanged)
    Q_PROPERTY(int asrState READ asrState NOTIFY asrStateChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(int audioSource READ audioSource WRITE setAudioSource NOTIFY audioSourceChanged)
    Q_PROPERTY(int displayMode READ displayMode WRITE setDisplayMode NOTIFY displayModeChanged)
    Q_PROPERTY(QVariantList modelList READ modelList NOTIFY modelListChanged)
    Q_PROPERTY(int fontSize READ fontSize WRITE setFontSize NOTIFY fontSizeChanged)
    Q_PROPERTY(QString fontColor READ fontColor WRITE setFontColor NOTIFY fontColorChanged)
    Q_PROPERTY(bool modelReady READ modelReady NOTIFY modelReadyChanged)
    Q_PROPERTY(QString modelLoadingStatus READ modelLoadingStatus NOTIFY modelLoadingStatusChanged)

public:
    enum ASRDisplayMode {
        OriginalOnly = 0,
        TranslationOnly,
        Bilingual
    };
    Q_ENUM(ASRDisplayMode)

    explicit QMLASRBridge(EventBus* eventBus = nullptr, QObject* parent = nullptr);
    ~QMLASRBridge() override;

    bool enabled() const;
    Q_INVOKABLE void setEnabled(bool enabled);

    QString currentSubtitleText() const;
    QString translatedSubtitleText() const;
    bool isRunning() const;
    int asrState() const;
    QString language() const;
    int audioSource() const;
    int displayMode() const;
    QVariantList modelList() const;

    int fontSize() const;
    Q_INVOKABLE void setFontSize(int size);
    QString fontColor() const;
    Q_INVOKABLE void setFontColor(const QString& color);

    Q_INVOKABLE void setLanguage(const QString& language);
    Q_INVOKABLE void setAudioSource(int source);
    Q_INVOKABLE void setDisplayMode(int mode);

    Q_INVOKABLE void toggleASR();
    Q_INVOKABLE void setModel(const QString& modelPath);
    Q_INVOKABLE void exportSRT(const QString& filePath);
    Q_INVOKABLE void initFromPlayer(QObject* qmlPlayer);
    Q_INVOKABLE void setModelDirectory(const QString& dir);

    bool modelReady() const;
    QString modelLoadingStatus() const;

signals:
    void enabledChanged();
    void currentSubtitleTextChanged();
    void translatedSubtitleTextChanged();
    void isRunningChanged();
    void asrStateChanged();
    void languageChanged();
    void audioSourceChanged();
    void displayModeChanged();
    void modelListChanged();
    void fontSizeChanged();
    void fontColorChanged();
    void errorOccurred(const QString& errorMessage);
    void modelReadyChanged();
    void modelLoadingStatusChanged();

private:
    void setupEventBusSubscription();
    void ensureAudioCallbackRegistered();
    void startPipeline();
    void stopPipeline();
    void handleSubtitleReadyEvent(const QString& text, const QString& translation,
                                   double startTime, double endTime,
                                   const QString& language, int sequenceId);
    void handleASRStateChangedEvent(int oldState, int newState);
    void beginPhasedPreload();
    void startPhase2Upgrade();
    std::string findBestAvailableModel(const std::vector<std::string>& priority) const;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} 
} 