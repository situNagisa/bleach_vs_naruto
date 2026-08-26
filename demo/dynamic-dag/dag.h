#pragma once

/// 动态 DAG 的执行内核（基于 stdexec / P2300）。
///
/// 设计要点（对应设计讨论里的结论）：
///
///  1. 图这一层**不**用现成组合子拼。没有 split、没有 when_all。
///     原因：`split` 本质是"跑一次 + 广播"的闩锁，`when_all` 本质是"倒数计数器"。
///     在纯控制依赖、且节点已被扁平化成数组之后，两者合起来退化成一句话：
///     "每个节点一个前驱计数器，减到 0 就启动"。用组合子拼只会为每个节点
///     多付 3~4 次堆分配和一圈引用计数。
///
///  2. 图这一层本身是一个**自定义 sender 算法**。`graph::run()` 返回合规 sender，
///     可以被 connect / sync_wait / when_all / 外层取消。
///     结构化并发在"整张图"这个边界上被恢复：graph_op 拥有一切。
///
///  3. 所有权的洞只打一个。树形所有权被 DAG 的菱形破坏无法避免，这里的选择是
///     把所有权整体上提到图这一层（arena + 索引），而不是像 split 那样在每个
///     共享点用引用计数各打一个洞。
///
///  4. 依赖是**控制依赖**。边不携带值，所以边的存储是 0 字节；节点产出的数据
///     走外部 context（见 main.cpp 的 frame_context）。
///
/// 每次执行的开销：
///   * 堆分配：首次 3 次（op arena / counters / poisoned），之后由 graph 内的
///     storage 池复用，稳态为 0。
///   * 原子操作：每条边 1 次 fetch_sub，每个节点 1 次 outstanding fetch_sub。
///   * 间接调用：每个节点 1 次（vtable 的 start）。
///   * 引用计数 / std::function / shared_ptr：0。

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <stdexec/execution.hpp>

namespace dag
{

using node_index = std::uint32_t;

inline constexpr node_index invalid_node = ~node_index{0};

class graph;
class graph_run;

// ------------------------------------------------------------------ 节点接收器

/// 节点看到的 env：只暴露 stop token。
/// 图内部失败触发的取消，和外层 receiver 传进来的取消，已经在 graph_run 里
/// 合并进同一个 inplace_stop_source，所以这里只需要一个 token。
struct node_env
{
	::stdexec::inplace_stop_token token;

	[[nodiscard]] ::stdexec::inplace_stop_token query(::stdexec::get_stop_token_t) const noexcept
	{
		return token;
	}
};

/// 所有节点共用的**固定类型**接收器。
///
/// 正因为它是固定类型，`connect_result_t<const Sender&, node_receiver>` 在
/// `add_node<Sender>` 处就可求值 —— 这是整个零分配方案的支点。
///
/// set_value 吞掉一切值：依赖是控制依赖，节点的返回值对图不可见。
class node_receiver
{
public:
	using receiver_concept = ::stdexec::receiver_t;

	node_receiver(graph_run* run, node_index index) noexcept
		: run_{run}
		, index_{index}
	{
	}

	template<class... Values>
	void set_value(Values&&...) && noexcept;

	template<class Error>
	void set_error(Error&& error) && noexcept;

	void set_stopped() && noexcept;

	[[nodiscard]] node_env get_env() const noexcept;

private:
	graph_run* run_;
	node_index index_;
};

// ------------------------------------------------------------------ 类型擦除

namespace detail
{

template<class Sender>
using node_operation_t = ::stdexec::connect_result_t<const Sender&, node_receiver>;

/// 每个节点 sender 类型一份，静态存储期，不产生任何分配。
struct node_vtable
{
	void (*destroy_sender)(void* sender) noexcept;
	void (*connect)(void* operation_storage, const void* sender, graph_run* run, node_index index);
	void (*start)(void* operation_storage) noexcept;
	void (*destroy_operation)(void* operation_storage) noexcept;
};

template<class Sender>
struct node_vtable_for
{
	using operation_type = node_operation_t<Sender>;

	static void destroy_sender(void* sender) noexcept
	{
		static_cast<Sender*>(sender)->~Sender();
	}

	static void connect(void* operation_storage, const void* sender, graph_run* run, node_index index)
	{
		// connect 返回 prvalue，直接就地构造 —— op-state 因此可以是不可移动的（P2300 要求）。
		::new (operation_storage) operation_type(
			::stdexec::connect(*static_cast<const Sender*>(sender), node_receiver{run, index}));
	}

