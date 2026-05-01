#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <hlplayer/StateMachine.h>
#include <hlplayer/EventBus.h>
#include <hlplayer/PlayerFacade.h>
#include <hlplayer/DirectStreamResolver.h>
#include <hlplayer/Result.h>

#include <QtAudioRenderer.h>
#include <VulkanVideoSink.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace hlplayer;
using Catch::Matchers::WithinAbs;

TEST_CASE("StateMachine starts in Idle", "[state-machine]") {
    StateMachine sm;
    REQUIRE(sm.getState() == PlayerState_Idle);
}

TEST_CASE("StateMachine valid transitions - open/resolve flow", "[state-machine]") {
    StateMachine sm;

    SECTION("Idle -> ResolvingURL on Open") {
        auto r = sm.transition(PlayerEvent::Open);
        REQUIRE(r.hasValue());
        REQUIRE(sm.getState() == PlayerState_ResolvingURL);
    }

    SECTION("ResolvingURL -> Prepared on ResolveSuccess") {
        sm.transition(PlayerEvent::Open);
        auto r = sm.transition(PlayerEvent::ResolveSuccess);
        REQUIRE(r.hasValue());
        REQUIRE(sm.getState() == PlayerState_Prepared);
    }

    SECTION("ResolvingURL -> Error on ResolveFailure") {
        sm.transition(PlayerEvent::Open);
        auto r = sm.transition(PlayerEvent::ResolveFailure);
        REQUIRE(r.hasValue());
        REQUIRE(sm.getState() == PlayerState_Error);
    }
}

TEST_CASE("StateMachine valid transitions - playback flow", "[state-machine]") {
    StateMachine sm;
    sm.transition(PlayerEvent::Open);
    sm.transition(PlayerEvent::ResolveSuccess);

    SECTION("Prepared -> Buffering on BufferReady") {
        auto r = sm.transition(PlayerEvent::BufferReady);
        REQUIRE(r.hasValue());
        REQUIRE(sm.getState() == PlayerState_Buffering);
    }

    SECTION("Prepared -> Idle on Stop") {
        auto r = sm.transition(PlayerEvent::Stop);
        REQUIRE(r.hasValue());
        REQUIRE(sm.getState() == PlayerState_Idle);
    }
}

TEST_CASE("StateMachine valid transitions - buffering to playing", "[state-machine]") {
    StateMachine sm;
    sm.transition(PlayerEvent::Open);
    sm.transition(PlayerEvent::ResolveSuccess);
    sm.transition(PlayerEvent::BufferReady);

    SECTION("Buffering -> Playing on Play") {
        auto r = sm.transition(PlayerEvent::Play);
        REQUIRE(r.hasValue());
        REQUIRE(sm.getState() == PlayerState_Playing);
    }

    SECTION("Buffering -> Paused on Pause") {
        auto r = sm.transition(PlayerEvent::Pause);
        REQUIRE(r.hasValue());
        REQUIRE(sm.getState() == PlayerState_Paused);
    }

    SECTION("Buffering -> Error on ErrorOccurred") {
        auto r = sm.transition(PlayerEvent::ErrorOccurred);
        REQUIRE(r.hasValue());
        REQUIRE(sm.getState() == PlayerState_Error);
    }

    SECTION("Buffering -> Idle on Stop") {
        auto r = sm.transition(PlayerEvent::Stop);
        REQUIRE(r.hasValue());
        REQUIRE(sm.getState() == PlayerState_Idle);
    }
}

