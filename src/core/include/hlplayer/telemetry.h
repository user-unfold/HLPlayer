#ifndef HLPLAYER_TELEMETRY_H
#define HLPLAYER_TELEMETRY_H

#include <atomic>
#include <unordered_map>
#include <string>
#include <memory>
#include <cstdint>
#include <mutex>
#include <map>

#ifndef HLPLAYER_CORE_API
# ifdef _WIN32
#   ifdef HLPLAYER_CORE_EXPORTS
#     define HLPLAYER_CORE_API __declspec(dllexport)
#   else
#     define HLPLAYER_CORE_API __declspec(dllimport)
#   endif
# else
#   define HLPLAYER_CORE_API
# endif
#endif

#ifdef TELEMETRY_ENABLED

#include <opentelemetry/trace/tracer.h>
#include <opentelemetry/trace/provider.h>
#include <opentelemetry/exporters/ostream/span_exporter.h>
#include <opentelemetry/sdk/trace/simple_processor.h>
#include <opentelemetry/sdk/trace/tracer_provider.h>

namespace hlplayer {

namespace nostd = opentelemetry::nostd;
namespace trace = opentelemetry::trace;

class HLPLAYER_CORE_API OtelTelemetry {
public:
    using SpanHandle = std::shared_ptr<trace::Span>;

    OtelTelemetry();
    ~OtelTelemetry();

    SpanHandle startSpan(const std::string& name, SpanHandle parent = nullptr);
    void setAttribute(SpanHandle span, const std::string& key, const std::string& value);
    void setAttribute(SpanHandle span, const std::string& key, long long value);
    void setAttribute(SpanHandle span, const std::string& key, double value);
    void endSpan(SpanHandle span);

private:
    nostd::shared_ptr<trace::Tracer> tracer_;
};

#else

namespace hlplayer {

class HLPLAYER_CORE_API OtelTelemetry {
public:
    struct SpanHandle {};

    OtelTelemetry() = default;
    ~OtelTelemetry() = default;

    SpanHandle startSpan(const std::string&, const SpanHandle& = SpanHandle{}) { return SpanHandle{}; }
    void setAttribute(SpanHandle, const std::string&, const std::string&) {}
    void setAttribute(SpanHandle, const std::string&, long long) {}
    void setAttribute(SpanHandle, const std::string&, double) {}
    void endSpan(SpanHandle) {};
};

class HLPLAYER_CORE_API AtomicTelemetry {
public:
    AtomicTelemetry() = default;
    ~AtomicTelemetry() = default;

    void incrementCounter(const std::string& name, long long delta = 1) noexcept;
    long long getCounter(const std::string& name) const noexcept;
    void resetCounter(const std::string& name) noexcept;
    std::unordered_map<std::string, long long> getAllCounters() const;

private:
    mutable std::mutex counters_mutex_;
    std::unordered_map<std::string, std::shared_ptr<std::atomic<long long>>> counters_;
};

}

#endif

#endif
