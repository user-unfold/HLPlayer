#ifndef HLPLAYER_LOGGER_H
#define HLPLAYER_LOGGER_H

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <memory>
#include <string>
#include <hlplayer/Export.h>

namespace hlplayer {

class HLPLAYER_CORE_API Logger {
public:
    static Logger& instance();
    static void initialize();

    void setLevel(spdlog::level::level_enum level);
    void setPattern(const std::string& pattern);

    std::shared_ptr<spdlog::logger> getLogger();

private:
    Logger() = default;
    ~Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger& operator=(Logger&&) = delete;

    inline static std::shared_ptr<spdlog::logger> logger_;
};

} 

#ifdef TRACE_ENABLED

#define LOG_TRACE(fmt, ...) hlplayer::Logger::instance().getLogger()->trace(fmt, ##__VA_ARGS__)

#else

#define LOG_DEBUG(fmt, ...) hlplayer::Logger::instance().getLogger()->debug(fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) hlplayer::Logger::instance().getLogger()->info(fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) hlplayer::Logger::instance().getLogger()->warn(fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) hlplayer::Logger::instance().getLogger()->error(fmt, ##__VA_ARGS__)

#endif

#endif // HLPLAYER_LOGGER_H