	static void start(void* operation_storage) noexcept
	{
		::stdexec::start(*static_cast<operation_type*>(operation_storage));
	}

	static void destroy_operation(void* operation_storage) noexcept
	{
		static_cast<operation_type*>(operation_storage)->~operation_type();
	}

	static constexpr node_vtable value{&destroy_sender, &connect, &start, &destroy_operation};
};

/// 构图期用的分块 bump 分配器。分块是为了让已放进去的 sender 永不移动。
class chunk_arena
{
public:
	chunk_arena() = default;
	chunk_arena(const chunk_arena&) = delete;
	chunk_arena& operator=(const chunk_arena&) = delete;

	~chunk_arena()
	{
		for (auto& entry : chunks_)
		{
			delete[] entry.base;
		}
	}

	[[nodiscard]] void* allocate(std::size_t size, std::size_t alignment)
	{
		if (!chunks_.empty())
		{
			chunk& back = chunks_.back();
			void* cursor = back.base + back.used;
			std::size_t space = back.capacity - back.used;
			if (std::align(alignment, size, cursor, space) != nullptr)
			{
				back.used = static_cast<std::size_t>(static_cast<std::byte*>(cursor) - back.base) + size;
				return cursor;
			}
		}

		const std::size_t capacity = (size + alignment > default_chunk_size) ? (size + alignment) : default_chunk_size;
		chunks_.push_back(chunk{new std::byte[capacity], 0, capacity});

		chunk& back = chunks_.back();
		void* cursor = back.base;
		std::size_t space = back.capacity;
		void* const result = std::align(alignment, size, cursor, space);
		assert(result != nullptr);
		back.used = static_cast<std::size_t>(static_cast<std::byte*>(result) - back.base) + size;
		return result;
	}

private:
	static constexpr std::size_t default_chunk_size = 16 * 1024;

	struct chunk
	{
		std::byte* base;
		std::size_t used;
		std::size_t capacity;
	};

	std::vector<chunk> chunks_;
};

} // namespace detail

// ------------------------------------------------------------------ 观测

enum class node_event : std::uint8_t
{
	started,
	completed,
	failed,
	/// 前驱失败或整图已取消，节点**从未启动**。
	skipped,
	/// 节点已经启动，在执行中收到取消（stop token），以 set_stopped 结束。
	cancelled,
};

/// 函数指针而非 std::function：关掉时零开销，开着时一次间接调用。
using tracer_fn = void (*)(void* user, node_index index, node_event event) noexcept;

// ------------------------------------------------------------------ 执行期存储

/// 一次执行所需的可复用存储。由 graph 内部的池管理，跨次执行复用 → 稳态 0 分配。
struct run_storage
{
	std::unique_ptr<std::byte[]> operation_block;
	std::byte* operation_base = nullptr;
	std::unique_ptr<std::atomic<std::uint32_t>[]> counters;
	std::unique_ptr<std::atomic<bool>[]> poisoned;
};

// ------------------------------------------------------------------ graph_run

/// graph_op<Receiver> 的类型无关基类。所有节点的完成回调都打到这里。
class graph_run
{
public:
	graph_run(const graph_run&) = delete;
	graph_run& operator=(const graph_run&) = delete;

	[[nodiscard]] ::stdexec::inplace_stop_token get_stop_token() const noexcept
	{
		return stop_source_.get_token();
	}

	[[nodiscard]] bool stop_requested() const noexcept
	{
		return stop_source_.stop_requested();
	}

	void request_stop() noexcept
	{
		stop_source_.request_stop();
	}

	void on_node_done(node_index index) noexcept;
	void on_node_error(node_index index, std::exception_ptr error) noexcept;
	void on_node_stopped(node_index index) noexcept;

protected:
	explicit graph_run(const graph& owner);
	~graph_run();

	void launch() noexcept;

	/// 由 graph_op<Receiver> 实现。**调用之后绝不再触碰 this。**
	virtual void complete(std::exception_ptr error, bool stopped) noexcept = 0;

private:
	// 蹦床：防止"内联完成"导致 O(图深度) 的栈递归。
	// 若节点在 start() 内部就完成（inline scheduler、结果已就绪、或被跳过），
	// on_node_done 会再次调用 start_node，链条足够长就会打穿栈。
	struct trampoline_state
	{
		std::vector<std::pair<graph_run*, node_index>> pending;
		bool active = false;
	};

