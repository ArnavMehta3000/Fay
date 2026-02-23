#pragma once
#include <print>
#include <format>

namespace fay
{
    class Log
    {
        enum class Level { Info, Warn, Error };
        static constexpr auto Reset  = "\033[0m";
        static constexpr auto White  = "\033[37m";
        static constexpr auto Yellow = "\033[33m";
        static constexpr auto Red    = "\033[31m";

    public:
        template <typename... Args>
        static void Info(std::format_string<Args...> fmt, Args&&... args)
        {
            Print(Level::Info, fmt, std::forward<Args>(args)...);
        }

        template <typename... Args>
        static void Warn(std::format_string<Args...> fmt, Args&&... args)
        {
            Print(Level::Warn, fmt, std::forward<Args>(args)...);
        }

        template <typename... Args>
        static void Error(std::format_string<Args...> fmt, Args&&... args)
        {
            Print(Level::Error, fmt, std::forward<Args>(args)...);
        }

    private:
        template <typename... Args>
        static void Print(Level level, std::format_string<Args...> fmt, Args&&... args)
        {
            const char* color = White;
            const char* tag   = "[Info]";

            switch (level)
            {
                case Level::Info:
                    color = White;
                    tag   = "[Info]";
                    break;
                case Level::Warn:
                    color = Yellow;
                    tag   = "[Warn]";
                    break;
                case Level::Error:
                    color = Red;
                    tag   = "[Error]";
                    break;
            }

            std::string msg = std::format(fmt, std::forward<Args>(args)...);
            std::println("{}{} {}{}", color, tag, msg, Reset);
        }
    };
}
