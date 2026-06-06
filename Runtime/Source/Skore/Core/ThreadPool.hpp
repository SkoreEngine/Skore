#pragma once

#include <vector>
#include <queue>
#include <condition_variable>
#include <functional>
#include <future>
#include <atomic>
#include <thread>

namespace Skore
{
	class ThreadPool
	{
	public:
		ThreadPool(String name, size_t num_threads = std::thread::hardware_concurrency())
		{
			for (size_t i = 0; i < num_threads; ++i)
			{

				auto t = std::thread([this]
				{
					while (true)
					{
						std::function<void()> task;
						{
							std::unique_lock lock(queue_mutex_);
							cv_.wait(lock, [this]
							{
								return !tasks_.empty() || stop_;
							});

							if (stop_ && tasks_.empty())
							{
								return;
							}
							task = move(tasks_.front());
							tasks_.pop();
						}

						task();
						--size_;
					}
				});
				auto string = fmt::format("{} - {} ", name, i);
				Platform::SetThreadName(t, {string.c_str(), string.size()});
				threads_.emplace_back(std::move(t));
			}
		}

		~ThreadPool()
		{
			{
				std::unique_lock lock(queue_mutex_);
				stop_ = true;
			}
			cv_.notify_all();
			for (std::thread& thread : threads_)
			{
				thread.join();
			}
		}

		void Enqueue(std::function<void()> task)
		{
			{
				std::unique_lock lock(queue_mutex_);
				tasks_.emplace(move(task));
				++size_;
			}
			cv_.notify_one();
		}

		u64 Size()
		{
			return size_;
		}

	private:
		std::vector<std::thread>          threads_;
		std::queue<std::function<void()>> tasks_;
		std::mutex                        queue_mutex_;
		std::condition_variable           cv_;
		std::atomic_uint64_t              size_ = 0;
		bool                              stop_ = false;
	};
}
