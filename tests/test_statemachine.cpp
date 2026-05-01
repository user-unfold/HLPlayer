#include <catch2/catch_test_macros.hpp>
#include <hlplayer/StateMachine.h>

#include <thread>
#include <vector>

using namespace hlplayer;

SCENARIO("StateMachine initial state") {
    GIVEN("A newly created StateMachine") {
        StateMachine sm;

        WHEN("Getting the initial state") {
            THEN("State should be Idle") {
                REQUIRE(sm.getState() == PlayerState_Idle);
            }
        }
    }
}

SCENARIO("StateMachine valid transitions - Idle to ResolvingURL") {
    GIVEN("A StateMachine in Idle state") {
        StateMachine sm;
        REQUIRE(sm.getState() == PlayerState_Idle);

        WHEN("Triggering Open event") {
            auto result = sm.transition(PlayerEvent::Open);

            THEN("Transition should succeed") {
                REQUIRE(result.hasValue());
                REQUIRE(sm.getState() == PlayerState_ResolvingURL);
            }
        }
    }
}

SCENARIO("StateMachine valid transitions - ResolvingURL to Prepared") {
    GIVEN("A StateMachine in ResolvingURL state") {
        StateMachine sm;
        sm.transition(PlayerEvent::Open);
        REQUIRE(sm.getState() == PlayerState_ResolvingURL);

        WHEN("Triggering ResolveSuccess event") {
            auto result = sm.transition(PlayerEvent::ResolveSuccess);

            THEN("Transition should succeed") {
                REQUIRE(result.hasValue());
                REQUIRE(sm.getState() == PlayerState_Prepared);
            }
        }
    }
}

SCENARIO("StateMachine valid transitions - ResolvingURL to Error") {
    GIVEN("A StateMachine in ResolvingURL state") {
        StateMachine sm;
        sm.transition(PlayerEvent::Open);
        REQUIRE(sm.getState() == PlayerState_ResolvingURL);

        WHEN("Triggering ResolveFailure event") {
            auto result = sm.transition(PlayerEvent::ResolveFailure);

            THEN("Transition should succeed") {
                REQUIRE(result.hasValue());
                REQUIRE(sm.getState() == PlayerState_Error);
            }
        }
    }
}

SCENARIO("StateMachine valid transitions - Prepared to Buffering via BufferReady") {
    GIVEN("A StateMachine in Prepared state") {
        StateMachine sm;
        sm.transition(PlayerEvent::Open);
        sm.transition(PlayerEvent::ResolveSuccess);
        REQUIRE(sm.getState() == PlayerState_Prepared);

        WHEN("Triggering BufferReady event") {
            auto result = sm.transition(PlayerEvent::BufferReady);

            THEN("Transition should succeed to Buffering") {
                REQUIRE(result.hasValue());
                REQUIRE(sm.getState() == PlayerState_Buffering);
            }
        }
    }
}

SCENARIO("StateMachine valid transitions - Buffering to Playing via Play") {
    GIVEN("A StateMachine in Buffering state (via BufferReady from Prepared)") {
        StateMachine sm;
        sm.transition(PlayerEvent::Open);
        sm.transition(PlayerEvent::ResolveSuccess);
        sm.transition(PlayerEvent::BufferReady);
        REQUIRE(sm.getState() == PlayerState_Buffering);

        WHEN("Triggering Play event") {
            auto result = sm.transition(PlayerEvent::Play);

            THEN("Transition should succeed to Playing") {
                REQUIRE(result.hasValue());
                REQUIRE(sm.getState() == PlayerState_Playing);
            }
        }
    }
}

SCENARIO("StateMachine valid transitions - Playing to Paused") {
    GIVEN("A StateMachine in Playing state") {
        StateMachine sm;
        sm.transition(PlayerEvent::Open);
        sm.transition(PlayerEvent::ResolveSuccess);
        sm.transition(PlayerEvent::BufferReady);
        sm.transition(PlayerEvent::Play);
        REQUIRE(sm.getState() == PlayerState_Playing);

        WHEN("Triggering Pause event") {
            auto result = sm.transition(PlayerEvent::Pause);

            THEN("Transition should succeed") {
                REQUIRE(result.hasValue());
                REQUIRE(sm.getState() == PlayerState_Paused);
            }
        }
    }
}

