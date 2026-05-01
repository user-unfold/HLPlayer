#include <catch2/catch_test_macros.hpp>
#include <hlplayer/EventBus.h>
#include <hlplayer/StateMachine.h>

#include <atomic>
#include <thread>
#include <vector>

using namespace hlplayer;

SCENARIO("EventBus basic subscription and publishing") {
    GIVEN("An EventBus instance") {
        EventBus bus;

        WHEN("Subscribing to StateChanged events") {
            std::atomic<int> callCount{0};
            PlayerState lastState = PlayerState_Idle;

            int subscriptionId = bus.subscribe(EventType::StateChanged,
                [&](const Event& event) {
                    auto payload = std::get_if<StateChangedPayload>(&event.payload);
                    REQUIRE(payload != nullptr);
                    lastState = payload->newState;
                    callCount++;
                });

            THEN("Subscription ID should be valid") {
                REQUIRE(subscriptionId > 0);
            }

            AND_WHEN("Publishing a StateChanged event") {
                Event event{
                    EventType::StateChanged,
                    0.0,
                    StateChangedPayload{PlayerState_Idle, PlayerState_Playing, PlayerEvent::Play}
                };

                bus.publish(event);

                THEN("Dispatch should process one event") {
                    REQUIRE(bus.dispatch() == 1);
                    REQUIRE(callCount == 1);
                    REQUIRE(lastState == PlayerState_Playing);
                }
            }

            AND_WHEN("Unsubscribing") {
                bus.unsubscribe(subscriptionId);

                AND_WHEN("Publishing another event") {
                    bus.publish(Event{
                        EventType::StateChanged,
                        0.0,
                        StateChangedPayload{PlayerState_Playing, PlayerState_Paused, PlayerEvent::Pause}
                    });

                    THEN("Callback should not be invoked") {
                        REQUIRE(bus.dispatch() == 1);
                        REQUIRE(callCount == 0);
                    }
                }
            }
        }
    }
}

SCENARIO("EventBus with multiple subscribers") {
    GIVEN("An EventBus with multiple subscribers to the same event") {
        EventBus bus;

        std::atomic<int> callCount1{0};
        std::atomic<int> callCount2{0};
        std::atomic<int> callCount3{0};

        bus.subscribe(EventType::Error, [&](const Event&) { callCount1++; });
        bus.subscribe(EventType::Error, [&](const Event&) { callCount2++; });
        bus.subscribe(EventType::Error, [&](const Event&) { callCount3++; });

        WHEN("Publishing an Error event") {
            bus.publish(Event{
                EventType::Error,
                0.0,
                ErrorPayload{PlayerError::NetworkError, "Connection failed"}
            });

            THEN("All subscribers should be notified") {
                bus.dispatch();
                REQUIRE(callCount1 == 1);
                REQUIRE(callCount2 == 1);
                REQUIRE(callCount3 == 1);
            }
        }
    }
}

SCENARIO("EventBus with different event types") {
    GIVEN("An EventBus with subscribers to different event types") {
        EventBus bus;

        std::atomic<int> stateChangedCount{0};
        std::atomic<int> errorCount{0};
        std::atomic<int> bufferLevelCount{0};

        bus.subscribe(EventType::StateChanged, [&](const Event&) { stateChangedCount++; });
        bus.subscribe(EventType::Error, [&](const Event&) { errorCount++; });
        bus.subscribe(EventType::BufferLevelChanged, [&](const Event&) { bufferLevelCount++; });

        WHEN("Publishing multiple events of different types") {
            bus.publish(Event{EventType::StateChanged, 0.0, StateChangedPayload{}});
            bus.publish(Event{EventType::Error, 0.0, ErrorPayload{}});
            bus.publish(Event{EventType::BufferLevelChanged, 0.0, BufferLevelPayload{}});
            bus.publish(Event{EventType::StateChanged, 0.0, StateChangedPayload{}});

            THEN("Only subscribers to matching events should be notified") {
                size_t dispatched = bus.dispatch();
                REQUIRE(dispatched == 4);
                REQUIRE(stateChangedCount == 2);
                REQUIRE(errorCount == 1);
                REQUIRE(bufferLevelCount == 1);
            }
        }
    }
}

