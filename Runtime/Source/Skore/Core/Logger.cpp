#include "Skore/Core/Logger.hpp"
#include "Skore/Core/HashMap.hpp"
#include "Skore/Core/Reflection.hpp"

#include <algorithm>

namespace Skore
{
	namespace
	{
		struct LogContext
		{
			LogContext() = default;
			SK_NO_COPY_CONSTRUCTOR(LogContext);

			HashMap<String, Logger*> loggers{};
			Array<LogSink*>          sinks{};

			LogLevel defaultLevel = LogLevel::Debug;

			~LogContext()
			{
				for (auto& logger : loggers)
				{
					DestroyAndFree(logger.second);
				}
			}
		};

		LogContext& GetContext()
		{
			static LogContext loggers{};
			return loggers;
		}
	}


	Logger::Logger(StringView name, LogLevel logLevel) : m_name(name), m_logLevel(logLevel) {}

	bool Logger::CanLog(LogLevel level)
	{
		if (level < m_logLevel)
		{
			return false;
		}

		for (LogSink* logSink : GetContext().sinks)
		{
			if (logSink->CanLog(level))
			{
				return true;
			}
		}

		//execute log sinks
		for (LogSink* logSink : m_logSinks)
		{
			if (logSink->CanLog(level))
			{
				return true;
			}
		}
		return false;
	}

	void Logger::PrintLog(LogLevel level, StringView message)
	{
		for (LogSink* logSink : GetContext().sinks)
		{
			if (logSink->CanLog(level))
			{
				logSink->DoLog(level, m_name, message);
			}
		}

		//execute log sinks
		for (LogSink* logSink : m_logSinks)
		{
			if (logSink->CanLog(level))
			{
				logSink->DoLog(level, m_name, message);
			}
		}
	}

	void Logger::SetLevel(LogLevel level)
	{
		m_logLevel = level;
	}

	void Logger::AddSink(LogSink& logSink)
	{
		m_logSinks.EmplaceBack(&logSink);
	}

	Logger& Logger::GetLogger(StringView name)
	{
		return GetLogger(name, GetContext().defaultLevel);
	}

	Logger& Logger::GetLogger(StringView name, LogLevel logLevel)
	{
		Allocator* allocator = MemoryGlobals::GetDefaultAllocator();
		HashMap<String, Logger*>& loggers = GetContext().loggers;

		auto it = loggers.Find(name);
		if (it == loggers.end())
		{
			Logger* newLogger = static_cast<Logger*>(allocator->MemAlloc(allocator->allocator, sizeof(Logger)));
			new(newLogger)Logger{name, logLevel};
			it = loggers.Insert(MakePair(String{name}, newLogger)).first;
		}
		return *it->second;
	}

	void Logger::SetDefaultLevel(LogLevel logLevel)
	{
		GetContext().defaultLevel = logLevel;
	}

	void Logger::RegisterSink(LogSink& logSink)
	{
		GetContext().sinks.EmplaceBack(&logSink);
	}

	void Logger::UnregisterSink(LogSink& logSink)
	{
		auto& sinks = GetContext().sinks;
		sinks.Erase(std::find(sinks.begin(), sinks.end(), &logSink), sinks.end());
	}

	void Logger::Reset()
	{
		LogContext& logContext = GetContext();
		logContext.sinks.Clear();
		logContext.sinks.ShrinkToFit();
		logContext.loggers.Clear();
	}

	void Logger::RegisterType(NativeReflectType<Logger>& type)
	{
		type.Function<static_cast<Logger&(*)(StringView)>(&Logger::GetLogger)>("GetLogger", "name");
		type.Function<&Logger::PrintLog>("PrintLog", "level", "message");
	}
}