TEST_CASE("StateMachine valid transitions - playing state", "[state-machine]") {
    StateMachine sm;
    sm.transition(PlayerEvent::Open);
    sm.transition(PlayerEvent::ResolveSuccess);
    sm.transition(PlayerEvent::BufferReady);
    sm.transition(PlayerEvent::Play);

    SECTION("Playing -> Paused on Pause") {
        auto r = sm.transition(PlayerEvent::Pause);
        REQUIRE(r.hasValue());
        REQUIRE(sm.getState() == PlayerState_Paused);
    }

    SECTION("Playing -> Buffering on BufferReady") {
        auto r = sm.transition(PlayerEvent::BufferReady);
        REQUIRE(r.hasValue());
        REQUIRE(sm.getState() == PlayerState_Buffering);
    }

    SECTION("Playing -> End on EndOfStream") {
        auto r = sm.transition(PlayerEvent::EndOfStream);
        REQUIRE(r.hasValue());
        REQUIRE(sm.getState() == PlayerState_End);
    }

    SECTION("Playing -> Error on ErrorOccurred") {
        auto r = sm.transition(PlayerEvent::ErrorOccurred);
        REQUIRE(r.hasValue());
        REQUIRE(sm.getState() == PlayerState_Error);
    }

    SECTION("Playing -> DeviceLost on DeviceLost") {
        auto r = sm.transition(PlayerEvent::DeviceLost);
        REQUIRE(r.hasValue());
        REQUIRE(sm.getState() == PlayerState_DeviceLost);
    }

    SECTION("Playing -> Idle on Stop") {
        auto r = sm.transition(PlayerEvent::Stop);
        REQUIRE(r.hasValue());
        REQUIRE(sm.getState() == PlayerState_Idle);
    }
}

TEST_CASE("StateMachine valid transitions - paused state", "[state-machine]") {
    StateMachine sm;
    sm.transition(PlayerEvent::Open);
    sm.transition(PlayerEvent::ResolveSuccess);
    sm.transition(PlayerEvent::BufferReady);
    sm.transition(PlayerEvent::Play);
    sm.transition(PlayerEvent::Pause);

    SECTION("Paused -> Playing on Resume") {
        auto r = sm.transition(PlayerEvent::Resume);
        REQUIRE(r.hasValue());
        REQUIRE(sm.getState() == PlayerState_Playing);
    }

    SECTION("Paused -> Playing on Play") {
        auto r = sm.transition(PlayerEvent::Play);
        REQUIRE(r.hasValue());
        REQUIRE(sm.getState() == PlayerState_Playing);
    }

    SECTION("Paused -> Idle on Stop") {
        auto r = sm.transition(PlayerEvent::Stop);
        REQUIRE(r.hasValue());
        REQUIRE(sm.getState() == PlayerState_Idle);
    }

    SECTION("Paused -> Error on ErrorOccurred") {
        auto r = sm.transition(PlayerEvent::ErrorOccurred);
        REQUIRE(r.hasValue());
        REQUIRE(sm.getState() == PlayerState_Error);
    }

    SECTION("Paused -> DeviceLost on DeviceLost") {
        auto r = sm.transition(PlayerEvent::DeviceLost);
        REQUIRE(r.hasValue());
        REQUIRE(sm.getState() == PlayerState_DeviceLost);
    }
}

