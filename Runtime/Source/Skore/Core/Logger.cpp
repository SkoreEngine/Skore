// MIT License
//
// Copyright (c) 2025 Paulo Marangoni (Daethalus)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#include "Logger.hpp"
#include "FormatUtils.hpp"
#include "HashMap.hpp"

#include <algorithm>
#include <chrono>

#include "Reflection.hpp"
#include "StringUtils.hpp"

namespace Skore
{
	namespace
	{
		const char* levelDesc[] = {"Trace", "Debug", "Info", "Warn", "Error", "Critical", "Off"};

		struct LogContext
		{
			HashMap<String, std::shared_ptr<Logger>> loggers{};
			Array<LogSink*>              sinks{};
#ifdef SK_DEBUG
			LogLevel defaultLevel = LogLevel::Debug;
#else
			LogLevel                            defaultLevel = LogLevel::Info;
#endif
		};

		LogContext& GetContext()
		{
			static LogContext loggers{};
			return loggers;
		}
	}


	Logger::Logger(const StringView& name, LogLevel logLevel) : m_name(name), m_logLevel(logLevel) {}

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

	void Logger::PrintLog(LogLevel level, const StringView& message)
	{
		const auto  now = std::chrono::system_clock::now();
		std::time_t t = std::chrono::system_clock::to_time_t(now);
		std::tm     tm{};

#ifdef SK_WIN
		::localtime_s(&tm, &t);
#else
		::localtime_r(&t, &tm);
#endif
		auto duration = now.time_since_epoch();
		i64  milliseconds = (std::chrono::duration_cast<std::chrono::milliseconds>(duration) - duration_cast<std::chrono::milliseconds>(duration_cast<std::chrono::seconds>(duration))).count();

		BufferString<1024> buffer{};

		String strYear = ToString(tm.tm_year + 1900);

		buffer = "[";
		buffer.Append(StringView{strYear});
		buffer.Append("-");
		Pad2(buffer, tm.tm_mon + 1);
		buffer.Append("-");
		Pad2(buffer, tm.tm_mday);
		buffer.Append(" ");
		Pad2(buffer, tm.tm_hour);
		buffer.Append(":");
		Pad2(buffer, tm.tm_min);
		buffer.Append(":");
		Pad2(buffer, tm.tm_sec);
		buffer.Append(":");
		Pad3(buffer, milliseconds);
		buffer += "] [";
		buffer += levelDesc[static_cast<usize>(level)];
		buffer += "] [";
		buffer += StringView(m_name);
		buffer += "] ";
		buffer += message;

		for (LogSink* logSink : GetContext().sinks)
		{
			if (logSink->CanLog(level))
			{
				logSink->DoLog(level, m_name, buffer);
			}
		}

		//execute log sinks
		for (LogSink* logSink : m_logSinks)
		{
			if (logSink->CanLog(level))
			{
				logSink->DoLog(level, m_name, buffer);
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

	Logger& Logger::GetLogger(const StringView& name, LogLevel logLevel)
	{
		Allocator* allocator = MemoryGlobals::GetDefaultAllocator();

		HashMap<String, std::shared_ptr<Logger>>& loggers = GetContext().loggers;
		auto                                it = loggers.Find(name);
		if (it == loggers.end())
		{
			Logger* newLogger = static_cast<Logger*>(allocator->MemAlloc(allocator->allocator, sizeof(Logger)));
			new(newLogger)Logger{name, logLevel};
			it = loggers.Insert(MakePair(String{name}, std::shared_ptr<Logger>(newLogger))).first;
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
