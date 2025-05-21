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

#include <iostream>
#include "Sinks.hpp"

#include "Span.hpp"


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

	void StdOutSink::DoLog(LogLevel level, const StringView& logName, const StringView& message)
	{
		std::cout << std::string_view(message.CStr(), message.Size()) << std::endl;
	}

	void ConsoleSink::SetLevel(LogLevel level)
	{
		m_logLevel = level;
	}

	bool ConsoleSink::CanLog(LogLevel level)
	{
		return m_logLevel < level;
	}

	void ConsoleSink::DoLog(LogLevel level, const StringView& logName, const StringView& message)
	{
		std::lock_guard lock(m_mutex);
		m_logs.EmplaceBack(level, message);
		m_version++;
	}

	void ConsoleSink::GetMessages(Array<LogMessage>& logs)
	{
		std::lock_guard lock(m_mutex);
		logs.Clear();
		logs.Insert(logs.begin(), m_logs.begin(), m_logs.end());
	}

	void ConsoleSink::ClearMessages()
	{
		std::lock_guard lock(m_mutex);
		m_logs.Clear();
		m_version++;
	}

	u64 ConsoleSink::Version() const
	{
		return m_version;
	}
}