SCENARIO("EventBus thread safety") {
    GIVEN("An EventBus instance") {
        EventBus bus;

        std::atomic<int> callCount{0};
        std::atomic<int> publishCount{0};

        int subscriptionId = bus.subscribe(EventType::BufferLevelChanged,
            [&](const Event&) {
                callCount++;
            });

        WHEN("Publishing from multiple threads") {
            const int numThreads = 4;
            const int eventsPerThread = 100;
            std::vector<std::thread> threads;

            for (int i = 0; i < numThreads; ++i) {
                threads.emplace_back([&]() {
                    for (int j = 0; j < eventsPerThread; ++j) {
                        bus.publish(Event{
                            EventType::BufferLevelChanged,
                            0.0,
                            BufferLevelPayload{static_cast<uint8_t>(j % 100), 1000}
                        });
                        publishCount++;
                    }
                });
            }

            for (auto& t : threads) {
                t.join();
            }

            THEN("All events should be queued") {
                REQUIRE(publishCount == numThreads * eventsPerThread);
            }

            AND_WHEN("Dispatching events") {
                size_t dispatched = bus.dispatch();

                THEN("All events should be processed") {
                    REQUIRE(dispatched == static_cast<size_t>(numThreads * eventsPerThread));
                    REQUIRE(callCount == numThreads * eventsPerThread);
                }
            }
        }
    }
}

SCENARIO("EventBus concurrent subscription and unsubscription") {
    GIVEN("An EventBus with initial subscribers") {
        EventBus bus;

        std::atomic<int> activeSubscribers{0};

        auto subscribeCallback = [&](int id) {
            bus.subscribe(EventType::LatencyMeasured,
                [&](const Event&) {
                    activeSubscribers++;
                });
        };

        for (int i = 0; i < 5; ++i) {
            bus.subscribe(EventType::LatencyMeasured, [&](const Event&) { activeSubscribers++; });
        }

        WHEN("Subscribing and unsubscribing concurrently with publishing") {
            const int numOps = 50;
            std::vector<std::thread> threads;
            std::vector<int> subscriptionIds;
            std::mutex idsMutex;

            for (int i = 0; i < 3; ++i) {
                threads.emplace_back([&]() {
                    for (int j = 0; j < numOps; ++j) {
                        int id = bus.subscribe(EventType::ResolutionChanged,
                            [&](const Event&) {});
                        std::lock_guard<std::mutex> lock(idsMutex);
                        subscriptionIds.push_back(id);
                    }
                });
            }

            for (int i = 0; i < 2; ++i) {
                threads.emplace_back([&]() {
                    for (int j = 0; j < numOps; ++j) {
                        if (!subscriptionIds.empty()) {
                            std::lock_guard<std::mutex> lock(idsMutex);
                            if (!subscriptionIds.empty()) {
                                int id = subscriptionIds.back();
                                subscriptionIds.pop_back();
                                bus.unsubscribe(id);
                            }
                        }
                    }
                });
            }

            for (int i = 0; i < 2; ++i) {
                threads.emplace_back([&]() {
                    for (int j = 0; j < numOps; ++j) {
                        bus.publish(Event{EventType::LatencyMeasured, 0.0, LatencyPayload{}});
                    }
                });
            }

            for (auto& t : threads) {
                t.join();
            }

            THEN("Dispatch should complete without deadlock") {
                REQUIRE_NOTHROW(bus.dispatch());
            }
        }
    }
}