SCENARIO("StateMachine valid transitions - Paused to Playing") {
    GIVEN("A StateMachine in Paused state") {
        StateMachine sm;
        sm.transition(PlayerEvent::Open);
        sm.transition(PlayerEvent::ResolveSuccess);
        sm.transition(PlayerEvent::BufferReady);
        sm.transition(PlayerEvent::Play);
        sm.transition(PlayerEvent::Pause);
        REQUIRE(sm.getState() == PlayerState_Paused);

        WHEN("Triggering Resume event") {
            auto result = sm.transition(PlayerEvent::Resume);

            THEN("Transition should succeed") {
                REQUIRE(result.hasValue());
                REQUIRE(sm.getState() == PlayerState_Playing);
            }
        }
    }
}

SCENARIO("StateMachine valid transitions - Playing to Idle via Stop") {
    GIVEN("A StateMachine in Playing state") {
        StateMachine sm;
        sm.transition(PlayerEvent::Open);
        sm.transition(PlayerEvent::ResolveSuccess);
        sm.transition(PlayerEvent::BufferReady);
        sm.transition(PlayerEvent::Play);
        REQUIRE(sm.getState() == PlayerState_Playing);

        WHEN("Triggering Stop event") {
            auto result = sm.transition(PlayerEvent::Stop);

            THEN("Transition should succeed to Idle") {
                REQUIRE(result.hasValue());
                REQUIRE(sm.getState() == PlayerState_Idle);
            }
        }
    }
}

SCENARIO("StateMachine valid transitions - Playing to End via EndOfStream") {
    GIVEN("A StateMachine in Playing state") {
        StateMachine sm;
        sm.transition(PlayerEvent::Open);
        sm.transition(PlayerEvent::ResolveSuccess);
        sm.transition(PlayerEvent::BufferReady);
        sm.transition(PlayerEvent::Play);
        REQUIRE(sm.getState() == PlayerState_Playing);

        WHEN("Triggering EndOfStream event") {
            auto result = sm.transition(PlayerEvent::EndOfStream);

            THEN("Transition should succeed") {
                REQUIRE(result.hasValue());
                REQUIRE(sm.getState() == PlayerState_End);
            }
        }
    }
}

SCENARIO("StateMachine valid transitions - Error to Idle") {
    GIVEN("A StateMachine in Error state") {
        StateMachine sm;
        sm.transition(PlayerEvent::Open);
        sm.transition(PlayerEvent::ResolveFailure);
        REQUIRE(sm.getState() == PlayerState_Error);

        WHEN("Triggering Stop event") {
            auto result = sm.transition(PlayerEvent::Stop);

            THEN("Transition should succeed") {
                REQUIRE(result.hasValue());
                REQUIRE(sm.getState() == PlayerState_Idle);
            }
        }
    }
}

SCENARIO("StateMachine DeviceLost recovery flow") {
    GIVEN("A StateMachine in Playing state") {
        StateMachine sm;
        sm.transition(PlayerEvent::Open);
        sm.transition(PlayerEvent::ResolveSuccess);
        sm.transition(PlayerEvent::BufferReady);
        sm.transition(PlayerEvent::Play);
        REQUIRE(sm.getState() == PlayerState_Playing);

        WHEN("Triggering DeviceLost event") {
            auto result1 = sm.transition(PlayerEvent::DeviceLost);

            THEN("Transition should succeed to DeviceLost") {
                REQUIRE(result1.hasValue());
                REQUIRE(sm.getState() == PlayerState_DeviceLost);
                REQUIRE(sm.isRecoverable());
            }

            AND_WHEN("Triggering RecoveryComplete event") {
                auto result2 = sm.transition(PlayerEvent::RecoveryComplete);

                THEN("Transition should succeed to ResolvingURL") {
                    REQUIRE(result2.hasValue());
                    REQUIRE(sm.getState() == PlayerState_ResolvingURL);
                    REQUIRE(!sm.isRecoverable());
                }
            }
        }
    }
}

