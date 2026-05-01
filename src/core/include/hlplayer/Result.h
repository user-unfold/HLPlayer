#ifndef HLPLAYER_RESULT_H
#define HLPLAYER_RESULT_H

#include <cstdint>
#include <string>
#include <utility>

#include <hlplayer/Export.h>

namespace hlplayer {

enum class PlayerError : int32_t {
    None = 0,
    InvalidURL,
    NetworkError,
    DecodeError,
    DeviceLost,
    InvalidState,
    UnsupportedFormat,
    Timeout,
    NeedMoreData,
    Unknown = 999
};

template<typename T>
class Result {
public:
    static constexpr Result success(T val) { return Result(std::move(val)); }
    static constexpr Result error(PlayerError err) { return Result(err); }

    constexpr bool hasValue() const noexcept { return !hasError_; }
    constexpr bool hasError() const noexcept { return hasError_; }

    constexpr const T& value() const & noexcept { return value_; }
    constexpr T& value() & noexcept { return value_; }
    constexpr T&& value() && noexcept { return std::move(value_); }

    constexpr PlayerError error() const noexcept { return error_; }

    constexpr T value_or(T defaultVal) const {
        return hasError_ ? std::move(defaultVal) : value_;
    }

private:
    constexpr Result(T val) : value_(std::move(val)), error_(PlayerError::None), hasError_(false) {}
    constexpr Result(PlayerError err) : value_{}, error_(err), hasError_(true) {}

    T value_{};
    PlayerError error_ = PlayerError::None;
    bool hasError_ = false;
};

template<>
class Result<void> {
public:
    static constexpr Result success() { return Result{}; }
    static constexpr Result error(PlayerError err) { return Result{err}; }

    constexpr bool hasValue() const noexcept { return !hasError_; }
    constexpr bool hasError() const noexcept { return hasError_; }
    constexpr PlayerError error() const noexcept { return error_; }

private:
    constexpr Result() : error_(PlayerError::None), hasError_(false) {}
    constexpr Result(PlayerError err) : error_(err), hasError_(true) {}

    PlayerError error_ = PlayerError::None;
    bool hasError_ = false;
};

} // namespace hlplayer

#endif // HLPLAYER_RESULT_H