	static trampoline_state& trampoline() noexcept
	{
		static thread_local trampoline_state state;
		return state;
	}

	void schedule_start(node_index index) noexcept;
	void start_node(node_index index) noexcept;
	void retire(node_index index, bool poison_successors) noexcept;
	void record_error(std::exception_ptr error) noexcept;
	void finish() noexcept;
	void trace(node_index index, node_event event) const noexcept;

	const graph* owner_;
	run_storage storage_;
	std::uint32_t node_count_ = 0;
	std::uint32_t constructed_ = 0;

	std::atomic<std::uint32_t> outstanding_{0};
	::stdexec::inplace_stop_source stop_source_;
	std::atomic<bool> stopped_{false};
	std::atomic<bool> error_claimed_{false};
	std::atomic<bool> error_ready_{false};
	std::exception_ptr error_;
};

template<class... Values>
void node_receiver::set_value(Values&&...) && noexcept
{
	run_->on_node_done(index_);
	// 之后绝不触碰 *this —— 它就住在刚刚可能已完成的 op-state 里。
}

template<class Error>
void node_receiver::set_error(Error&& error) && noexcept
{
	if constexpr (std::is_same_v<std::decay_t<Error>, std::exception_ptr>)
	{
		run_->on_node_error(index_, std::forward<Error>(error));
	}
	else
	{
		run_->on_node_error(index_, std::make_exception_ptr(std::forward<Error>(error)));
	}
}

inline void node_receiver::set_stopped() && noexcept
{
	run_->on_node_stopped(index_);
}

inline node_env node_receiver::get_env() const noexcept
{
	return node_env{run_->get_stop_token()};
}

// ------------------------------------------------------------------ 图

template<class Receiver>
class graph_op;

class graph_sender;

class graph
{
public:
	graph() = default;
	graph(const graph&) = delete;
	graph& operator=(const graph&) = delete;

	~graph()
	{
		for (const node_desc& node : nodes_)
		{
			node.vtable->destroy_sender(node.sender);
		}
	}

	/// 加入一个节点。
	///
	/// preds 必须全部是**已经加入过**的节点 —— 于是图按构造即无环，节点索引本身
	/// 就是一个拓扑序，finalize() 不需要跑拓扑排序，也不需要环检测。
	template<class Sender>
	node_index add_node(std::string name, Sender sender, std::span<const node_index> predecessors)
	{
		static_assert(std::is_copy_constructible_v<Sender>,
			"节点 sender 必须可复制：构图一次、执行多次需要从同一份 sender 反复 connect");

		assert(!finalized_ && "finalize() 之后不能再加节点");

		using operation_type = detail::node_operation_t<Sender>;

		const node_index index = static_cast<node_index>(nodes_.size());

		void* const sender_storage = sender_arena_.allocate(sizeof(Sender), alignof(Sender));
		::new (sender_storage) Sender(std::move(sender));

		// 支点：Sender 在这里还是完整类型，op-state 的尺寸/对齐立刻可知。
		if (operation_alignment_ < alignof(operation_type))
		{
			operation_alignment_ = alignof(operation_type);
		}
		operation_arena_size_ = (operation_arena_size_ + alignof(operation_type) - 1) & ~(alignof(operation_type) - 1);

		node_desc desc;
		desc.vtable = &detail::node_vtable_for<Sender>::value;
		desc.sender = sender_storage;
		desc.operation_offset = static_cast<std::uint32_t>(operation_arena_size_);
		desc.predecessor_count = static_cast<std::uint32_t>(predecessors.size());
		desc.name = std::move(name);
		nodes_.push_back(std::move(desc));

		operation_arena_size_ += sizeof(operation_type);

		for (const node_index predecessor : predecessors)
		{
			assert(predecessor < index && "前驱必须先于本节点加入（保证无环）");
			edges_.push_back(edge{predecessor, index});
		}

		return index;
	}

	template<class Sender>
	node_index add_node(std::string name, Sender sender, std::initializer_list<node_index> predecessors)
	{
		return add_node(std::move(name), std::move(sender),
			std::span<const node_index>{predecessors.begin(), predecessors.size()});
	}

	template<class Sender>
	node_index add_node(std::string name, Sender sender)
	{
		return add_node(std::move(name), std::move(sender), std::span<const node_index>{});
	}

