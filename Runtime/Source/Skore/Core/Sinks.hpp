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

#include <mutex>

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

	struct LogMessage
	{
		LogLevel level;
		String   message;
	};


	class SK_API ConsoleSink : public LogSink
	{
	public:
		void SetLevel(LogLevel level) override;
		bool CanLog(LogLevel level) override;
		void DoLog(LogLevel level, const StringView& logName, const StringView& message) override;
		void GetMessages(Array<LogMessage>& logs);
		void ClearMessages();
		u64  Version() const;
	private:
		LogLevel          m_logLevel{LogLevel::Trace};
		Array<LogMessage> m_logs;
		std::mutex        m_mutex;
		u64               m_version = 0;
	};
}