SCENARIO("StateMachine DeviceLost from Paused state") {
    GIVEN("A StateMachine in Paused state") {
        StateMachine sm;
        sm.transition(PlayerEvent::Open);
        sm.transition(PlayerEvent::ResolveSuccess);
        sm.transition(PlayerEvent::BufferReady);
        sm.transition(PlayerEvent::Play);
        sm.transition(PlayerEvent::Pause);
        REQUIRE(sm.getState() == PlayerState_Paused);

        WHEN("Triggering DeviceLost event") {
            auto result = sm.transition(PlayerEvent::DeviceLost);

            THEN("Transition should succeed to DeviceLost") {
                REQUIRE(result.hasValue());
                REQUIRE(sm.getState() == PlayerState_DeviceLost);
            }

            AND_WHEN("Triggering RecoveryComplete event") {
                auto result2 = sm.transition(PlayerEvent::RecoveryComplete);

                THEN("Transition should succeed to ResolvingURL") {
                    REQUIRE(result2.hasValue());
                    REQUIRE(sm.getState() == PlayerState_ResolvingURL);
                }
            }
        }
    }
}

SCENARIO("StateMachine invalid transitions") {
    GIVEN("A StateMachine in Idle state") {
        StateMachine sm;
        REQUIRE(sm.getState() == PlayerState_Idle);

        WHEN("Triggering invalid events") {
            THEN("Transitions should fail") {
                REQUIRE_FALSE(sm.transition(PlayerEvent::Play).hasValue());
                REQUIRE_FALSE(sm.transition(PlayerEvent::Pause).hasValue());
                REQUIRE_FALSE(sm.transition(PlayerEvent::Stop).hasValue());
                REQUIRE_FALSE(sm.transition(PlayerEvent::ResolveSuccess).hasValue());
                REQUIRE_FALSE(sm.transition(PlayerEvent::ResolveFailure).hasValue());
                REQUIRE_FALSE(sm.transition(PlayerEvent::BufferReady).hasValue());
                REQUIRE_FALSE(sm.transition(PlayerEvent::Resume).hasValue());
                REQUIRE_FALSE(sm.transition(PlayerEvent::Seek).hasValue());
                REQUIRE_FALSE(sm.transition(PlayerEvent::EndOfStream).hasValue());
                REQUIRE_FALSE(sm.transition(PlayerEvent::RecoveryComplete).hasValue());
                REQUIRE_FALSE(sm.transition(PlayerEvent::DeviceLost).hasValue());

                REQUIRE(sm.getState() == PlayerState_Idle);
            }
        }
    }
}

SCENARIO("StateMachine invalid transitions - Playing to ResolvingURL") {
    GIVEN("A StateMachine in Playing state") {
        StateMachine sm;
        sm.transition(PlayerEvent::Open);
        sm.transition(PlayerEvent::ResolveSuccess);
        sm.transition(PlayerEvent::BufferReady);
        sm.transition(PlayerEvent::Play);
        REQUIRE(sm.getState() == PlayerState_Playing);

        WHEN("Triggering Open event (invalid)") {
            auto result = sm.transition(PlayerEvent::Open);

            THEN("Transition should fail") {
                REQUIRE_FALSE(result.hasValue());
                REQUIRE(result.error() == PlayerError::InvalidState);
                REQUIRE(sm.getState() == PlayerState_Playing);
            }
        }
    }
}

SCENARIO("StateMachine invalid transitions - Idle to Playing") {
    GIVEN("A StateMachine in Idle state") {
        StateMachine sm;

        WHEN("Trying to play without opening a URL") {
            auto result = sm.transition(PlayerEvent::Play);

            THEN("Transition should fail (Idle + Play is not a valid transition)") {
                REQUIRE_FALSE(result.hasValue());
                REQUIRE(result.error() == PlayerError::InvalidState);
                REQUIRE(sm.getState() == PlayerState_Idle);
            }
        }
    }
}

SCENARIO("StateMachine seek transition from Playing is invalid") {
    GIVEN("A StateMachine in Playing state") {
        StateMachine sm;
        sm.transition(PlayerEvent::Open);
        sm.transition(PlayerEvent::ResolveSuccess);
        sm.transition(PlayerEvent::BufferReady);
        sm.transition(PlayerEvent::Play);
        REQUIRE(sm.getState() == PlayerState_Playing);

        WHEN("Triggering Seek event") {
            auto result = sm.transition(PlayerEvent::Seek);

            THEN("Transition should fail (Seek is handled by facade, not state machine)") {
                REQUIRE_FALSE(result.hasValue());
                REQUIRE(result.error() == PlayerError::InvalidState);
                REQUIRE(sm.getState() == PlayerState_Playing);
            }
        }
    }
}