	/// 构图期的分析 pass：反转边建 CSR、收集根、算深度。
	/// 真正的引擎还会在这里做死节点消除、链熔合、关键路径优先级、op-state 存储别名。
	void finalize()
	{
		const std::size_t count = nodes_.size();

		successor_offsets_.assign(count + 1, 0);
		for (const edge& e : edges_)
		{
			++successor_offsets_[e.predecessor + 1];
		}
		for (std::size_t i = 0; i < count; ++i)
		{
			successor_offsets_[i + 1] += successor_offsets_[i];
		}

		successors_.assign(edges_.size(), 0);
		std::vector<std::uint32_t> cursor{successor_offsets_.begin(), successor_offsets_.end() - 1};
		for (const edge& e : edges_)
		{
			successors_[cursor[e.predecessor]++] = e.successor;
		}

		roots_.clear();
		depth_.assign(count, 0);
		for (std::size_t i = 0; i < count; ++i)
		{
			if (nodes_[i].predecessor_count == 0)
			{
				roots_.push_back(static_cast<node_index>(i));
			}
		}
		// 索引即拓扑序，所以一次正向扫描就能算出最长路径深度（可当关键路径优先级用）。
		for (const edge& e : edges_)
		{
			const std::uint32_t candidate = depth_[e.predecessor] + 1;
			if (depth_[e.successor] < candidate)
			{
				depth_[e.successor] = candidate;
			}
		}

		finalized_ = true;
	}

	[[nodiscard]] std::size_t node_count() const noexcept { return nodes_.size(); }
	[[nodiscard]] std::size_t edge_count() const noexcept { return edges_.size(); }
	[[nodiscard]] std::size_t root_count() const noexcept { return roots_.size(); }
	[[nodiscard]] std::size_t operation_arena_size() const noexcept { return operation_arena_size_; }

	[[nodiscard]] const std::string& node_name(node_index index) const noexcept { return nodes_[index].name; }
	[[nodiscard]] std::uint32_t node_depth(node_index index) const noexcept { return depth_[index]; }

	[[nodiscard]] std::uint32_t critical_path_length() const noexcept
	{
		std::uint32_t longest = 0;
		for (const std::uint32_t value : depth_)
		{
			longest = (value > longest) ? value : longest;
		}
		return longest + 1;
	}

	void set_tracer(tracer_fn tracer, void* user) noexcept
	{
		tracer_ = tracer;
		tracer_user_ = user;
	}

	/// 返回一个合规 sender：整张图对外就是"完成 / 失败 / 取消"的单点。
	[[nodiscard]] graph_sender run() const noexcept;

private:
	friend class graph_run;

	struct node_desc
	{
		const detail::node_vtable* vtable = nullptr;
		void* sender = nullptr;
		std::uint32_t operation_offset = 0;
		std::uint32_t predecessor_count = 0;
		std::string name;
	};

	struct edge
	{
		node_index predecessor;
		node_index successor;
	};

	[[nodiscard]] run_storage acquire_storage() const;
	void release_storage(run_storage storage) const;

	std::vector<node_desc> nodes_;
	std::vector<edge> edges_;
	std::vector<std::uint32_t> successor_offsets_;
	std::vector<node_index> successors_;
	std::vector<node_index> roots_;
	std::vector<std::uint32_t> depth_;

	detail::chunk_arena sender_arena_;
	std::size_t operation_arena_size_ = 0;
	std::size_t operation_alignment_ = alignof(std::max_align_t);
	bool finalized_ = false;

	tracer_fn tracer_ = nullptr;
	void* tracer_user_ = nullptr;

