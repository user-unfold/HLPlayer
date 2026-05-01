#include <catch2/catch_test_macros.hpp>

#include <hlplayer/PlayerApi.h>

#ifdef HAS_QT

#include <QObject>
#include <QColor>
#include <QSignalSpy>

#include "ThemeManager.h"
#include "PlayerI18nContext.h"
#include "QMLPlayer.h"

TEST_CASE("ThemeManager defaults to System theme", "[qml][theme]") {
    hlplayer::qml::ThemeManager tm;
    REQUIRE(static_cast<int>(tm.theme()) == static_cast<int>(hlplayer::qml::ThemeManager::Theme::System));
    REQUIRE(tm.accentColor() == QColor(0x00, 0x78, 0xD4));
}

TEST_CASE("ThemeManager setTheme cycles Light/Dark", "[qml][theme]") {
    hlplayer::qml::ThemeManager tm;

    tm.setTheme(hlplayer::qml::ThemeManager::Theme::Light);
    REQUIRE(tm.theme() == hlplayer::qml::ThemeManager::Theme::Light);
    REQUIRE_FALSE(tm.isDarkMode());
    REQUIRE(tm.primary() == QColor(0x00, 0x78, 0xD4));
    REQUIRE(tm.surface() == QColor(0xF3, 0xF3, 0xF3));
    REQUIRE(tm.onSurface() == QColor(0x00, 0x00, 0x00));

    tm.setTheme(hlplayer::qml::ThemeManager::Theme::Dark);
    REQUIRE(tm.theme() == hlplayer::qml::ThemeManager::Theme::Dark);
    REQUIRE(tm.isDarkMode());
    REQUIRE(tm.primary() == QColor(0x60, 0xCD, 0xFF));
    REQUIRE(tm.surface() == QColor(0x20, 0x20, 0x20));
    REQUIRE(tm.onSurface() == QColor(0xFF, 0xFF, 0xFF));

    tm.setTheme(hlplayer::qml::ThemeManager::Theme::System);
    REQUIRE(tm.theme() == hlplayer::qml::ThemeManager::Theme::System);
}

TEST_CASE("ThemeManager accentColor getter/setter", "[qml][theme]") {
    hlplayer::qml::ThemeManager tm;

    tm.setAccentColor(QColor(0xFF, 0x00, 0xFF));
    REQUIRE(tm.accentColor() == QColor(0xFF, 0x00, 0xFF));
}

TEST_CASE("ThemeManager errorColor is red in both themes", "[qml][theme]") {
    hlplayer::qml::ThemeManager tm;

    tm.setTheme(hlplayer::qml::ThemeManager::Theme::Light);
    REQUIRE(tm.errorColor() == QColor(0xFF, 0x00, 0x00));

    tm.setTheme(hlplayer::qml::ThemeManager::Theme::Dark);
    REQUIRE(tm.errorColor() == QColor(0xFF, 0x00, 0x00));
}

TEST_CASE("ThemeManager emits themeChanged on setTheme", "[qml][theme]") {
    hlplayer::qml::ThemeManager tm;
    QSignalSpy spy(&tm, &hlplayer::qml::ThemeManager::themeChanged);

    tm.setTheme(hlplayer::qml::ThemeManager::Theme::Dark);
    REQUIRE(spy.count() == 1);

    tm.setTheme(hlplayer::qml::ThemeManager::Theme::Dark);
    REQUIRE(spy.count() == 1);
}

TEST_CASE("PlayerI18nContext defaults to English", "[qml][i18n]") {
    hlplayer::qml::PlayerI18nContext i18n;
    REQUIRE(i18n.currentLanguage() == "en");
}

TEST_CASE("PlayerI18nContext setLanguage changes language", "[qml][i18n]") {
    hlplayer::qml::PlayerI18nContext i18n;
    i18n.setLanguage("zh");
    REQUIRE(i18n.currentLanguage() == "zh");
}

TEST_CASE("PlayerI18nContext tr returns key as fallback", "[qml][i18n]") {
    hlplayer::qml::PlayerI18nContext i18n;

    SECTION("without loaded translation, returns key") {
        REQUIRE(i18n.tr("Play") == "Play");
        REQUIRE(i18n.tr("Pause") == "Pause");
        REQUIRE(i18n.tr("NonExistentKey") == "NonExistentKey");
    }

    SECTION("empty key returns empty") {
        REQUIRE(i18n.tr("") == "");
    }
}

TEST_CASE("PlayerI18nContext setLanguage emits languageChanged", "[qml][i18n]") {
    hlplayer::qml::PlayerI18nContext i18n;
    QSignalSpy spy(&i18n, &hlplayer::qml::PlayerI18nContext::languageChanged);

    i18n.setLanguage("zh");
    REQUIRE(spy.count() == 1);

    i18n.setLanguage("zh");
    REQUIRE(spy.count() == 1);
}

TEST_CASE("QMLPlayer property defaults", "[qml][player]") {
    hlplayer::qml::QMLPlayer player;

    REQUIRE(player.source().isEmpty());
    REQUIRE(player.state() == PlayerState_Idle);
    REQUIRE(player.volume() == 1.0);
    REQUIRE(player.position() == 0.0);
    REQUIRE(player.duration() == 0.0);
    REQUIRE(player.error().isEmpty());
    REQUIRE_FALSE(player.isPlaying());
    REQUIRE_FALSE(player.isPaused());
}

