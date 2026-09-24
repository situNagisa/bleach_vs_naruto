#pragma once

#include <algorithm>
#include <bit>
#include <concepts>
#include <exception>
#include <functional>
#include <optional>
#include <span>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

#include <stdexec/execution.hpp>

#include "./context.h"

/// 原型的第二层：GPU 域的 sender。
///
/// 核心规则（推演 D2）：**GPU sender 在什么时间完成，由接收者的环境决定。**
///
/// - 环境里有 `get_encoder`：在"录制时"完成。`set_value` 在 CPU 上同步调用，意思是
///   "到这里的命令都录好了"；值是 GPU 句柄（`buffer_ref`），不能在 CPU 上读。
/// - 环境里没有：这个 sender 自己成为**边界**。它持有命令缓冲，把自己以录制模式连到
///   一个带 encoder 的内部接收者上；上游录完后它 end、submit、登记到 reactor，等 GPU
///   真正做完才完成，值换成 CPU 可读的形态（`host_view`）。
///
/// `get_encoder` 是转发查询，所以 `then` / `let_value` 这类转发环境的适配器夹在中间时，
/// 会自然地跟着上游留在录制时；不转发环境的适配器会退化成"GPU 同步完再在 CPU 上继续"，
/// 语义依旧正确。
namespace gpu
{
// ---------------------------------------------------------------------------
// 值：GPU 句柄与它的 CPU 形态

/// 录制时的 buffer 值：句柄 + 上一次访问（推演【发现 11】的"未满足依赖"）。
///
/// 访问状态放在值里，所以值是**线性**的：同一个 buffer 分成两份往下传，两边的状态会分叉。
/// 正式版应当把状态放进 encoder 里的按资源跟踪表，值只带句柄。
struct buffer_ref
{
	::VkBuffer handle = VK_NULL_HANDLE;
	::VkDeviceSize size = 0;
	void* mapped = nullptr;
	::VkPipelineStageFlags2 stage = ::VK_PIPELINE_STAGE_2_NONE;
	::VkAccessFlags2 access = ::VK_ACCESS_2_NONE;
};

[[nodiscard]] inline auto ref(host_buffer const& buffer) noexcept -> buffer_ref
{
	return {.handle = buffer.handle(), .size = buffer.size(), .mapped = buffer.mapped()};
}

/// 边界完成后 CPU 拿到的 buffer：GPU 已经做完，内存可读。
struct host_view
{
	::std::span<::std::byte const> bytes;

	template <class T>
	[[nodiscard]] auto as() const noexcept -> ::std::span<T const>
	{
		return {reinterpret_cast<T const*>(bytes.data()), bytes.size() / sizeof(T)};
	}
};

// ---------------------------------------------------------------------------
// encoder：录制时的命令流

class encoder
{
public:
	explicit encoder(::VkCommandBuffer command) noexcept : _command(command) {}

	[[nodiscard]] auto command() const noexcept -> ::VkCommandBuffer { return _command; }

	/// 声明接下来要以 (stage, access) 访问 `b`：和上一次访问之间插 barrier，返回更新后的值。
	/// 上一次访问为空就不插。读后读也插了一个执行依赖，偏保守，原型不细分。
	auto use(buffer_ref b, ::VkPipelineStageFlags2 stage, ::VkAccessFlags2 access) -> buffer_ref
	{
		using barrier = ::vkfu::param::buffer_memory_barrier2;

		if (b.stage != ::VK_PIPELINE_STAGE_2_NONE)
		{
			// vkfu 的掩码是位域结构体，适合字面量；这里的掩码是运行时跟踪出来的，只能 bit_cast 进去。
			auto const info = ::vkfu::evaluate(barrier{
				.src_stage_mask = ::std::bit_cast<barrier::src_stage_mask_type>(b.stage),
				.src_access_mask = ::std::bit_cast<barrier::src_access_mask_type>(b.access),
				.dst_stage_mask = ::std::bit_cast<barrier::dst_stage_mask_type>(stage),
				.dst_access_mask = ::std::bit_cast<barrier::dst_access_mask_type>(access),
				.src_queue_family_index = VK_QUEUE_FAMILY_IGNORED,
				.dst_queue_family_index = VK_QUEUE_FAMILY_IGNORED,
				.buffer = b.handle,
				.offset = 0,
				.size = VK_WHOLE_SIZE,
			});
			::vkfu::cmd_pipeline_barrier2(_command, ::vkfu::param::dependency{
				.buffer_memory_barriers = ::std::span{&info, 1u},
			});
			++_barriers;
		}
		b.stage = stage;
		b.access = access;
		return b;
	}

