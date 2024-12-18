#pragma once

#include "Logger.hpp"

namespace Skore
{
    class SK_API StdOutSink : public LogSink
    {
    public:
        void SetLevel(LogLevel level) override;
        bool CanLog(LogLevel level) override;
        void DoLog(LogLevel level, const StringView& logName, const StringView& message) override;
    private:
        LogLevel m_logLevel{LogLevel::Trace};
    };

}