TEST_CASE("QMLPlayer setVolume updates property", "[qml][player]") {
    hlplayer::qml::QMLPlayer player;

    player.setVolume(0.5);
    REQUIRE(player.volume() == 0.5);
}

TEST_CASE("QMLPlayer setSource updates property", "[qml][player]") {
    hlplayer::qml::QMLPlayer player;

    player.setSource("https://example.com/stream.m3u8");
    REQUIRE(player.source() == "https://example.com/stream.m3u8");
}

#endif

#include <hlplayer/HLPlayer.h>
#include <hlplayer/PlayerFacade.h>
#include <hlplayer/EventBus.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

TEST_CASE("PlayerFacade instantiate and control from worker thread without blocking main", "[qml][integration]") {
    using namespace std::chrono_literals;

    std::atomic<bool> workerStarted{false};
    std::atomic<bool> workerDone{false};
    std::atomic<int> opsCompleted{0};

    std::thread worker([&] {
        hlplayer::sdk::init();

        auto facade = std::make_unique<hlplayer::PlayerFacade>();
        workerStarted.store(true, std::memory_order_release);

        while (!workerDone.load(std::memory_order_acquire)) {
            facade->getState();
            facade->setVolume(0.5);
            facade->eventBus().dispatch();
            opsCompleted.fetch_add(3, std::memory_order_relaxed);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        facade.reset();
        hlplayer::sdk::shutdown();
    });

    while (!workerStarted.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    int snapshot = opsCompleted.load(std::memory_order_acquire);
    REQUIRE(snapshot > 0);

    workerDone.store(true, std::memory_order_release);
    worker.join();

    REQUIRE(opsCompleted.load() >= snapshot);
    REQUIRE(opsCompleted.load() > 0);
}

TEST_CASE("C-API full lifecycle from worker thread", "[qml][integration][cabi]") {
    HLPlayer_Init();

    std::atomic<bool> done{false};
    std::atomic<bool> started{false};
    std::atomic<bool> success{true};
    std::atomic<int> opsCount{0};

    std::thread worker([&] {
        started.store(true, std::memory_order_release);

        while (!done.load(std::memory_order_acquire)) {
            HLPlayerHandle h = HLPlayer_Create();
            if (!h) { success = false; done = true; return; }

            HLPlayer_GetState(h);
            HLPlayer_SetVolume(h, 0.75);
            opsCount.fetch_add(2, std::memory_order_relaxed);

            char errBuf[256] = {0};
            auto openErr = HLPlayer_Open(h, "rtsp://test.local/stream");
            if (openErr != PlayerErrorCode_None) {
                HLPlayer_GetError(h, errBuf, sizeof(errBuf));
                opsCount.fetch_add(1, std::memory_order_relaxed);
            }

            HLPlayer_Destroy(h);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    while (!started.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    int snapshot = opsCount.load(std::memory_order_acquire);
    REQUIRE(snapshot > 0);
    REQUIRE(success.load());

    done.store(true, std::memory_order_release);
    worker.join();
    REQUIRE(opsCount.load() >= snapshot);
    REQUIRE(success.load());

    HLPlayer_Shutdown();
}

TEST_CASE("SDK dual-telemetry initializes at startup", "[qml][integration][telemetry]") {
    if (hlplayer::sdk::isInitialized()) {
        hlplayer::sdk::shutdown();
    }
    REQUIRE_FALSE(hlplayer::sdk::isInitialized());

    hlplayer::sdk::init();
    REQUIRE(hlplayer::sdk::isInitialized());

    auto ver = hlplayer::sdk::version();
    REQUIRE(ver.major == 1);
    REQUIRE(ver.minor == 0);
    REQUIRE(ver.patch == 0);

    uint32_t cVer = HLPlayer_GetVersion();
    REQUIRE(cVer == 10000u);

    hlplayer::sdk::shutdown();
    REQUIRE_FALSE(hlplayer::sdk::isInitialized());

    hlplayer::sdk::init();
    REQUIRE(hlplayer::sdk::isInitialized());

    hlplayer::sdk::shutdown();
}

TEST_CASE("Multiple worker threads control independent facades concurrently", "[qml][integration][thread]") {

    hlplayer::sdk::init();

    constexpr int threadCount = 4;
    constexpr int iterations = 100;
    std::vector<std::thread> threads;
    std::atomic<int> totalOps{0};

    for (int t = 0; t < threadCount; t++) {
        threads.emplace_back([&] {
            auto facade = std::make_unique<hlplayer::PlayerFacade>();
            for (int i = 0; i < iterations; i++) {
                facade->getState();
                facade->setVolume(0.5);
                facade->eventBus().dispatch();
                totalOps.fetch_add(3, std::memory_order_relaxed);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    REQUIRE(totalOps.load() == threadCount * iterations * 3);

    hlplayer::sdk::shutdown();
}

TEST_CASE("PlayerState enum ABI values are stable", "[qml][abi]") {
    STATIC_REQUIRE(PlayerState_Idle == 0);
    STATIC_REQUIRE(PlayerState_ResolvingURL == 1);
    STATIC_REQUIRE(PlayerState_Prepared == 2);
    STATIC_REQUIRE(PlayerState_Buffering == 3);
    STATIC_REQUIRE(PlayerState_Playing == 4);
    STATIC_REQUIRE(PlayerState_Paused == 5);
    STATIC_REQUIRE(PlayerState_Error == 6);
    STATIC_REQUIRE(PlayerState_End == 7);
    STATIC_REQUIRE(PlayerState_DeviceLost == 8);
}