SCENARIO("StateMachine seek transition from Paused is invalid") {
    GIVEN("A StateMachine in Paused state") {
        StateMachine sm;
        sm.transition(PlayerEvent::Open);
        sm.transition(PlayerEvent::ResolveSuccess);
        sm.transition(PlayerEvent::BufferReady);
        sm.transition(PlayerEvent::Play);
        sm.transition(PlayerEvent::Pause);
        REQUIRE(sm.getState() == PlayerState_Paused);

        WHEN("Triggering Seek event") {
            auto result = sm.transition(PlayerEvent::Seek);

            THEN("Transition should fail (Seek is handled by facade, not state machine)") {
                REQUIRE_FALSE(result.hasValue());
                REQUIRE(result.error() == PlayerError::InvalidState);
                REQUIRE(sm.getState() == PlayerState_Paused);
            }
        }
    }
}

SCENARIO("StateMachine reset functionality") {
    GIVEN("A StateMachine in Playing state") {
        StateMachine sm;
        sm.transition(PlayerEvent::Open);
        sm.transition(PlayerEvent::ResolveSuccess);
        sm.transition(PlayerEvent::BufferReady);
        sm.transition(PlayerEvent::Play);
        REQUIRE(sm.getState() == PlayerState_Playing);

        WHEN("Resetting the state machine") {
            sm.reset();

            THEN("State should return to Idle") {
                REQUIRE(sm.getState() == PlayerState_Idle);
            }

            AND_WHEN("Transitioning after reset") {
                auto result = sm.transition(PlayerEvent::Open);

                THEN("Transition should succeed normally") {
                    REQUIRE(result.hasValue());
                    REQUIRE(sm.getState() == PlayerState_ResolvingURL);
                }
            }
        }
    }
}

SCENARIO("StateMachine ErrorOccurred transitions") {
    GIVEN("A StateMachine in various states") {
        WHEN("Error occurs during buffering") {
            StateMachine sm1;
            sm1.transition(PlayerEvent::Open);
            sm1.transition(PlayerEvent::ResolveSuccess);
            sm1.transition(PlayerEvent::BufferReady);
            REQUIRE(sm1.getState() == PlayerState_Buffering);

            auto result = sm1.transition(PlayerEvent::ErrorOccurred);
            REQUIRE(result.hasValue());
            REQUIRE(sm1.getState() == PlayerState_Error);
        }

        WHEN("Error occurs during playing") {
            StateMachine sm2;
            sm2.transition(PlayerEvent::Open);
            sm2.transition(PlayerEvent::ResolveSuccess);
            sm2.transition(PlayerEvent::BufferReady);
            sm2.transition(PlayerEvent::Play);
            REQUIRE(sm2.getState() == PlayerState_Playing);

            auto result = sm2.transition(PlayerEvent::ErrorOccurred);
            REQUIRE(result.hasValue());
            REQUIRE(sm2.getState() == PlayerState_Error);
        }

        WHEN("Error occurs during preparation") {
            StateMachine sm3;
            sm3.transition(PlayerEvent::Open);
            sm3.transition(PlayerEvent::ResolveSuccess);
            REQUIRE(sm3.getState() == PlayerState_Prepared);

            auto result = sm3.transition(PlayerEvent::ErrorOccurred);
            REQUIRE(result.hasValue());
            REQUIRE(sm3.getState() == PlayerState_Error);
        }
    }
}

SCENARIO("StateMachine complete playback flow") {
    GIVEN("A StateMachine starting from Idle") {
        StateMachine sm;

        WHEN("Executing a complete playback cycle") {
            REQUIRE(sm.transition(PlayerEvent::Open).hasValue());
            REQUIRE(sm.getState() == PlayerState_ResolvingURL);

            REQUIRE(sm.transition(PlayerEvent::ResolveSuccess).hasValue());
            REQUIRE(sm.getState() == PlayerState_Prepared);

            REQUIRE(sm.transition(PlayerEvent::BufferReady).hasValue());
            REQUIRE(sm.getState() == PlayerState_Buffering);

            REQUIRE(sm.transition(PlayerEvent::Play).hasValue());
            REQUIRE(sm.getState() == PlayerState_Playing);

            REQUIRE(sm.transition(PlayerEvent::Pause).hasValue());
            REQUIRE(sm.getState() == PlayerState_Paused);

            REQUIRE(sm.transition(PlayerEvent::Resume).hasValue());
            REQUIRE(sm.getState() == PlayerState_Playing);

            REQUIRE(sm.transition(PlayerEvent::EndOfStream).hasValue());
            REQUIRE(sm.getState() == PlayerState_End);
        }
    }
}