TEST_CASE("StateMachine valid transitions - error and end recovery", "[state-machine]") {
    SECTION("Error -> Idle on Stop") {
        StateMachine sm;
        sm.transition(PlayerEvent::Open);
        sm.transition(PlayerEvent::ResolveFailure);
        REQUIRE(sm.getState() == PlayerState_Error);

        auto r = sm.transition(PlayerEvent::Stop);
        REQUIRE(r.hasValue());
        REQUIRE(sm.getState() == PlayerState_Idle);
    }

    SECTION("Error -> ResolvingURL on Open") {
        StateMachine sm;
        sm.transition(PlayerEvent::Open);
        sm.transition(PlayerEvent::ResolveFailure);
        REQUIRE(sm.getState() == PlayerState_Error);

        auto r = sm.transition(PlayerEvent::Open);
        REQUIRE(r.hasValue());
        REQUIRE(sm.getState() == PlayerState_ResolvingURL);
    }

    SECTION("End -> Idle on Stop") {
        StateMachine sm;
        sm.transition(PlayerEvent::Open);
        sm.transition(PlayerEvent::ResolveSuccess);
        sm.transition(PlayerEvent::BufferReady);
        sm.transition(PlayerEvent::Play);
        sm.transition(PlayerEvent::EndOfStream);
        REQUIRE(sm.getState() == PlayerState_End);

        auto r = sm.transition(PlayerEvent::Stop);
        REQUIRE(r.hasValue());
        REQUIRE(sm.getState() == PlayerState_Idle);
    }

    SECTION("End -> ResolvingURL on Open") {
        StateMachine sm;
        sm.transition(PlayerEvent::Open);
        sm.transition(PlayerEvent::ResolveSuccess);
        sm.transition(PlayerEvent::BufferReady);
        sm.transition(PlayerEvent::Play);
        sm.transition(PlayerEvent::EndOfStream);
        REQUIRE(sm.getState() == PlayerState_End);

        auto r = sm.transition(PlayerEvent::Open);
        REQUIRE(r.hasValue());
        REQUIRE(sm.getState() == PlayerState_ResolvingURL);
    }

    SECTION("DeviceLost -> ResolvingURL on RecoveryComplete") {
        StateMachine sm;
        sm.transition(PlayerEvent::Open);
        sm.transition(PlayerEvent::ResolveSuccess);
        sm.transition(PlayerEvent::BufferReady);
        sm.transition(PlayerEvent::Play);
        sm.transition(PlayerEvent::DeviceLost);
        REQUIRE(sm.getState() == PlayerState_DeviceLost);

        auto r = sm.transition(PlayerEvent::RecoveryComplete);
        REQUIRE(r.hasValue());
        REQUIRE(sm.getState() == PlayerState_ResolvingURL);
    }
}

TEST_CASE("StateMachine invalid transitions return InvalidState", "[state-machine]") {
    SECTION("Play in Idle rejected") {
        StateMachine sm;
        auto r = sm.transition(PlayerEvent::Play);
        REQUIRE(r.hasError());
        REQUIRE(r.error() == PlayerError::InvalidState);
        REQUIRE(sm.getState() == PlayerState_Idle);
    }

    SECTION("QA scenario: Play in ResolvingURL rejected, remains in ResolvingURL") {
        StateMachine sm;
        sm.transition(PlayerEvent::Open);
        REQUIRE(sm.getState() == PlayerState_ResolvingURL);

        auto r = sm.transition(PlayerEvent::Play);
        REQUIRE(r.hasError());
        REQUIRE(r.error() == PlayerError::InvalidState);
        REQUIRE(sm.getState() == PlayerState_ResolvingURL);
    }

    SECTION("Pause in Idle rejected") {
        StateMachine sm;
        auto r = sm.transition(PlayerEvent::Pause);
        REQUIRE(r.hasError());
        REQUIRE(r.error() == PlayerError::InvalidState);
        REQUIRE(sm.getState() == PlayerState_Idle);
    }

    SECTION("Resume in Idle rejected") {
        StateMachine sm;
        auto r = sm.transition(PlayerEvent::Resume);
        REQUIRE(r.hasError());
        REQUIRE(r.error() == PlayerError::InvalidState);
    }

    SECTION("Open in Playing rejected") {
        StateMachine sm;
        sm.transition(PlayerEvent::Open);
        sm.transition(PlayerEvent::ResolveSuccess);
        sm.transition(PlayerEvent::BufferReady);
        sm.transition(PlayerEvent::Play);
        REQUIRE(sm.getState() == PlayerState_Playing);

        auto r = sm.transition(PlayerEvent::Open);
        REQUIRE(r.hasError());
        REQUIRE(r.error() == PlayerError::InvalidState);
        REQUIRE(sm.getState() == PlayerState_Playing);
    }

    SECTION("EndOfStream in Idle rejected") {
        StateMachine sm;
        auto r = sm.transition(PlayerEvent::EndOfStream);
        REQUIRE(r.hasError());
        REQUIRE(r.error() == PlayerError::InvalidState);
    }

    SECTION("DeviceLost in Idle rejected") {
        StateMachine sm;
        auto r = sm.transition(PlayerEvent::DeviceLost);
        REQUIRE(r.hasError());
        REQUIRE(r.error() == PlayerError::InvalidState);
    }

    SECTION("RecoveryComplete in Idle rejected") {
        StateMachine sm;
        auto r = sm.transition(PlayerEvent::RecoveryComplete);
        REQUIRE(r.hasError());
        REQUIRE(r.error() == PlayerError::InvalidState);
    }

    SECTION("Seek in any state rejected (no valid Seek transitions)") {
        StateMachine sm;
        auto r = sm.transition(PlayerEvent::Seek);
        REQUIRE(r.hasError());
        REQUIRE(r.error() == PlayerError::InvalidState);

        sm.transition(PlayerEvent::Open);
        sm.transition(PlayerEvent::ResolveSuccess);
        sm.transition(PlayerEvent::BufferReady);
        sm.transition(PlayerEvent::Play);

        r = sm.transition(PlayerEvent::Seek);
        REQUIRE(r.hasError());
        REQUIRE(r.error() == PlayerError::InvalidState);
    }

    SECTION("Stop in Idle rejected") {
        StateMachine sm;
        auto r = sm.transition(PlayerEvent::Stop);
        REQUIRE(r.hasError());
        REQUIRE(r.error() == PlayerError::InvalidState);
    }
}

