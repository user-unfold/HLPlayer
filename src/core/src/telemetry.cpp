#include "hlplayer/telemetry.h"

#ifdef TELEMETRY_ENABLED

namespace hlplayer {

OtelTelemetry::OtelTelemetry() {
    auto exporter = std::make_shared<opentelemetry::exporter::trace::OStreamSpanExporter>();
    auto processor = std::make_shared<opentelemetry::sdk::trace::SimpleSpanProcessor>(exporter);
    auto provider = opentelemetry::sdk::trace::TracerProviderFactory::Create(processor);
    opentelemetry::trace::Provider::SetTracerProvider(provider);
    tracer_ = provider->GetTracer("hlplayer", "1.0.0");
}

OtelTelemetry::~OtelTelemetry() = default;

OtelTelemetry::SpanHandle OtelTelemetry::startSpan(const std::string& name, SpanHandle parent) {
    if (parent) {
        return SpanHandle(tracer_->StartSpan(name, {parent}));
    }
    return SpanHandle(tracer_->StartSpan(name));
}

void OtelTelemetry::setAttribute(OtelTelemetry::SpanHandle span, const std::string& key, const std::string& value) {
    if (span) {
        span->SetAttribute(key, value);
    }
}

void OtelTelemetry::setAttribute(OtelTelemetry::SpanHandle span, const std::string& key, long long value) {
    if (span) {
        span->SetAttribute(key, value);
    }
}

void OtelTelemetry::setAttribute(OtelTelemetry::SpanHandle span, const std::string& key, double value) {
    if (span) {
        span->SetAttribute(key, value);
    }
}

void OtelTelemetry::endSpan(OtelTelemetry::SpanHandle span) {
    if (span) {
        span->End();
    }
}

} 

#else

namespace hlplayer {

} 

#endif

namespace hlplayer {

void AtomicTelemetry::incrementCounter(const std::string& name, long long delta) noexcept {
    try {
        std::lock_guard<std::mutex> lock(counters_mutex_);
        auto it = counters_.find(name);
        if (it == counters_.end()) {
            counters_[name] = std::make_shared<std::atomic<long long>>(delta);
        } else {
            it->second->fetch_add(delta, std::memory_order_relaxed);
        }
    } catch (...) {
    }
}

long long AtomicTelemetry::getCounter(const std::string& name) const noexcept {
    try {
        std::lock_guard<std::mutex> lock(counters_mutex_);
        auto it = counters_.find(name);
        if (it != counters_.end()) {
            return it->second->load(std::memory_order_relaxed);
        }
    } catch (...) {
    }
    return 0;
}

void AtomicTelemetry::resetCounter(const std::string& name) noexcept {
    try {
        std::lock_guard<std::mutex> lock(counters_mutex_);
        auto it = counters_.find(name);
        if (it != counters_.end()) {
            it->second->store(0, std::memory_order_relaxed);
        }
    } catch (...) {
    }
}

std::unordered_map<std::string, long long> AtomicTelemetry::getAllCounters() const {
    std::lock_guard<std::mutex> lock(counters_mutex_);
    std::unordered_map<std::string, long long> result;
    for (const auto& [name, counter] : counters_) {
        result[name] = counter->load(std::memory_order_relaxed);
    }
    return result;
}

} 