	auto fill(buffer_ref dst, ::std::uint32_t value) -> buffer_ref
	{
		dst = use(dst, ::VK_PIPELINE_STAGE_2_CLEAR_BIT, ::VK_ACCESS_2_TRANSFER_WRITE_BIT);
		::vkCmdFillBuffer(_command, dst.handle, 0, VK_WHOLE_SIZE, value);
		return dst;
	}

	/// 只返回 dst；src 的读状态随之丢弃（见 `buffer_ref` 上关于线性的说明）。
	auto copy(buffer_ref src, buffer_ref dst) -> buffer_ref
	{
		src = use(src, ::VK_PIPELINE_STAGE_2_COPY_BIT, ::VK_ACCESS_2_TRANSFER_READ_BIT);
		dst = use(dst, ::VK_PIPELINE_STAGE_2_COPY_BIT, ::VK_ACCESS_2_TRANSFER_WRITE_BIT);
		auto const region = ::VkBufferCopy{.srcOffset = 0, .dstOffset = 0, .size = (::std::min)(src.size, dst.size)};
		::vkCmdCopyBuffer(_command, src.handle, dst.handle, 1, &region);
		return dst;
	}

	[[nodiscard]] auto barriers() const noexcept -> int { return _barriers; }

private:
	::VkCommandBuffer _command;
	int _barriers = 0;
};

/// 边界处把录制时的值换成 CPU 形态，同时录下让 host 可见所需的 barrier。
/// 默认原样传出；`buffer_ref` 换成 `host_view`。
template <class T>
[[nodiscard]] auto finish(encoder&, T&& value) -> ::std::decay_t<T>
{
	return static_cast<T&&>(value);
}

[[nodiscard]] inline auto finish(encoder& e, buffer_ref b) -> host_view
{
	b = e.use(b, ::VK_PIPELINE_STAGE_2_HOST_BIT, ::VK_ACCESS_2_HOST_READ_BIT);
	return {{static_cast<::std::byte const*>(b.mapped), static_cast<::std::size_t>(b.size)}};
}

template <class T>
using host_value_t = decltype(finish(::std::declval<encoder&>(), ::std::declval<T>()));

// ---------------------------------------------------------------------------
// 查询

/// 接收者环境上的查询：当前录制用的 encoder。转发查询，能穿过 `then` 等适配器。
struct get_encoder_t : ::stdexec::forwarding_query_t
{
	// Self 只是为了推迟到实例化再检查：类体内 get_encoder_t 还是不完整类型。
	template <class Env, class Self = get_encoder_t>
		requires requires(Env const& env) { { env.query(Self{}) } -> ::std::same_as<encoder&>; }
	auto operator()(Env const& env) const noexcept -> encoder&
	{
		return env.query(*this);
	}
};
inline constexpr get_encoder_t get_encoder{};

template <class Env>
concept recording_env = ::std::invocable<get_encoder_t, Env const&>;

/// sender 属性上的查询：它属于哪个 context。边界靠它拿队列和 timeline。
struct get_context_t : ::stdexec::forwarding_query_t
{
	template <class Attrs, class Self = get_context_t>
		requires requires(Attrs const& attrs) { { attrs.query(Self{}) } -> ::std::same_as<context*>; }
	auto operator()(Attrs const& attrs) const noexcept -> context*
	{
		return attrs.query(*this);
	}
};
inline constexpr get_context_t get_context{};

struct context_attrs
{
	context* _context;

	[[nodiscard]] auto query(get_context_t) const noexcept -> context* { return _context; }
};

/// 边界给上游的环境：加上 encoder，其余转发外层环境。
template <class Env>
struct encoder_env
{
	encoder* _encoder;
	Env _outer;

	[[nodiscard]] auto query(get_encoder_t) const noexcept -> encoder& { return *_encoder; }

