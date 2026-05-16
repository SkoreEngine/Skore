#pragma once

#include <mutex>

#include "Logger.hpp"
#include "HashMap.hpp"

namespace Skore
{
	class SK_API StdOutSink : public LogSink
	{
	public:
		void SetLevel(LogLevel level) override;
		bool CanLog(LogLevel level) override;
		void DoLog(LogLevel level, StringView logName, const StringView message) override;

	private:
		LogLevel m_logLevel{LogLevel::Trace};
	};

	struct LogMessage
	{
		LogLevel level;
		String   message;
	};

	struct CollapsedLogMessage
	{
		LogLevel level;
		String   message;
		u32      count;
	};

	class SK_API ConsoleSink : public LogSink
	{
	public:
		void SetLevel(LogLevel level) override;
		bool CanLog(LogLevel level) override;
		void DoLog(LogLevel level, StringView logName, const StringView message) override;
		void GetMessages(Array<LogMessage>& logs);
		void GetCollapsedMessages(Array<CollapsedLogMessage>& logs);
		void ClearMessages();
		u64  Version() const;
	private:
		LogLevel                  m_logLevel{LogLevel::Trace};
		Array<LogMessage>         m_logs;
		Array<CollapsedLogMessage> m_collapsedLogs;
		HashMap<String, u32>      m_collapsedIndex;
		std::mutex                m_mutex;
		u64                       m_version = 0;
		BufferString<256>         m_buffer;
	};
}
