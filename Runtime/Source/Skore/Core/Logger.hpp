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

	struct SK_API LogSink
	{
		virtual ~LogSink() = default;
		virtual void SetLevel(LogLevel level) = 0;
		virtual bool CanLog(LogLevel level) = 0;
		virtual void DoLog(LogLevel level, const StringView& logName, const StringView& message) = 0;
	};

	class SK_API Logger
	{
	private:
		Logger(const StringView& name, LogLevel logLevel);
	public:

		void PrintLog(LogLevel level, const StringView& message);
		void SetLevel(LogLevel level);
		void AddSink(LogSink& logSink);
		bool CanLog(LogLevel level);

		template<typename ...Args>
		inline void Log(LogLevel level, const fmt::format_string<Args...>& fmt, Args&& ...args)
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
		static Logger&  GetLogger(const StringView& name, LogLevel logLevel);
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