#include <catch2/catch_test_macros.hpp>
#include <hlplayer/telemetry.h>

TEST_CASE("Telemetry placeholder", "[telemetry]") {
    REQUIRE(true);
}

TEST_CASE("AtomicTelemetry - counter operations", "[telemetry][atomic]") {
    hlplayer::AtomicTelemetry telemetry;

    SECTION("increment counter") {
        telemetry.incrementCounter("test_counter");
        REQUIRE(telemetry.getCounter("test_counter") == 1);
    }

    SECTION("increment with delta - use explicit cast for int64_t literals") {
        telemetry.incrementCounter("delta_counter", 10LL);
        REQUIRE(telemetry.getCounter("delta_counter") == 10LL);
    }

    SECTION("get non-existent counter") {
        REQUIRE(telemetry.getCounter("non_existent") == 0);
    }

    SECTION("reset counter") {
        telemetry.incrementCounter("reset_counter", 5LL);
        telemetry.resetCounter("reset_counter");
        REQUIRE(telemetry.getCounter("reset_counter") == 0LL);
    }

    SECTION("get all counters") {
        telemetry.incrementCounter("counter1", 1LL);
        telemetry.incrementCounter("counter2", 2LL);

        auto counters = telemetry.getAllCounters();
        REQUIRE(counters.size() == 2);
        REQUIRE(counters["counter1"] == 1LL);
        REQUIRE(counters["counter2"] == 2LL);
    }

    SECTION("multiple increments") {
        telemetry.incrementCounter("multi_counter", 1LL);
        telemetry.incrementCounter("multi_counter", 2LL);
        telemetry.incrementCounter("multi_counter", 3LL);
        REQUIRE(telemetry.getCounter("multi_counter") == 6LL);
    }
}