TEST_CASE("StateMachine reset forces Idle", "[state-machine]") {
    StateMachine sm;
    sm.transition(PlayerEvent::Open);
    sm.transition(PlayerEvent::ResolveSuccess);
    sm.transition(PlayerEvent::BufferReady);
    sm.transition(PlayerEvent::Play);
    REQUIRE(sm.getState() == PlayerState_Playing);

    sm.reset();
    REQUIRE(sm.getState() == PlayerState_Idle);
}

TEST_CASE("StateMachine isRecoverable", "[state-machine]") {
    SECTION("Error is recoverable") {
        StateMachine sm;
        sm.transition(PlayerEvent::Open);
        sm.transition(PlayerEvent::ResolveFailure);
        REQUIRE(sm.getState() == PlayerState_Error);
        REQUIRE(sm.isRecoverable());
    }

    SECTION("DeviceLost is recoverable") {
        StateMachine sm;
        sm.transition(PlayerEvent::Open);
        sm.transition(PlayerEvent::ResolveSuccess);
        sm.transition(PlayerEvent::BufferReady);
        sm.transition(PlayerEvent::Play);
        sm.transition(PlayerEvent::DeviceLost);
        REQUIRE(sm.getState() == PlayerState_DeviceLost);
        REQUIRE(sm.isRecoverable());
    }

    SECTION("Idle is not recoverable") {
        StateMachine sm;
        REQUIRE_FALSE(sm.isRecoverable());
    }

    SECTION("Playing is not recoverable") {
        StateMachine sm;
        sm.transition(PlayerEvent::Open);
        sm.transition(PlayerEvent::ResolveSuccess);
        sm.transition(PlayerEvent::BufferReady);
        sm.transition(PlayerEvent::Play);
        REQUIRE_FALSE(sm.isRecoverable());
    }
}

