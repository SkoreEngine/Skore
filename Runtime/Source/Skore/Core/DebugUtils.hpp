#pragma once

#include <chrono>
#include <iostream>

#include "Skore/Core/StringView.hpp"
#include "Skore/Core/String.hpp"
#include "Skore/Common.hpp"

namespace Skore
{
	class Logger;
	struct Timer
	{
		f64 startTime{};

		Timer() : startTime(GetTime()) {}

		f64 Get()
		{
			return (GetTime() - startTime);
		}

		void Reset()
		{
			startTime = GetTime();
		}

		void Print() const
		{
			std::cout << "time spent: " << (GetTime() - startTime) << "ms " << std::endl;
		}

		void Print(const char* msg) const
		{
			std::cout << msg << " time spent: " << (GetTime() - startTime) << "ms " << std::endl;
		}

		f64 Diff() const
		{
			return GetTime() - startTime;
		}

		static f64 GetTime()
		{
			return std::chrono::duration<f64, std::milli>(std::chrono::system_clock::now().time_since_epoch()).count();
		}
	};


	struct SK_API ScopedTimer
	{
		Logger& logger;
		String message;
		Timer timer;

		explicit ScopedTimer(Logger& logger, StringView message = "") : logger(logger), message(message) {}

		void Check(StringView message = "") const;

		~ScopedTimer();
	};
}
