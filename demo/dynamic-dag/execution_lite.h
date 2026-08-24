#pragma once

/// 极简的 P2300 形状的 sender/receiver 机制，仅为让本 demo 自洽而存在。
///
/// 刻意保持"看得见"：这里没有 CPO、没有 completion_signatures 的类型计算、
/// 没有 domain / transform_sender。全部用鸭子类型的成员函数表达：
///
///     sender.connect(receiver) -> operation_state   （prvalue，不可移动）
///     operation_state.start()  -> void noexcept
///     receiver.set_value() / set_error(exception_ptr) / set_stopped()
///     receiver.get_env().get_stop_token().stop_requested()
///
/// 换成 stdexec 时，`dag.h` 里需要改的只有这几处调用形式。
///
/// 全局约定（整个 demo 依赖它，很重要）：
///   **调用完 receiver 的完成函数之后，绝不允许再触碰对应的 operation_state。**
///   因为完成动作可能让上层（例如 sync_wait）立刻销毁整条 op-state 链。

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <mutex>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace exl
{

// ------------------------------------------------------------------ stop token

class inplace_stop_source;

/// 轮询式 stop token。真实的 P2300 token 支持 stop_callback 注册，
/// 本 demo 只需要"节点自己 poll"和"图在启动节点前 poll"，故省略回调链。
class inplace_stop_token
{
public:
	inplace_stop_token() noexcept = default;

	[[nodiscard]] bool stop_requested() const noexcept;

private:
	friend class inplace_stop_source;

	explicit inplace_stop_token(const inplace_stop_source* source) noexcept
		: source_{source}
	{
	}

	const inplace_stop_source* source_ = nullptr;
};

class inplace_stop_source
{
public:
	inplace_stop_source() noexcept = default;
	inplace_stop_source(const inplace_stop_source&) = delete;
	inplace_stop_source& operator=(const inplace_stop_source&) = delete;

	/// 返回 true 表示本次调用是第一个把它翻成 stopped 的。
	bool request_stop() noexcept
	{
		return !stopped_.exchange(true, std::memory_order_release);
	}

	[[nodiscard]] bool stop_requested() const noexcept
	{
		return stopped_.load(std::memory_order_acquire);
	}

	[[nodiscard]] inplace_stop_token get_token() const noexcept
	{
		return inplace_stop_token{this};
	}

private:
	std::atomic<bool> stopped_{false};
};

inline bool inplace_stop_token::stop_requested() const noexcept
{
	return source_ != nullptr && source_->stop_requested();
}

/// 最小的 env：只回答 get_stop_token。
template<class StopToken>
struct basic_env
{
	StopToken token;

	[[nodiscard]] StopToken get_stop_token() const noexcept
	{
		return token;
	}
};

using env = basic_env<inplace_stop_token>;

// ------------------------------------------------------------------ thread pool

/// 侵入式任务节点，入队不分配。
struct task_base
{
	task_base* next = nullptr;
	void (*execute)(task_base*) noexcept = nullptr;
};

class static_thread_pool
{
public:
	explicit static_thread_pool(unsigned thread_count)
	{
		if (thread_count == 0)
		{
			thread_count = 1;
		}

		workers_.reserve(thread_count);
		for (unsigned i = 0; i < thread_count; ++i)
		{
			workers_.emplace_back([this] { this->worker_loop(); });
		}
	}

	static_thread_pool(const static_thread_pool&) = delete;
	static_thread_pool& operator=(const static_thread_pool&) = delete;

	~static_thread_pool()
	{
		{
			std::lock_guard lock{mutex_};
			shutting_down_ = true;
		}
		condition_.notify_all();

		for (auto& worker : workers_)
		{
			worker.join();
		}
	}

	[[nodiscard]] unsigned thread_count() const noexcept
	{
		return static_cast<unsigned>(workers_.size());
	}

	void enqueue(task_base* task) noexcept
	{
		{
			std::lock_guard lock{mutex_};
			task->next = nullptr;
			if (tail_ != nullptr)
			{
				tail_->next = task;
			}
			else
			{
				head_ = task;
			}
			tail_ = task;
		}
		condition_.notify_one();
	}

	class scheduler;

	[[nodiscard]] scheduler get_scheduler() noexcept;

private:
	void worker_loop() noexcept
	{
		for (;;)
		{
			task_base* task = nullptr;
			{
				std::unique_lock lock{mutex_};
				condition_.wait(lock, [this] { return head_ != nullptr || shutting_down_; });
				if (head_ == nullptr)
				{
					return;
				}
				task = head_;
				head_ = task->next;
				if (head_ == nullptr)
				{
					tail_ = nullptr;
				}
			}

			task->execute(task);
			// 从这里开始 task 可能已被销毁，绝不再触碰。
		}
	}

	std::mutex mutex_;
	std::condition_variable condition_;
	task_base* head_ = nullptr;
	task_base* tail_ = nullptr;
	bool shutting_down_ = false;
	std::vector<std::thread> workers_;
};

class static_thread_pool::scheduler
{
public:
	explicit scheduler(static_thread_pool* pool) noexcept
		: pool_{pool}
	{
	}

	class schedule_sender
	{
	public:
		explicit schedule_sender(static_thread_pool* pool) noexcept
			: pool_{pool}
		{
		}

		template<class Receiver>
		class operation final : public task_base
		{
		public:
			operation(static_thread_pool* pool, Receiver receiver)
				: pool_{pool}
				, receiver_{std::move(receiver)}
			{
				this->execute = &operation::run;
			}

			operation(operation&&) = delete;
			operation& operator=(operation&&) = delete;

			void start() noexcept
			{
				pool_->enqueue(this);
			}

		private:
			static void run(task_base* self) noexcept
			{
				auto* const op = static_cast<operation*>(self);

				// 取消检查放在这里：排队期间被取消的任务不再执行工作体。
				if (op->receiver_.get_env().get_stop_token().stop_requested())
				{
					std::move(op->receiver_).set_stopped();
				}
				else
				{
					std::move(op->receiver_).set_value();
				}
				// 完成之后不再触碰 op。
			}

			static_thread_pool* pool_;
			Receiver receiver_;
		};

		template<class Receiver>
		[[nodiscard]] operation<std::decay_t<Receiver>> connect(Receiver&& receiver) const
		{
			return operation<std::decay_t<Receiver>>(pool_, std::forward<Receiver>(receiver));
		}

	private:
		static_thread_pool* pool_;
	};

	[[nodiscard]] schedule_sender schedule() const noexcept
	{
		return schedule_sender{pool_};
	}

private:
	static_thread_pool* pool_;
};

inline static_thread_pool::scheduler static_thread_pool::get_scheduler() noexcept
{
	return scheduler{this};
}

// ------------------------------------------------------------------ then

template<class Sender, class Function>
class then_sender
{
public:
	then_sender(Sender sender, Function function)
		: sender_{std::move(sender)}
		, function_{std::move(function)}
	{
	}

	template<class Receiver>
	class operation
	{
		struct inner_receiver
		{
			operation* op;

			void set_value() && noexcept
			{
				try
				{
					op->function_();
				}
				catch (...)
				{
					std::move(op->receiver_).set_error(std::current_exception());
					return;
				}
				std::move(op->receiver_).set_value();
			}

			void set_error(std::exception_ptr error) && noexcept
			{
				std::move(op->receiver_).set_error(std::move(error));
			}

			void set_stopped() && noexcept
			{
				std::move(op->receiver_).set_stopped();
			}

			[[nodiscard]] auto get_env() const noexcept
			{
				return op->receiver_.get_env();
			}
		};

		using inner_operation = decltype(std::declval<const Sender&>().connect(std::declval<inner_receiver>()));

	public:
		operation(const Sender& sender, Function function, Receiver receiver)
			: function_{std::move(function)}
			, receiver_{std::move(receiver)}
			, inner_{sender.connect(inner_receiver{this})}
		{
		}

		operation(operation&&) = delete;
		operation& operator=(operation&&) = delete;

		void start() noexcept
		{
			inner_.start();
		}

	private:
		Function function_;
		Receiver receiver_;
		inner_operation inner_;
	};

	template<class Receiver>
	[[nodiscard]] operation<std::decay_t<Receiver>> connect(Receiver&& receiver) const
	{
		return operation<std::decay_t<Receiver>>(sender_, function_, std::forward<Receiver>(receiver));
	}

private:
	Sender sender_;
	Function function_;
};

template<class Sender, class Function>
[[nodiscard]] auto then(Sender sender, Function function)
{
	return then_sender<Sender, Function>{std::move(sender), std::move(function)};
}

// ------------------------------------------------------------------ sync_wait

/// 阻塞等待一个 sender 完成；错误会被重抛。返回 true 表示正常完成，false 表示 stopped。
template<class Sender>
bool sync_wait(const Sender& sender)
{
	struct shared_state
	{
		std::mutex mutex;
		std::condition_variable condition;
		bool done = false;
		bool stopped = false;
		std::exception_ptr error;
		inplace_stop_source stop_source;
	} state;

	struct receiver
	{
		shared_state* state;

		void set_value() && noexcept
		{
			finish();
		}

		void set_error(std::exception_ptr error) && noexcept
		{
			state->error = std::move(error);
			finish();
		}

		void set_stopped() && noexcept
		{
			state->stopped = true;
			finish();
		}

		[[nodiscard]] env get_env() const noexcept
		{
			return env{state->stop_source.get_token()};
		}

	private:
		void finish() const noexcept
		{
			std::lock_guard lock{state->mutex};
			state->done = true;
			state->condition.notify_one();
		}
	};

	auto operation = sender.connect(receiver{&state});
	operation.start();

	{
		std::unique_lock lock{state.mutex};
		state.condition.wait(lock, [&state] { return state.done; });
	}

	if (state.error)
	{
		std::rethrow_exception(state.error);
	}
	return !state.stopped;
}

} // namespace exl
