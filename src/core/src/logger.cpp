#include "hlplayer/logger.h"
#include <iostream>

namespace hlplayer {

Logger& Logger::instance() {
    static Logger instance;
    return instance;
}

void Logger::initialize() {
    if (!logger_) {
        try {
            logger_ = spdlog::stdout_color_mt("hlplayer");
            logger_->set_level(spdlog::level::info);
            logger_->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [thread %t] %v]");
        } catch (const spdlog::spdlog_ex& ex) {
            std::cerr << "Log init failed: " << ex.what() << std::endl;
        }
    }
}

void Logger::setLevel(spdlog::level::level_enum level) {
    if (logger_) {
        logger_->set_level(level);
    }
}

void Logger::setPattern(const std::string& pattern) {
    if (logger_) {
        logger_->set_pattern(pattern);
    }
}

std::shared_ptr<spdlog::logger> Logger::getLogger() {
    if (!logger_) {
        initialize();
    }
    return logger_;
}

}