SCENARIO("EventBus payload variants") {
    GIVEN("An EventBus with subscriber checking all payload types") {
        EventBus bus;

        bool stateChangedReceived = false;
        bool errorReceived = false;
        bool bufferLevelReceived = false;
        bool latencyReceived = false;
        bool resolutionReceived = false;

        bus.subscribe(EventType::StateChanged, [&](const Event& event) {
            stateChangedReceived = (std::holds_alternative<StateChangedPayload>(event.payload));
        });

        bus.subscribe(EventType::Error, [&](const Event& event) {
            errorReceived = (std::holds_alternative<ErrorPayload>(event.payload));
        });

        bus.subscribe(EventType::BufferLevelChanged, [&](const Event& event) {
            bufferLevelReceived = (std::holds_alternative<BufferLevelPayload>(event.payload));
        });

        bus.subscribe(EventType::LatencyMeasured, [&](const Event& event) {
            latencyReceived = (std::holds_alternative<LatencyPayload>(event.payload));
        });

        bus.subscribe(EventType::ResolutionChanged, [&](const Event& event) {
            resolutionReceived = (std::holds_alternative<ResolutionPayload>(event.payload));
        });

        WHEN("Publishing events with different payload types") {
            bus.publish(Event{EventType::StateChanged, 0.0, StateChangedPayload{}});
            bus.publish(Event{EventType::Error, 0.0, ErrorPayload{}});
            bus.publish(Event{EventType::BufferLevelChanged, 0.0, BufferLevelPayload{}});
            bus.publish(Event{EventType::LatencyMeasured, 0.0, LatencyPayload{}});
            bus.publish(Event{EventType::ResolutionChanged, 0.0, ResolutionPayload{}});

            THEN("All payload types should be correctly identified") {
                bus.dispatch();
                REQUIRE(stateChangedReceived);
                REQUIRE(errorReceived);
                REQUIRE(bufferLevelReceived);
                REQUIRE(latencyReceived);
                REQUIRE(resolutionReceived);
            }
        }
    }
}

SCENARIO("EventBus payload data access") {
    GIVEN("An EventBus with subscriber accessing payload data") {
        EventBus bus;

        PlayerError capturedError = PlayerError::None;
        std::string capturedMessage;
        uint8_t capturedPercentage = 0;
        uint32_t capturedDuration = 0;
        uint32_t capturedWidth = 0;
        uint32_t capturedHeight = 0;

        bus.subscribe(EventType::Error, [&](const Event& event) {
            auto payload = std::get_if<ErrorPayload>(&event.payload);
            REQUIRE(payload != nullptr);
            capturedError = payload->error;
            capturedMessage = payload->message;
        });

        bus.subscribe(EventType::BufferLevelChanged, [&](const Event& event) {
            auto payload = std::get_if<BufferLevelPayload>(&event.payload);
            REQUIRE(payload != nullptr);
            capturedPercentage = payload->percentage;
            capturedDuration = payload->durationMs;
        });

        bus.subscribe(EventType::ResolutionChanged, [&](const Event& event) {
            auto payload = std::get_if<ResolutionPayload>(&event.payload);
            REQUIRE(payload != nullptr);
            capturedWidth = payload->width;
            capturedHeight = payload->height;
        });

        WHEN("Publishing events with specific payload data") {
            bus.publish(Event{
                EventType::Error,
                0.0,
                ErrorPayload{PlayerError::DecodeError, "Frame decode failed"}
            });

            bus.publish(Event{
                EventType::BufferLevelChanged,
                0.0,
                BufferLevelPayload{75, 5000}
            });

            bus.publish(Event{
                EventType::ResolutionChanged,
                0.0,
                ResolutionPayload{1920, 1080}
            });

            THEN("Payload data should be correctly extracted") {
                bus.dispatch();
                REQUIRE(capturedError == PlayerError::DecodeError);
                REQUIRE(capturedMessage == "Frame decode failed");
                REQUIRE(capturedPercentage == 75);
                REQUIRE(capturedDuration == 5000);
                REQUIRE(capturedWidth == 1920);
                REQUIRE(capturedHeight == 1080);
            }
        }
    }
}

SCENARIO("EventBus empty queue behavior") {
    GIVEN("An EventBus with no events in queue") {
        EventBus bus;

        WHEN("Dispatching on empty queue") {
            THEN("Should return 0 events processed") {
                REQUIRE(bus.dispatch() == 0);
            }
        }
    }
}

SCENARIO("EventBus unsubscribe of non-existent subscription") {
    GIVEN("An EventBus instance") {
        EventBus bus;

        WHEN("Unsubscribing a non-existent ID") {
            THEN("Should not throw or crash") {
                REQUIRE_NOTHROW(bus.unsubscribe(999999));
            }
        }
    }
}