TEST_CASE("StateMachine concurrent transitions", "[state-machine]") {
    StateMachine sm;

    constexpr int threadCount = 8;
    constexpr int opsPerThread = 100;
    std::atomic<int> successCount{0};
    std::atomic<int> failCount{0};

    std::vector<std::thread> threads;
    threads.reserve(threadCount);

    for (int i = 0; i < threadCount; ++i) {
        threads.emplace_back([&sm, &successCount, &failCount]() {
            for (int j = 0; j < opsPerThread; ++j) {
                auto r = sm.transition(PlayerEvent::Open);
                if (r.hasValue()) {
                    successCount++;
                } else {
                    failCount++;
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    REQUIRE(successCount == 1);
    REQUIRE(failCount == threadCount * opsPerThread - 1);
    REQUIRE(sm.getState() == PlayerState_ResolvingURL);
}

TEST_CASE("StateMachine sequential multi-thread stress", "[state-machine]") {
    StateMachine sm;
    constexpr int threadCount = 4;

    SECTION("Open -> ResolveSuccess -> BufferReady -> Play sequentially by threads") {
        sm.transition(PlayerEvent::Open);
        sm.transition(PlayerEvent::ResolveSuccess);

        std::atomic<int> playSuccessCount{0};
        std::vector<std::thread> threads;
        threads.reserve(threadCount);

        sm.transition(PlayerEvent::BufferReady);

        for (int i = 0; i < threadCount; ++i) {
            threads.emplace_back([&sm, &playSuccessCount]() {
                auto r = sm.transition(PlayerEvent::Play);
                if (r.hasValue()) {
                    playSuccessCount++;
                }
            });
        }

        for (auto& t : threads) {
            t.join();
        }

        REQUIRE(playSuccessCount == 1);
        REQUIRE(sm.getState() == PlayerState_Playing);
    }
}

TEST_CASE("EventBus subscribe publish dispatch cycle", "[event-bus]") {
    EventBus bus;
    int callCount = 0;
    EventType receivedType{EventType::Error};

    int subId = bus.subscribe(EventType::StateChanged, [&](const Event& e) {
        callCount++;
        receivedType = e.type;
    });

    Event e{EventType::StateChanged, 0.0, StateChangedPayload{PlayerState_Idle, PlayerState_Playing, PlayerEvent::Play}};
    bus.publish(e);
    size_t dispatched = bus.dispatch();

    REQUIRE(dispatched == 1);
    REQUIRE(callCount == 1);
    REQUIRE(receivedType == EventType::StateChanged);
}

TEST_CASE("EventBus publish sets timestamp", "[event-bus]") {
    EventBus bus;
    double capturedTimestamp = 0.0;

    bus.subscribe(EventType::StateChanged, [&](const Event& e) {
        capturedTimestamp = e.timestamp;
    });

    auto before = std::chrono::steady_clock::now();
    Event e{EventType::StateChanged, 0.0, StateChangedPayload{PlayerState_Idle, PlayerState_Idle, PlayerEvent::Open}};
    bus.publish(e);
    auto after = std::chrono::steady_clock::now();

    bus.dispatch();

    double beforeSec = std::chrono::duration<double>(before.time_since_epoch()).count();
    double afterSec = std::chrono::duration<double>(after.time_since_epoch()).count();
    REQUIRE(capturedTimestamp >= beforeSec);
    REQUIRE(capturedTimestamp <= afterSec);
}

TEST_CASE("EventBus unsubscribe stops receiving events", "[event-bus]") {
    EventBus bus;
    int callCount = 0;

    int subId = bus.subscribe(EventType::Error, [&](const Event&) {
        callCount++;
    });

    Event e{EventType::Error, 0.0, ErrorPayload{PlayerError::NetworkError, "test"}};
    bus.publish(e);
    bus.dispatch();
    REQUIRE(callCount == 1);

    bus.unsubscribe(subId);
    bus.publish(e);
    bus.dispatch();
    REQUIRE(callCount == 1);
}

TEST_CASE("EventBus multiple subscribers for same event type", "[event-bus]") {
    EventBus bus;
    int count1 = 0;
    int count2 = 0;
    int count3 = 0;

    bus.subscribe(EventType::BufferLevelChanged, [&](const Event&) { count1++; });
    bus.subscribe(EventType::BufferLevelChanged, [&](const Event&) { count2++; });
    bus.subscribe(EventType::BufferLevelChanged, [&](const Event&) { count3++; });

    Event e{EventType::BufferLevelChanged, 0.0, BufferLevelPayload{75, 2000}};
    bus.publish(e);
    bus.dispatch();

    REQUIRE(count1 == 1);
    REQUIRE(count2 == 1);
    REQUIRE(count3 == 1);
}

TEST_CASE("EventBus variant payload for all event types", "[event-bus]") {
    SECTION("StateChangedPayload") {
        EventBus bus;
        PlayerState capturedOld = PlayerState_End;
        PlayerState capturedNew = PlayerState_End;
        PlayerEvent capturedEvent = PlayerEvent::EndOfStream;

        bus.subscribe(EventType::StateChanged, [&](const Event& e) {
            auto& p = std::get<StateChangedPayload>(e.payload);
            capturedOld = p.oldState;
            capturedNew = p.newState;
            capturedEvent = p.event;
        });

        Event e{EventType::StateChanged, 0.0, StateChangedPayload{PlayerState_Buffering, PlayerState_Playing, PlayerEvent::Play}};
        bus.publish(e);
        bus.dispatch();

        REQUIRE(capturedOld == PlayerState_Buffering);
        REQUIRE(capturedNew == PlayerState_Playing);
        REQUIRE(capturedEvent == PlayerEvent::Play);
    }

    SECTION("ErrorPayload") {
        EventBus bus;
        PlayerError capturedError = PlayerError::None;
        std::string capturedMsg;

        bus.subscribe(EventType::Error, [&](const Event& e) {
            auto& p = std::get<ErrorPayload>(e.payload);
            capturedError = p.error;
            capturedMsg = p.message;
        });

        Event e{EventType::Error, 0.0, ErrorPayload{PlayerError::DecodeError, "codec failure"}};
        bus.publish(e);
        bus.dispatch();

        REQUIRE(capturedError == PlayerError::DecodeError);
        REQUIRE(capturedMsg == "codec failure");
    }

    SECTION("BufferLevelPayload") {
        EventBus bus;
        uint8_t capturedPct = 0;
        uint32_t capturedDur = 0;

        bus.subscribe(EventType::BufferLevelChanged, [&](const Event& e) {
            auto& p = std::get<BufferLevelPayload>(e.payload);
            capturedPct = p.percentage;
            capturedDur = p.durationMs;
        });

        Event e{EventType::BufferLevelChanged, 0.0, BufferLevelPayload{50, 3000}};
        bus.publish(e);
        bus.dispatch();

        REQUIRE(capturedPct == 50);
        REQUIRE(capturedDur == 3000);
    }

    SECTION("LatencyPayload") {
        EventBus bus;
        double capturedLatency = -1.0;

        bus.subscribe(EventType::LatencyMeasured, [&](const Event& e) {
            auto& p = std::get<LatencyPayload>(e.payload);
            capturedLatency = p.latencyMs;
        });

        Event e{EventType::LatencyMeasured, 0.0, LatencyPayload{42.5}};
        bus.publish(e);
        bus.dispatch();

        REQUIRE_THAT(capturedLatency, WithinAbs(42.5, 0.001));
    }

    SECTION("ResolutionPayload") {
        EventBus bus;
        uint32_t capturedW = 0;
        uint32_t capturedH = 0;

        bus.subscribe(EventType::ResolutionChanged, [&](const Event& e) {
            auto& p = std::get<ResolutionPayload>(e.payload);
            capturedW = p.width;
            capturedH = p.height;
        });

        Event e{EventType::ResolutionChanged, 0.0, ResolutionPayload{1920, 1080}};
        bus.publish(e);
        bus.dispatch();

        REQUIRE(capturedW == 1920);
        REQUIRE(capturedH == 1080);
    }
}

TEST_CASE("EventBus dispatch returns count of processed events", "[event-bus]") {
    EventBus bus;
    int received = 0;
    bus.subscribe(EventType::Error, [&](const Event&) { received++; });

    Event e{EventType::Error, 0.0, ErrorPayload{PlayerError::Unknown, ""}};
    bus.publish(e);
    bus.publish(e);
    bus.publish(e);

    REQUIRE(bus.dispatch() == 3);
    REQUIRE(received == 3);
    REQUIRE(bus.dispatch() == 0);
}

TEST_CASE("EventBus only dispatches to matching event type subscribers", "[event-bus]") {
    EventBus bus;
    int stateCount = 0;
    int errorCount = 0;

    bus.subscribe(EventType::StateChanged, [&](const Event&) { stateCount++; });
    bus.subscribe(EventType::Error, [&](const Event&) { errorCount++; });

    Event stateEvent{EventType::StateChanged, 0.0, StateChangedPayload{PlayerState_Idle, PlayerState_Idle, PlayerEvent::Open}};
    Event errorEvent{EventType::Error, 0.0, ErrorPayload{PlayerError::NetworkError, "fail"}};

    bus.publish(stateEvent);
    bus.publish(errorEvent);
    bus.dispatch();

    REQUIRE(stateCount == 1);
    REQUIRE(errorCount == 1);
}

TEST_CASE("EventBus concurrent publish from multiple threads", "[event-bus]") {
    EventBus bus;
    constexpr int threadCount = 8;
    constexpr int eventsPerThread = 100;
    std::atomic<int> totalPublished{0};

    std::vector<std::thread> threads;
    threads.reserve(threadCount);

    for (int i = 0; i < threadCount; ++i) {
        threads.emplace_back([&bus, &totalPublished]() {
            for (int j = 0; j < eventsPerThread; ++j) {
                Event e{EventType::LatencyMeasured, 0.0, LatencyPayload{static_cast<double>(j)}};
                bus.publish(e);
                totalPublished++;
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    int received = 0;
    bus.subscribe(EventType::LatencyMeasured, [&](const Event&) { received++; });

    size_t dispatched = 0;
    size_t batch = 0;
    do {
        batch = bus.dispatch();
        dispatched += batch;
    } while (batch > 0);

    REQUIRE(dispatched == static_cast<size_t>(threadCount * eventsPerThread));
    REQUIRE(received == threadCount * eventsPerThread);
}

TEST_CASE("EventBus full lifecycle: subscribe, publish, unsubscribe, re-subscribe", "[event-bus]") {
    EventBus bus;
    int phase1Count = 0;
    int phase2Count = 0;

    int sub1 = bus.subscribe(EventType::StateChanged, [&](const Event&) { phase1Count++; });
    bus.publish(Event{EventType::StateChanged, 0.0, StateChangedPayload{PlayerState_Idle, PlayerState_Idle, PlayerEvent::Open}});
    bus.dispatch();
    REQUIRE(phase1Count == 1);

    bus.unsubscribe(sub1);

    bus.publish(Event{EventType::StateChanged, 0.0, StateChangedPayload{PlayerState_Idle, PlayerState_Idle, PlayerEvent::Open}});
    bus.dispatch();
    REQUIRE(phase1Count == 1);

    bus.subscribe(EventType::StateChanged, [&](const Event&) { phase2Count++; });
    bus.publish(Event{EventType::StateChanged, 0.0, StateChangedPayload{PlayerState_Idle, PlayerState_Idle, PlayerEvent::Open}});
    bus.dispatch();
    REQUIRE(phase1Count == 1);
    REQUIRE(phase2Count == 1);
}

// ============================================================================
// Local Playback Regression Tests (Task 12)
// ============================================================================

TEST_CASE("PlayerFacade starts in Idle state", "[player][regression][state]") {
    PlayerFacade player;
    REQUIRE(player.getState() == PlayerState_Idle);
}

TEST_CASE("PlayerFacade open with nonexistent path goes to error state (no crash)", "[player][regression][error]") {
    PlayerFacade player;

    auto result = player.open("/nonexistent/file/that/does/not/exist.mp4");

    // Should return an error (file not found or invalid URL)
    REQUIRE(result.hasError());

    // Player should be in Error state, not crashed
    REQUIRE(player.getState() == PlayerState_Error);
}

TEST_CASE("PlayerFacade open with invalid file goes to error state (no crash)", "[player][regression][error]") {
    PlayerFacade player;

    auto result = player.open("invalid://protocol");

    // Should return an error (invalid URL format)
    REQUIRE(result.hasError());

    // Player should be in Error state, not crashed
    REQUIRE(player.getState() == PlayerState_Error);
}

TEST_CASE("PlayerFacade create and destroy without crash", "[player][regression][lifecycle]") {
    REQUIRE_NOTHROW([] {
        PlayerFacade player;
        (void)player.getState();
    }());
}

TEST_CASE("PlayerFacade multiple open/stop cycles without leak or crash", "[player][regression][lifecycle]") {
    PlayerFacade player;

    for (int i = 0; i < 10; ++i) {
        // Open with invalid URL (will fail but shouldn't crash)
        auto result = player.open("/invalid/path/file.mp4");
        (void)result;

        // Stop to reset
        auto stopResult = player.stop();
        (void)stopResult;

        // State should be Idle after stop (recoverable)
        REQUIRE(player.getState() == PlayerState_Idle);
    }

    // If we get here without crash, lifecycle is correct
    REQUIRE(true);
}

TEST_CASE("PlayerFacade stop from Idle is harmless", "[player][regression][lifecycle]") {
    PlayerFacade player;

    // Player starts in Idle
    REQUIRE(player.getState() == PlayerState_Idle);

    // Calling stop() from Idle should be harmless (may return error but shouldn't crash)
    REQUIRE_NOTHROW(player.stop());

    // State should still be Idle
    REQUIRE(player.getState() == PlayerState_Idle);
}

TEST_CASE("EventBus publish/subscribe works for StateChanged events", "[player][regression][eventbus]") {
    EventBus bus;
    bool eventReceived = false;
    PlayerState capturedOldState = PlayerState_Idle;
    PlayerState capturedNewState = PlayerState_Idle;

    int subId = bus.subscribe(EventType::StateChanged, [&](const Event& e) {
        eventReceived = true;
        const auto& payload = std::get<StateChangedPayload>(e.payload);
        capturedOldState = payload.oldState;
        capturedNewState = payload.newState;
    });

    Event e{EventType::StateChanged, 0.0, StateChangedPayload{PlayerState_Idle, PlayerState_Playing, PlayerEvent::Play}};
    bus.publish(e);
    bus.dispatch();

    REQUIRE(eventReceived);
    REQUIRE(capturedOldState == PlayerState_Idle);
    REQUIRE(capturedNewState == PlayerState_Playing);

    bus.unsubscribe(subId);
}

TEST_CASE("EventBus publish/subscribe works for Error events", "[player][regression][eventbus]") {
    EventBus bus;
    bool eventReceived = false;
    PlayerError capturedError = PlayerError::None;
    std::string capturedMessage;

    int subId = bus.subscribe(EventType::Error, [&](const Event& e) {
        eventReceived = true;
        const auto& payload = std::get<ErrorPayload>(e.payload);
        capturedError = payload.error;
        capturedMessage = payload.message;
    });

    Event e{EventType::Error, 0.0, ErrorPayload{PlayerError::DecodeError, "Test error message"}};
    bus.publish(e);
    bus.dispatch();

    REQUIRE(eventReceived);
    REQUIRE(capturedError == PlayerError::DecodeError);
    REQUIRE(capturedMessage == "Test error message");

    bus.unsubscribe(subId);
}

TEST_CASE("QtAudioRenderer can be created and destroyed", "[player][regression][render]") {
    REQUIRE_NOTHROW([] {
        hlplayer::render::QtAudioRenderer renderer;
        (void)renderer;
    }());
}

TEST_CASE("VulkanVideoSink can be created and destroyed", "[player][regression][render]") {
    REQUIRE_NOTHROW([] {
        hlplayer::render::VulkanVideoSink sink;
        (void)sink;
    }());
}