SCENARIO("StateMachine thread safety") {
    GIVEN("A StateMachine instance") {
        StateMachine sm;

        WHEN("Transitioning from multiple threads") {
            std::atomic<int> successCount{0};
            std::atomic<int> failCount{0};
            const int numThreads = 4;
            std::vector<std::thread> threads;

            for (int i = 0; i < numThreads; ++i) {
                threads.emplace_back([&]() {
                    for (int j = 0; j < 10; ++j) {
                        auto result = sm.transition(PlayerEvent::Open);
                        if (result.hasValue()) {
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

            THEN("Only one transition should succeed") {
                REQUIRE(successCount == 1);
                REQUIRE(failCount == numThreads * 10 - 1);
                REQUIRE(sm.getState() == PlayerState_ResolvingURL);
            }
        }
    }
}

SCENARIO("StateMachine isRecoverable functionality") {
    GIVEN("A StateMachine in different states") {
        WHEN("In DeviceLost state") {
            StateMachine sm;
            sm.transition(PlayerEvent::Open);
            sm.transition(PlayerEvent::ResolveSuccess);
            sm.transition(PlayerEvent::BufferReady);
            sm.transition(PlayerEvent::Play);
            sm.transition(PlayerEvent::DeviceLost);

            THEN("Should be recoverable") {
                REQUIRE(sm.isRecoverable());
            }
        }

        WHEN("In normal states") {
            StateMachine sm2;
            sm2.transition(PlayerEvent::Open);
            sm2.transition(PlayerEvent::ResolveSuccess);
            sm2.transition(PlayerEvent::BufferReady);
            sm2.transition(PlayerEvent::Play);

            THEN("Should not be recoverable") {
                REQUIRE_FALSE(sm2.isRecoverable());
            }
        }

        WHEN("In Error state") {
            StateMachine sm3;
            sm3.transition(PlayerEvent::Open);
            sm3.transition(PlayerEvent::ResolveFailure);

            THEN("Should be recoverable") {
                REQUIRE(sm3.isRecoverable());
            }
        }
    }
}

SCENARIO("StateMachine Stop from various states") {
    GIVEN("A StateMachine in different states") {
        WHEN("Stopping from Buffering") {
            StateMachine sm1;
            sm1.transition(PlayerEvent::Open);
            sm1.transition(PlayerEvent::ResolveSuccess);
            sm1.transition(PlayerEvent::BufferReady);

            auto result = sm1.transition(PlayerEvent::Stop);
            REQUIRE(result.hasValue());
            REQUIRE(sm1.getState() == PlayerState_Idle);
        }

        WHEN("Stopping from Paused") {
            StateMachine sm2;
            sm2.transition(PlayerEvent::Open);
            sm2.transition(PlayerEvent::ResolveSuccess);
            sm2.transition(PlayerEvent::BufferReady);
            sm2.transition(PlayerEvent::Play);
            sm2.transition(PlayerEvent::Pause);

            auto result = sm2.transition(PlayerEvent::Stop);
            REQUIRE(result.hasValue());
            REQUIRE(sm2.getState() == PlayerState_Idle);
        }

        WHEN("Stopping from DeviceLost") {
            StateMachine sm3;
            sm3.transition(PlayerEvent::Open);
            sm3.transition(PlayerEvent::ResolveSuccess);
            sm3.transition(PlayerEvent::BufferReady);
            sm3.transition(PlayerEvent::Play);
            sm3.transition(PlayerEvent::DeviceLost);

            auto result = sm3.transition(PlayerEvent::Stop);
            REQUIRE(result.hasValue());
            REQUIRE(sm3.getState() == PlayerState_End);
        }
    }
}
