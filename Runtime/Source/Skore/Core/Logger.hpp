#pragma once

#include "Skore/Common.hpp"
#include "StringView.hpp"
#include "Format.hpp"
#include "String.hpp"
#include "Array.hpp"

namespace Skore
{
	enum class LogLevel
	{
		Trace = 0,
		Debug = 1,
		Info = 2,
		Warn = 3,
		Error = 4,
		Critical = 5,
		Off = 6
	};

	constexpr const char* levelDesc[] = {"Trace", "Debug", "Info", "Warn", "Error", "Critical", "Off"};

	struct SK_API LogSink
	{
		virtual      ~LogSink() = default;
		virtual void SetLevel(LogLevel level) = 0;
		virtual bool CanLog(LogLevel level) = 0;
		virtual void DoLog(LogLevel level, StringView logName, StringView message) = 0;
	};

	class SK_API Logger
	{
	private:
		Logger(StringView name, LogLevel logLevel);
	public:

		void PrintLog(LogLevel level, StringView message);
		void SetLevel(LogLevel level);
		void AddSink(LogSink& logSink);
		bool CanLog(LogLevel level);

		template<typename ...Args>
		void Log(LogLevel level, const fmt::format_string<Args...>& fmt, Args&& ...args)
		{
			if (!CanLog(level))
			{
				return;
			}

			usize size = fmt::formatted_size(fmt, Traits::Forward<Args>(args)...);
			BufferString<128> message(size);
			fmt::format_to(message.begin(), fmt, Traits::Forward<Args>(args)...);
			PrintLog(level, message);
		}

		template<typename ...Args>
		void Trace(const fmt::format_string<Args...>& fmt, Args&& ...args)
		{
			Log(LogLevel::Trace, fmt, Traits::Forward<Args>(args)...);
		}

		template<typename ...Args>
		void Debug(const fmt::format_string<Args...>& fmt, Args&& ...args)
		{
			Log(LogLevel::Debug, fmt, Traits::Forward<Args>(args)...);
		}

		template<typename ...Args>
		void Info(const fmt::format_string<Args...>& fmt, Args&& ...args)
		{
			Log(LogLevel::Info, fmt, Traits::Forward<Args>(args)...);
		}

		template<typename ...Args>
		void Warn(const fmt::format_string<Args...>& fmt, Args&& ...args)
		{
			Log(LogLevel::Warn, fmt, Traits::Forward<Args>(args)...);
		}

		template<typename ...Args>
		void Error(const fmt::format_string<Args...>& fmt, Args&& ...args)
		{
			Log(LogLevel::Error, fmt, Traits::Forward<Args>(args)...);
		}

		template<typename ...Args>
		void Critical(const fmt::format_string<Args...>& fmt, Args&& ...args)
		{
			Log(LogLevel::Critical, fmt, Traits::Forward<Args>(args)...);
		}

		template<typename ...Args>
		void FatalError(const fmt::format_string<Args...>& fmt, Args&& ...args)
		{
			Log(LogLevel::Error, fmt, Traits::Forward<Args>(args)...);
			SK_ASSERT(false, "error");
		}

		static Logger&  GetLogger(StringView name);
		static Logger&  GetLogger(StringView name, LogLevel logLevel);
		static void     RegisterSink(LogSink& logSink);
		static void     UnregisterSink(LogSink& logSink);
		static void     SetDefaultLevel(LogLevel logLevel);
		static void     Reset();


		static void RegisterType(NativeReflectType<Logger>& type);

	private:
		String           m_name{};
		LogLevel         m_logLevel{};
		Array<LogSink*>  m_logSinks{};
	};

}