	template <class Query>
		requires (!::std::same_as<Query, get_encoder_t>) && ::std::invocable<Query const&, Env const&>
	[[nodiscard]] auto query(Query const& query) const noexcept(::std::is_nothrow_invocable_v<Query const&, Env const&>)
		-> ::std::invoke_result_t<Query const&, Env const&>
	{
		return query(_outer);
	}
};

// ---------------------------------------------------------------------------
// 完成签名

template <class... Ts>
struct pack {};

template <class... Alternatives>
struct single_value_set
{
	static_assert(sizeof...(Alternatives) == 1, "gpu: a GPU predecessor must complete with exactly one value set");
};

template <class Alternative>
struct single_value_set<Alternative>
{
	using type = Alternative;
};

template <class... Alternatives>
using single_value_set_t = typename single_value_set<Alternatives...>::type;

template <class Values>
struct signatures;

template <class... Vs>
struct signatures<pack<Vs...>>
{
	using recording = ::stdexec::completion_signatures<
		::stdexec::set_value_t(Vs...),
		::stdexec::set_error_t(::std::exception_ptr),
		::stdexec::set_stopped_t()>;
	using boundary = ::stdexec::completion_signatures<
		::stdexec::set_value_t(host_value_t<Vs>...),
		::stdexec::set_error_t(::std::exception_ptr),
		::stdexec::set_stopped_t()>;
};

template <class Sender, class Receiver>
class boundary_operation;

/// 所有 GPU sender 的公共部分：按环境分派完成签名和 connect。
///
/// 派生类提供：
/// - `template <recording_env Env> using recording_values = pack<Vs...>;`
/// - `connect_recording(Receiver)`：环境里有 encoder 时的算子状态；
/// - `get_env()`：带 `get_context` 的属性。
template <class Derived>
struct gpu_sender
{
	using sender_concept = ::stdexec::sender_t;

	template <class Self, class... Env>
	static consteval auto get_completion_signatures()
	{
		if constexpr (sizeof...(Env) == 0)
			return ::stdexec::__throw_dependent_sender_error<Self>();
		else if constexpr ((recording_env<Env> && ...))
			return typename signatures<typename Derived::template recording_values<Env...>>::recording{};
		else
			return typename signatures<typename Derived::template recording_values<encoder_env<Env>...>>::boundary{};
	}

	template <class Self, ::stdexec::receiver Receiver>
	auto connect(this Self&& self, Receiver receiver)
	{
		if constexpr (recording_env<::stdexec::env_of_t<Receiver>>)
			return static_cast<Self&&>(self).connect_recording(::std::move(receiver));
		else
			return boundary_operation<::std::decay_t<Self>, Receiver>{static_cast<Self&&>(self), ::std::move(receiver)};
	}
};

// ---------------------------------------------------------------------------
// 边界

/// 最下游的 GPU 算子：持有命令池与命令缓冲，上游全部录进这一个缓冲（推演【发现 4】）。
///
/// connect 时分配命令池（"connect 即编译任务图"），start 时 begin 再启动上游；上游录完
/// 回到 `_inner_receiver::set_value`，这里录 host barrier、end、submit、登记 reactor。
/// GPU 做完后 reactor 调 `_complete`，才对外 `set_value`。
///
/// 停止只在 submit 之前生效；错误和停止都不会在 GPU 仍持有资源时完成（【发现 9】）。
template <class Sender, class Receiver>
class boundary_operation : waiter, immovable
{
	using env_type = encoder_env<::stdexec::env_of_t<Receiver>>;
	using values_type = typename Sender::template recording_values<env_type>;

	template <class Values>
	struct host_tuple;

	template <class... Vs>
	struct host_tuple<pack<Vs...>>
	{
		using type = ::std::tuple<host_value_t<Vs>...>;
	};

	struct _inner_receiver
	{
		using receiver_concept = ::stdexec::receiver_t;

		boundary_operation* _op;

		template <class... Vs>
		auto set_value(Vs&&... values) noexcept -> void
		{
			_op->_recorded(static_cast<Vs&&>(values)...);
		}

		auto set_error(::std::exception_ptr error) noexcept -> void
		{
			_op->_fail_before_submit(::std::move(error));
		}

		auto set_stopped() noexcept -> void
		{
			_op->_stop_before_submit();
		}

		[[nodiscard]] auto get_env() const noexcept -> env_type
		{
			return {&_op->_encoder, ::stdexec::get_env(_op->_receiver)};
		}
	};

public:
	using operation_state_concept = ::stdexec::operation_state_t;

