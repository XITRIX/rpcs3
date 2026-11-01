#pragma once

#include "Utilities/Thread.h"
#include "AOTError.h"
#include "IOSMemoryPressurePolicy.h"
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>

namespace rpcs3::ios::aot
{
	// Retry an oversized native module through smaller complete units. A single
	// oversized function keeps a null descriptor and executes in the interpreter.
	// The emitter owns cleanup, progress and the actual file-size check.
	template <typename Emit>
	void prepare_partition(std::size_t first, std::size_t count, Emit& emit)
	{
		if (!count)
			throw failure{"Empty AOT module partition"};
		if (emit(first, count, false))
			return;
		if (count == 1)
		{
			if (!emit(first, count, true))
				throw failure{"AOT descriptor exceeds the signed module size limit"};
			return;
		}
		const auto half = count / 2;
		prepare_partition(first, half, emit);
		prepare_partition(first + half, count - half, emit);
	}

	// Only independent LLVM contexts/objects run concurrently. The native linker
	// retains its process-wide serialization, and the caller owns publication.
	template <typename Work>
	void preparation_tasks(std::size_t count, unsigned workers, Work work)
	{
		if (!count)
			return;
		workers = static_cast<unsigned>(std::min<std::size_t>(count, std::clamp(workers, 1u, automatic_llvm_compile_threads)));
		std::atomic<std::size_t> next{0};
		std::atomic<bool> failed{false};
		std::exception_ptr error;
		std::mutex error_mutex;
		const auto run = [&]
		{
			while (!failed.load(std::memory_order_acquire))
			{
				const auto index = next.fetch_add(1, std::memory_order_relaxed);
				if (index >= count)
					return;
				try
				{
					work(index);
				}
				catch (...)
				{
					std::lock_guard lock(error_mutex);
					if (!error)
						error = std::current_exception();
					failed.store(true, std::memory_order_release);
				}
			}
		};
		// Reuse the native compiler workers, including their 8 MiB Apple stacks.
		// A Swift/dispatch caller's small stack cannot safely run LLVM optimization.
		named_thread_group threads("AOT Compiler ", workers, std::move(run));
		threads.join();
		if (error)
			std::rethrow_exception(error);
	}

	// Compile independent objects while one native worker links/signs completed
	// objects. Bound the queue; the caller must release object buffers before
	// enqueueing. All workers join before publication or error propagation.
	template <typename Produce, typename Consume>
	void preparation_pipeline(std::size_t count, unsigned workers, Produce produce, Consume consume)
	{
		if (!count)
			return;
		std::mutex mutex;
		std::condition_variable changed;
		std::deque<std::size_t> ready;
		bool finished = false;
		std::exception_ptr error;
		const auto fail = [&]
		{
			std::lock_guard lock(mutex);
			if (!error)
				error = std::current_exception();
			changed.notify_all();
		};
		named_thread_group signer("AOT Linker ", 1, [&]
			{
				try
				{
					for (;;)
					{
						std::unique_lock lock(mutex);
						changed.wait(lock, [&]
							{
								return error || finished || !ready.empty();
							});
						if (error || ready.empty())
							return;
						const auto index = ready.front();
						ready.pop_front();
						changed.notify_all();
						lock.unlock();
						consume(index);
					}
				}
				catch (...)
				{
					fail();
				}
			});
		try
		{
			preparation_tasks(count, workers, [&](std::size_t index)
				{
					{
						std::lock_guard lock(mutex);
						if (error)
							return;
					}
					produce(index);
					std::unique_lock lock(mutex);
					changed.wait(lock, [&]
						{
							return error || ready.size() < 2;
						});
					if (error)
						return;
					ready.push_back(index);
					changed.notify_all();
				});
		}
		catch (...)
		{
			fail();
		}
		{
			std::lock_guard lock(mutex);
			finished = true;
			changed.notify_all();
		}
		signer.join();
		if (error)
			std::rethrow_exception(error);
	}
} // namespace rpcs3::ios::aot
