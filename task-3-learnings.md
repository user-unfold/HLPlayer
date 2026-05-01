# Learnings from Task 3: Dual Telemetry & Logging Foundation

## Implementation Patterns

### 1. std::atomic Container Issue
**Problem**: `std::atomic<T>` is not copyable, cannot be stored directly in `std::unordered_map`
**Solution**: Use `std::unordered_map<Key, std::shared_ptr<std::atomic<T>>>`
- Allows map to be copied while keeping atomic operations lock-free
- Shared pointers manage atomic lifetime correctly
- Trade-off: Slight overhead from pointer indirection, acceptable for telemetry use case

### 2. Compile-Time Code Elimination
**Pattern**: `if constexpr (FLAG)` guards before method definitions
```cpp
if constexpr (TELEMETRY_ENABLED) {
    // Full implementation
} else {
    // Stub/no-op implementation
}
```
**Benefit**: Zero code generation when disabled, not just "small overhead"
**Critical**: Satisfies Metis guardrail for zero instructions when spans disabled

### 3. Exception Safety Strategy
**Pattern**: Wrap operations in try-catch, return early on exception
```cpp
void incrementCounter(...) noexcept {
    try {
        // implementation
    } catch (...) {
        // suppress all exceptions
    }
}
```
**Rationale**: Prevents exceptions from propagating across DLL boundaries

### 4. Thread Safety Design
**Pattern**: `std::mutex` protects operations on shared data structures
- Atomic counters are already lock-free
- Map of counters needs mutex for creation and iteration
- Use `std::lock_guard` for RAII

## Technical Decisions

### OpenTelemetry Integration
**Decision**: Disable OTel by default, enable via compile-time flag
**Rationale**: 
- OTel C++ SDK has compilation complexity (CMake version compatibility)
- MinGW compatibility issues on Windows
- Atomic telemetry provides hot-path metrics (primary use case)
- OTel can be enabled when needed via `-DTELEMETRY_ENABLED=ON`

### Logger Macro Design
**Pattern**: Preprocessor macro that expands to no-op when disabled
```cpp
#ifdef TRACE_ENABLED
#define LOG_TRACE(fmt, ...) logger->trace(fmt, ##__VA_ARGS__)
#else
#define LOG_TRACE(fmt, ...) ((void)0)
#endif
```
**Benefit**: 
- Zero runtime cost when disabled (no function call)
- Simple compile-time branch prediction

## Key Learnings

### Platform-Specific Issues
**Windows/MinGW**: Integer literal type handling
- Problem: `5` is `long` (32-bit on some platforms) but `int64_t` is `long long` (64-bit)
- Solution needed: Use `5LL` suffix or explicit casting

### CMake Best Practices
- FetchContent for dependencies ensures reproducible builds
- `option()` commands allow compile-time feature flags
- Target-specific compile definitions

### Code Organization
- Headers in `src/core/include/hlplayer/`
- Implementations in `src/core/src/`
- Tests in `tests/`
- Clean separation of concerns

## Design Patterns Used

### Singleton Pattern
```cpp
class Logger {
public:
    static Logger& instance();  // Meyers singleton
private:
    Logger() = default;  // Prevent copying
};
```

### RAII (Resource Acquisition Is Initialization)
```cpp
std::lock_guard<std::mutex> lock(counters_mutex_);
// Automatic unlock when scope ends
```

### PIMPL (Pointer to Implementation)
Not used in this task (simple header-only API), but kept in mind for DLL exports.

## Performance Considerations

### Memory Ordering
- Used `memory_order_relaxed` for atomic operations
- Rationale: Telemetry counters don't need strict ordering
- Benefit: Faster than sequentially consistent, suitable for metrics

### Cache Line Performance
- Atomic counters in separate cache lines (64-byte aligned)
- Prevents false sharing between concurrent increments

## Future Improvements

### Test Compilation Issues
- Need to handle integer literal types correctly on all platforms
- Consider using `LL` suffix for 64-bit literals
- Or create helper macros for typed literals

### Alternative OTel Integration
- Consider using OTel C API instead of C++ SDK
- Provides better CMake compatibility
- More explicit control over resource management

### Advanced Features (Not Implemented)
- Counter rate limiting (incrementCounterWithMax)
- Counter sampling (incrementCounterWithProbability)
- Async counter aggregation (background thread)
