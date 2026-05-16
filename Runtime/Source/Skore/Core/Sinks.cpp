#include <iostream>
#include "Skore/Core/Sinks.hpp"

#include <chrono>

#include "Skore/Core/Span.hpp"

#ifdef SK_ANDROID
#include <android/log.h>
#endif

namespace Skore
{
	void StdOutSink::SetLevel(LogLevel level)
	{
		m_logLevel = level;
	}

	bool StdOutSink::CanLog(LogLevel level)
	{
		return m_logLevel < level;
	}

	template <usize Size, typename... Args>
	void FormatStr(BufferString<Size>& buffer, const fmt::format_string<Args...>& fmt, Args&&... args)
	{
		usize size = fmt::formatted_size(fmt, Traits::Forward<Args>(args)...);
		buffer.Resize(size);
		fmt::format_to(buffer.begin(), fmt, Traits::Forward<Args>(args)...);
	}

	template <usize Size>
	void DefaultFormatStr(BufferString<Size>& buffer, LogLevel level, StringView logName, StringView message)
	{
		auto now = std::chrono::system_clock::now();
		auto nowSec = std::chrono::floor<std::chrono::seconds>(now);
		auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - nowSec);
		FormatStr(buffer, "[{:%Y-%m-%d %H:%M:%S}:{:03d}] [{}] [{}] {}", nowSec, ms.count(), levelDesc[static_cast<usize>(level)], logName, message);
	}

	void StdOutSink::DoLog(LogLevel level, StringView logName, StringView message)
	{
		BufferString<256> buffer;
		DefaultFormatStr(buffer, level, logName, message);

#ifdef SK_ANDROID
		i32 prio = ANDROID_LOG_UNKNOWN;
		switch (level)
		{
			case LogLevel::Trace:
				prio = ANDROID_LOG_VERBOSE;
				break;
			case LogLevel::Debug:
				prio = ANDROID_LOG_DEBUG;
				break;
			case LogLevel::Info:
				prio = ANDROID_LOG_INFO;
				break;
			case LogLevel::Warn:
				prio = ANDROID_LOG_WARN;
				break;
			case LogLevel::Error:
				prio = ANDROID_LOG_ERROR;
				break;
			case LogLevel::Critical:
				prio = ANDROID_LOG_FATAL;
				break;
			case LogLevel::Off:
				prio = ANDROID_LOG_SILENT;
				break;
		}
		__android_log_print(prio, logName.CStr(), "%s", buffer.CStr());
#else
		std::cout << std::string_view(buffer.CStr(), buffer.Size()) << std::endl;
#endif
	}

	void ConsoleSink::SetLevel(LogLevel level)
	{
		m_logLevel = level;
	}

	bool ConsoleSink::CanLog(LogLevel level)
	{
		return m_logLevel < level;
	}

	void ConsoleSink::DoLog(LogLevel level, StringView logName, StringView message)
	{
		std::lock_guard lock(m_mutex);
		DefaultFormatStr(m_buffer, level, logName, message);
		String formatted{StringView{m_buffer}};
		m_logs.EmplaceBack(level, formatted);

		String key{message};
		if (auto it = m_collapsedIndex.Find(key); it != m_collapsedIndex.end())
		{
			m_collapsedLogs[it->second].count++;
			m_collapsedLogs[it->second].message = formatted;
		}
		else
		{
			u32 index = static_cast<u32>(m_collapsedLogs.Size());
			m_collapsedIndex[key] = index;
			m_collapsedLogs.EmplaceBack(level, formatted, 1u);
		}

		m_version++;
	}

	void ConsoleSink::GetMessages(Array<LogMessage>& logs)
	{
		std::lock_guard lock(m_mutex);
		logs.Clear();
		logs.Insert(logs.begin(), m_logs.begin(), m_logs.end());
	}

	void ConsoleSink::GetCollapsedMessages(Array<CollapsedLogMessage>& logs)
	{
		std::lock_guard lock(m_mutex);
		logs.Clear();
		logs.Insert(logs.begin(), m_collapsedLogs.begin(), m_collapsedLogs.end());
	}

	void ConsoleSink::ClearMessages()
	{
		std::lock_guard lock(m_mutex);
		m_logs.Clear();
		m_collapsedLogs.Clear();
		m_collapsedIndex.Clear();
		m_version++;
	}

	u64 ConsoleSink::Version() const
	{
		return m_version;
	}
}