	mutable std::mutex storage_mutex_;
	mutable std::vector<run_storage> storage_pool_;
};

inline run_storage graph::acquire_storage() const
{
	{
		std::lock_guard lock{storage_mutex_};
		if (!storage_pool_.empty())
		{
			run_storage storage = std::move(storage_pool_.back());
			storage_pool_.pop_back();
			return storage;
		}
	}

	run_storage storage;
	const std::size_t count = nodes_.size();

	// 多要 alignment 个字节然后手工对齐，从而不需要 aligned operator new。
	const std::size_t bytes = operation_arena_size_ + operation_alignment_;
	storage.operation_block = std::make_unique<std::byte[]>(bytes);

	void* cursor = storage.operation_block.get();
	std::size_t space = bytes;
	void* const aligned = std::align(operation_alignment_,
		(operation_arena_size_ == 0) ? std::size_t{1} : operation_arena_size_, cursor, space);
	assert(aligned != nullptr);
	storage.operation_base = static_cast<std::byte*>(aligned);

	storage.counters = std::make_unique<std::atomic<std::uint32_t>[]>(count);
	storage.poisoned = std::make_unique<std::atomic<bool>[]>(count);
	return storage;
}

inline void graph::release_storage(run_storage storage) const
{
	std::lock_guard lock{storage_mutex_};
	storage_pool_.push_back(std::move(storage));
}

// ------------------------------------------------------------------ graph_run 实现

inline graph_run::graph_run(const graph& owner)
	: owner_{&owner}
	, storage_{owner.acquire_storage()}
	, node_count_{static_cast<std::uint32_t>(owner.nodes_.size())}
{
	assert(owner.finalized_ && "执行前必须调用 finalize()");
}

inline graph_run::~graph_run()
{
	// 坑 ①：op-state 绝不能在自己的 completion 里析构 —— 那时它自己的调用栈帧还在。
	// 统一推迟到整图结束后在这里析构。arena 反正已经预留，没有额外代价。
	for (std::uint32_t i = 0; i < constructed_; ++i)
	{
		const graph::node_desc& node = owner_->nodes_[i];
		node.vtable->destroy_operation(storage_.operation_base + node.operation_offset);
	}
	owner_->release_storage(std::move(storage_));
}

inline void graph_run::trace(node_index index, node_event event) const noexcept
{
	if (owner_->tracer_ != nullptr)
	{
		owner_->tracer_(owner_->tracer_user_, index, event);
	}
}

inline void graph_run::launch() noexcept
{
	if (node_count_ == 0)
	{
		complete({}, false);
		return;
	}

	try
	{
		for (std::uint32_t i = 0; i < node_count_; ++i)
		{
			const graph::node_desc& node = owner_->nodes_[i];
			node.vtable->connect(storage_.operation_base + node.operation_offset, node.sender, this, i);
			constructed_ = i + 1;
		}
	}
	catch (...)
	{
		// connect 抛了（复制 sender 内的可调用对象失败）。一个节点都没启动过，
		// 直接以错误完成；已构造的 op-state 交给析构函数收拾。
		complete(std::current_exception(), false);
		return;
	}

	for (std::uint32_t i = 0; i < node_count_; ++i)
	{
		storage_.counters[i].store(owner_->nodes_[i].predecessor_count, std::memory_order_relaxed);
		storage_.poisoned[i].store(false, std::memory_order_relaxed);
	}
	outstanding_.store(node_count_, std::memory_order_relaxed);

	// 保证上面的初始化对随后被唤醒的 worker 线程可见。
	std::atomic_thread_fence(std::memory_order_release);

	for (const node_index root : owner_->roots_)
	{
		schedule_start(root);
	}
}

inline void graph_run::schedule_start(node_index index) noexcept
{
	// 坑 ②：内联完成导致的栈递归。最外层调用负责把嵌套的启动请求排干。
	trampoline_state& state = trampoline();
	if (state.active)
	{
		state.pending.emplace_back(this, index);
		return;
	}

	state.active = true;
	start_node(index);

	while (!state.pending.empty())
	{
		const std::pair<graph_run*, node_index> entry = state.pending.back();
		state.pending.pop_back();
		entry.first->start_node(entry.second);
	}
	state.active = false;
	// pending 的容量被 thread_local 留了下来，稳态下不再分配。
}

inline void graph_run::start_node(node_index index) noexcept
{
	if (storage_.poisoned[index].load(std::memory_order_relaxed) || stop_requested())
	{
		// 前驱失败，或整图已被取消：不执行，直接退休，并把毒性继续往下传。
		trace(index, node_event::skipped);
		retire(index, true);
		return;
	}

	trace(index, node_event::started);
	const graph::node_desc& node = owner_->nodes_[index];
	node.vtable->start(storage_.operation_base + node.operation_offset);
}

inline void graph_run::retire(node_index index, bool poison_successors) noexcept
{
	const graph& owner = *owner_;
	const std::uint32_t begin = owner.successor_offsets_[index];
	const std::uint32_t end = owner.successor_offsets_[index + 1];

	for (std::uint32_t k = begin; k < end; ++k)
	{
		const node_index successor = owner.successors_[k];

		if (poison_successors)
		{
			// relaxed 就够：下面那次 fetch_sub(acq_rel) 会把这个写发布出去；
			// 读到计数归零的线程 acquire 了所有前驱的 release，因此必然看得见。
			storage_.poisoned[successor].store(true, std::memory_order_relaxed);
		}

		// 每条边恰好一次原子操作。acq_rel 同时承担两件事：
		//   * 计数（扇入 == 倒数计数器）
		//   * happens-before：本节点的所有写，对启动后继的那个线程可见
		if (storage_.counters[successor].fetch_sub(1, std::memory_order_acq_rel) == 1)
		{
			schedule_start(successor);
		}
	}

	if (outstanding_.fetch_sub(1, std::memory_order_acq_rel) == 1)
	{
		finish();
		return; // finish 之后 this 可能已被销毁。
	}
}

inline void graph_run::record_error(std::exception_ptr error) noexcept
{
	bool expected = false;
	if (error_claimed_.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
	{
		error_ = std::move(error);
		error_ready_.store(true, std::memory_order_release);
	}
	// 后续错误被丢弃，但节点数不会因此少退休一个 —— 不会死锁。
}

inline void graph_run::on_node_done(node_index index) noexcept
{
	trace(index, node_event::completed);
	retire(index, false);
}

inline void graph_run::on_node_error(node_index index, std::exception_ptr error) noexcept
{
	trace(index, node_event::failed);
	record_error(std::move(error));
	request_stop(); // 让还在跑的兄弟节点收到取消
	// 坑 ③：失败时**仍然必须**给后继减计数，只是同时打上毒标记。
	// 少减一次就意味着 outstanding_ 永远到不了 0 —— 整图挂死。
	retire(index, true);
}

inline void graph_run::on_node_stopped(node_index index) noexcept
{
	trace(index, node_event::cancelled);
	stopped_.store(true, std::memory_order_release);
	retire(index, true);
}

inline void graph_run::finish() noexcept
{
	std::exception_ptr error;
	if (error_ready_.load(std::memory_order_acquire))
	{
		error = std::move(error_);
	}
	const bool stopped = stopped_.load(std::memory_order_acquire);

	complete(std::move(error), stopped);
	// complete 之后绝不触碰 this。
}

// ------------------------------------------------------------------ graph 的 sender / op-state

template<class Receiver>
class graph_op final : public graph_run
{
public:
	using operation_state_concept = ::stdexec::operation_state_t;

	graph_op(const graph& owner, Receiver receiver)
		: graph_run{owner}
		, receiver_{std::move(receiver)}
	{
	}

	graph_op(graph_op&&) = delete;
	graph_op& operator=(graph_op&&) = delete;

	void start() & noexcept
	{
		// 把外层 env 的取消接进图自己的 stop source，两路取消合并成一路。
		outer_callback_.emplace(
			::stdexec::get_stop_token(::stdexec::get_env(receiver_)),
			on_outer_stop{this});
		launch();
	}

private:
	struct on_outer_stop
	{
		graph_run* run;

		void operator()() const noexcept
		{
			run->request_stop();
		}
	};

	using outer_token_type = ::stdexec::stop_token_of_t<::stdexec::env_of_t<Receiver>>;
	using outer_callback_type = typename outer_token_type::template callback_type<on_outer_stop>;

	void complete(std::exception_ptr error, bool stopped) noexcept override
	{
		// 先摘掉回调，之后 receiver 的完成动作可能立刻销毁整个 op。
		outer_callback_.reset();

		if (error)
		{
			::stdexec::set_error(std::move(receiver_), std::move(error));
		}
		else if (stopped)
		{
			::stdexec::set_stopped(std::move(receiver_));
		}
		else
		{
			::stdexec::set_value(std::move(receiver_));
		}
	}

	Receiver receiver_;
	std::optional<outer_callback_type> outer_callback_;
};

class graph_sender
{
public:
	using sender_concept = ::stdexec::sender_t;

	using completion_signatures = ::stdexec::completion_signatures<
		::stdexec::set_value_t(),
		::stdexec::set_error_t(std::exception_ptr),
		::stdexec::set_stopped_t()>;

	explicit graph_sender(const graph& owner) noexcept
		: owner_{&owner}
	{
	}

	template<class Receiver>
	[[nodiscard]] graph_op<std::decay_t<Receiver>> connect(Receiver&& receiver) const
	{
		return graph_op<std::decay_t<Receiver>>(*owner_, std::forward<Receiver>(receiver));
	}

private:
	const graph* owner_;
};

inline graph_sender graph::run() const noexcept
{
	return graph_sender{*this};
}

} // namespace dag
