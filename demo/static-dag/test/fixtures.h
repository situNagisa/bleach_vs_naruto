#pragma once

/// 测试共用的键、测试用 sender / 接收者、模拟定制 when_all 的 domain。

#include <atomic>
#include <exception>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <stdexec/execution.hpp>
#include <exec/sender_for.hpp>

namespace
{

struct a
{
};
struct b
{
};
struct c
{
};
struct d
{
};
struct e
{
};
struct f
{
};

// ---------------------------------------------------------------- 测试用 sender / 接收者

/// 启动时已请求停止 -> set_stopped，否则 set_value()。
struct stop_probe
{
	using sender_concept = ::stdexec::sender_t;

	template<class Self, class... Env>
	static consteval auto get_completion_signatures()
	{
		return ::stdexec::completion_signatures<::stdexec::set_value_t(), ::stdexec::set_stopped_t()>{};
	}

	template<class Receiver>
	struct operation
	{
		Receiver _receiver;

		void start() noexcept
		{
			if (::stdexec::get_stop_token(::stdexec::get_env(_receiver)).stop_requested())
			{
				::stdexec::set_stopped(::std::move(_receiver));
			}
			else
			{
				::stdexec::set_value(::std::move(_receiver));
			}
		}
	};

	template<::stdexec::receiver Receiver>
	operation<Receiver> connect(Receiver receiver) const noexcept
	{
		return {::std::move(receiver)};
	}
};

/// 永远 set_stopped，但声明自己也可能 set_value()。
struct always_stopped
{
	using sender_concept = ::stdexec::sender_t;

	template<class Self, class... Env>
	static consteval auto get_completion_signatures()
	{
		return ::stdexec::completion_signatures<::stdexec::set_value_t(), ::stdexec::set_stopped_t()>{};
	}

	template<class Receiver>
	struct operation
	{
		Receiver _receiver;

		void start() noexcept
		{
			::stdexec::set_stopped(::std::move(_receiver));
		}
	};

	template<::stdexec::receiver Receiver>
	operation<Receiver> connect(Receiver receiver) const noexcept
	{
		return {::std::move(receiver)};
	}
};

/// 移动时按开关抛异常：用来触发"存值时抛异常 -> exception_ptr"路径（开关在 sync_wait 前才打开）。
struct fragile
{
	bool* _explode;

	explicit fragile(bool* explode) noexcept : _explode(explode)
	{
	}

	fragile(fragile&& other) : _explode(other._explode)
	{
		if (*_explode)
		{
			throw ::std::runtime_error("fragile");
		}
	}
};

enum class outcome
{
	none,
	value,
	error,
	stopped,
};

struct stop_env
{
	::stdexec::inplace_stop_token _token;

	[[nodiscard]] ::stdexec::inplace_stop_token query(::stdexec::get_stop_token_t) const noexcept
	{
		return _token;
	}
};

/// 带外层停止令牌的接收者；值只记录个数。
struct probe_receiver
{
	using receiver_concept = ::stdexec::receiver_t;

	outcome* _outcome;
	int* _value_count;
	::stdexec::inplace_stop_token _token;

	template<class... Args>
	void set_value(Args&&...) noexcept
	{
		*_outcome = outcome::value;
		*_value_count = sizeof...(Args);
	}

	template<class Error>
	void set_error(Error&&) noexcept
	{
		*_outcome = outcome::error;
	}

	void set_stopped() noexcept
	{
		*_outcome = outcome::stopped;
	}

	[[nodiscard]] stop_env get_env() const noexcept
	{
		return {_token};
	}
};

/// 完成时顺带唤醒等待线程。
struct notify_receiver
{
	using receiver_concept = ::stdexec::receiver_t;

	probe_receiver _inner;
	::std::atomic<bool>* _done;

	template<class... Args>
	void set_value(Args&&... args) noexcept
	{
		_inner.set_value(::std::forward<Args>(args)...);
		_done->store(true);
		_done->notify_all();
	}

	template<class Error>
	void set_error(Error&& error) noexcept
	{
		_inner.set_error(::std::forward<Error>(error));
		_done->store(true);
		_done->notify_all();
	}

	void set_stopped() noexcept
	{
		_inner.set_stopped();
		_done->store(true);
		_done->notify_all();
	}

	[[nodiscard]] stop_env get_env() const noexcept
	{
		return _inner.get_env();
	}
};

template<class V>
constexpr bool shared_access = ::std::is_const_v<::std::remove_reference_t<V>>;

// ---------------------------------------------------------------- 模拟一个会定制 when_all 的 domain

inline int custom_when_all_uses = 0;

template<class Sender>
struct counted_when_all
{
	Sender _sender;

	auto operator()() const
	{
		++custom_when_all_uses;
		// 内层 when_all 连接时环境里的 domain 换回默认，避免被再次定制。
		return ::stdexec::write_env(_sender, ::stdexec::prop{::stdexec::get_domain, ::stdexec::default_domain{}});
	}
};

/// 只定制 when_all（起始阶段）：把它包进一个计数的 let_value。
struct mock_domain : ::stdexec::default_domain
{
	template<::exec::sender_for<::stdexec::when_all_t> Sender, class Env>
	static auto transform_sender(::stdexec::start_t, Sender&& sender, Env const&)
	{
		return ::stdexec::let_value(::stdexec::just(), counted_when_all<::std::remove_cvref_t<Sender>>{::std::forward<Sender>(sender)});
	}
};

struct domain_env
{
	[[nodiscard]] mock_domain query(::stdexec::get_domain_t) const noexcept
	{
		return {};
	}
};

/// 环境里带 mock_domain 的接收者。
struct domain_receiver
{
	using receiver_concept = ::stdexec::receiver_t;

	outcome* _outcome;
	int* _value_count;

	template<class... Args>
	void set_value(Args&&...) noexcept
	{
		*_outcome = outcome::value;
		*_value_count = sizeof...(Args);
	}

	template<class Error>
	void set_error(Error&&) noexcept
	{
		*_outcome = outcome::error;
	}

	void set_stopped() noexcept
	{
		*_outcome = outcome::stopped;
	}

	[[nodiscard]] domain_env get_env() const noexcept
	{
		return {};
	}
};

}
