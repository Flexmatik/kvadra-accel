#pragma once

#include <chrono>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

namespace accel {

class Logger {
public:
    template <typename... Args>
    static void Info(Args&&... args) {
        Write("INFO", std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void Error(Args&&... args) {
        Write("ERROR", std::forward<Args>(args)...);
    }

private:
    template <typename... Args>
    static void Write(const char* level, Args&&... args) {
        std::ostringstream message;
        (message << ... << args);

        const auto kNow = std::chrono::system_clock::now();
        const auto kTime = std::chrono::system_clock::to_time_t(kNow);

        std::lock_guard lock(Mutex());
        std::cerr << std::put_time(std::localtime(&kTime), "%F %T") << " ["
                  << level << "] " << message.str() << '\n';
    }

    static std::mutex& Mutex() {
        static std::mutex mutex;
        return mutex;
    }
};

}  // namespace accel