	boundary_operation(Sender sender, Receiver receiver)
		: waiter{.value = 0, .complete = &_complete}
		, _context(get_context(::stdexec::get_env(sender)))
		, _receiver(::std::move(receiver))
		, _pool(_context->device(), ::vkfu::create_command_pool(_context->device(), ::vkfu::param::command_pool{
			.flags = {.transient = 1}, .queue_family_index = _context->queue_family(),
		}))
		, _command(_allocate(_context->device(), _pool.handle))
		, _encoder(_command.handle)
		, _inner(::stdexec::connect(::std::move(sender), _inner_receiver{this}))
	{
	}

	auto start() & noexcept -> void
	{
		try
		{
			::vkfu::begin_command_buffer(_command.handle, ::vkfu::param::command_buffer_begin{.flags = {.one_time_submit = 1}});
		}
		catch (...)
		{
			::stdexec::set_error(::std::move(_receiver), ::std::current_exception());
			return;
		}
		::stdexec::start(_inner);
	}

private:
	static auto _allocate(::VkDevice device, ::VkCommandPool pool) -> ::vkkl::command_buffer
	{
		auto raw = ::VkCommandBuffer{};
		::vkfu::allocate_command_buffers(device, ::vkfu::param::command_buffer{
			.command_pool = pool, .level = ::vkfu::enums::command_buffer_level::primary, .command_buffer_count = 1,
		}, ::std::span{&raw, 1u});
		return {device, pool, raw};
	}

	template <class... Vs>
	auto _recorded(Vs&&... values) noexcept -> void
	{
		try
		{
			_values.emplace(finish(_encoder, static_cast<Vs&&>(values))...);
			check(::vkEndCommandBuffer(_command.handle), "gpu: end command buffer");
			if (::stdexec::get_stop_token(::stdexec::get_env(_receiver)).stop_requested())
			{
				::stdexec::set_stopped(::std::move(_receiver));
				return;
			}
			_context->submit(_command.handle, *this);
		}
		catch (...)
		{
			// submit 失败意味着没有东西在 GPU 上跑，可以立即报错。
			::stdexec::set_error(::std::move(_receiver), ::std::current_exception());
		}
	}

	auto _fail_before_submit(::std::exception_ptr error) noexcept -> void
	{
		::stdexec::set_error(::std::move(_receiver), ::std::move(error));
	}

	auto _stop_before_submit() noexcept -> void
	{
		::stdexec::set_stopped(::std::move(_receiver));
	}

	static auto _complete(waiter& w, ::VkResult result) noexcept -> void
	{
		auto& self = static_cast<boundary_operation&>(w);
		if (result != ::VK_SUCCESS)
		{
			::stdexec::set_error(::std::move(self._receiver),
				::std::make_exception_ptr(::std::runtime_error{"gpu: waiting for the timeline failed"}));
			return;
		}
		::std::apply([&](auto&... values)
		{
			::stdexec::set_value(::std::move(self._receiver), ::std::move(values)...);
		}, *self._values);
	}

	context* _context;
	Receiver _receiver;
	::vkkl::command_pool _pool;
	::vkkl::command_buffer _command;
	encoder _encoder;
	::std::optional<typename host_tuple<values_type>::type> _values{};
	::stdexec::connect_result_t<Sender, _inner_receiver> _inner;
};

// ---------------------------------------------------------------------------
// schedule(gpu)

class scheduler
{
public:
	explicit scheduler(context& ctx) noexcept : _context(&ctx) {}

	struct sender : gpu_sender<sender>
	{
		template <recording_env Env>
		using recording_values = pack<>;

		template <class Receiver>
		struct operation : immovable
		{
			using operation_state_concept = ::stdexec::operation_state_t;

			Receiver _receiver;

			explicit operation(Receiver receiver) : _receiver(::std::move(receiver)) {}

			/// 录制时：命令流从这里开始，什么也不用录。
			auto start() & noexcept -> void
			{
				::stdexec::set_value(::std::move(_receiver));
			}
		};

		context* _context;

		template <class Receiver>
		auto connect_recording(Receiver receiver) const -> operation<Receiver>
		{
			return operation<Receiver>{::std::move(receiver)};
		}

		[[nodiscard]] auto get_env() const noexcept -> context_attrs
		{
			return {_context};
		}
	};

	[[nodiscard]] auto schedule() const noexcept -> sender
	{
		return sender{{}, _context};
	}

	auto operator==(scheduler const&) const noexcept -> bool = default;

private:
	context* _context;
};

// ---------------------------------------------------------------------------
// record(f)

/// 录制时调用 `f(encoder&, values...)`，返回值作为新的值往下传。
///
/// 没有改写 `then`（推演 D1）：`then` 的签名里没有 encoder，改写它就等于让同一个名字
/// 有两种签名。录命令一律走这里，`then` 保持"前驱完成时执行"。
template <class Predecessor, class Function>
struct record_sender : gpu_sender<record_sender<Predecessor, Function>>
{
	template <class Values>
	struct result_of;

	template <class... Vs>
	struct result_of<pack<Vs...>>
	{
		using type = ::std::invoke_result_t<Function&, encoder&, Vs...>;
		using values = ::std::conditional_t<::std::is_void_v<type>, pack<>, pack<::std::decay_t<type>>>;
	};

	template <recording_env Env>
	using recording_values = typename result_of<
		::stdexec::value_types_of_t<Predecessor, Env, pack, single_value_set_t>>::values;

	template <class Receiver>
	struct operation : immovable
	{
		using operation_state_concept = ::stdexec::operation_state_t;

		struct _predecessor_receiver
		{
			using receiver_concept = ::stdexec::receiver_t;

			operation* _op;

			template <class... Vs>
			auto set_value(Vs&&... values) noexcept -> void
			{
				_op->_invoke(static_cast<Vs&&>(values)...);
			}

			template <class Error>
			auto set_error(Error&& error) noexcept -> void
			{
				if constexpr (::std::same_as<::std::decay_t<Error>, ::std::exception_ptr>)
					::stdexec::set_error(::std::move(_op->_receiver), static_cast<Error&&>(error));
				else
					::stdexec::set_error(::std::move(_op->_receiver), ::std::make_exception_ptr(static_cast<Error&&>(error)));
			}

			auto set_stopped() noexcept -> void
			{
				::stdexec::set_stopped(::std::move(_op->_receiver));
			}

			[[nodiscard]] auto get_env() const noexcept -> ::stdexec::env_of_t<Receiver>
			{
				return ::stdexec::get_env(_op->_receiver);
			}
		};

		Function _function;
		Receiver _receiver;
		::stdexec::connect_result_t<Predecessor, _predecessor_receiver> _predecessor;

		operation(Predecessor predecessor, Function function, Receiver receiver)
			: _function(::std::move(function))
			, _receiver(::std::move(receiver))
			, _predecessor(::stdexec::connect(::std::move(predecessor), _predecessor_receiver{this}))
		{
		}

		auto start() & noexcept -> void
		{
			::stdexec::start(_predecessor);
		}

		template <class... Vs>
		auto _invoke(Vs&&... values) noexcept -> void
		{
			auto& e = get_encoder(::stdexec::get_env(_receiver));
			try
			{
				if constexpr (::std::is_void_v<::std::invoke_result_t<Function&, encoder&, Vs...>>)
				{
					::std::invoke(_function, e, static_cast<Vs&&>(values)...);
					::stdexec::set_value(::std::move(_receiver));
				}
				else
				{
					auto result = ::std::invoke(_function, e, static_cast<Vs&&>(values)...);
					::stdexec::set_value(::std::move(_receiver), ::std::move(result));
				}
			}
			catch (...)
			{
				::stdexec::set_error(::std::move(_receiver), ::std::current_exception());
			}
		}
	};

	Predecessor _predecessor;
	Function _function;

	template <class Receiver>
	auto connect_recording(Receiver receiver) && -> operation<Receiver>
	{
		return operation<Receiver>{::std::move(_predecessor), ::std::move(_function), ::std::move(receiver)};
	}

	[[nodiscard]] auto get_env() const noexcept -> context_attrs
	{
		return {get_context(::stdexec::get_env(_predecessor))};
	}
};

template <class Function>
struct record_closure : ::stdexec::sender_adaptor_closure<record_closure<Function>>
{
	Function _function;

	template <::stdexec::sender Predecessor>
		requires ::std::invocable<get_context_t, ::stdexec::env_of_t<Predecessor>>
	auto operator()(Predecessor&& predecessor) && -> record_sender<::std::decay_t<Predecessor>, Function>
	{
		return {{}, static_cast<Predecessor&&>(predecessor), ::std::move(_function)};
	}
};

/// 前驱必须能回答 `get_context`——也就是说录制链要从 `schedule(gpu)` 起头。
/// CPU 起头（`just(x) | continues_on(gpu) | ...`）原型还没接。
template <class Function>
[[nodiscard]] auto record(Function function) -> record_closure<Function>
{
	return {{}, ::std::move(function)};
}
}
