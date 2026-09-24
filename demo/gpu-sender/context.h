#pragma once

#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <limits>
#include <mutex>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <stop_token>
#include <string_view>
#include <thread>
#include <vector>

#include <vulkan/vulkan.h>

#include <vkkl/vkkl.h>
#include <vkfu/generated/vulkan-v1.4.328.h>

/// 原型的第一层：GPU 时间线。
///
/// 整个 GPU 域只有一个队列、一条 timeline semaphore。每次 submit 在队列锁里分配下一个
/// 值 N 并 signal 它；reactor 线程等 timeline 走到 N，再回调登记在 N 上的等待者。
/// CPU 观察 GPU 的唯一通道就是这里——fence、binary semaphore 都不出现。
namespace gpu
{
struct immovable
{
	immovable() = default;
	immovable(immovable&&) = delete;
	auto operator=(immovable&&) -> immovable& = delete;
};

/// 登记在某个 timeline 值上的等待者。`complete` 在 reactor 线程上被调用，
/// 失败时（device lost 等）带回非 `VK_SUCCESS`。
struct waiter
{
	::std::uint64_t value = 0;
	void (*complete)(waiter&, ::VkResult) noexcept = nullptr;
};

/// 把"timeline 到达 N"翻译成 CPU 回调。
///
/// 只有一条 timeline，且值在队列锁里单调分配、按同一顺序登记，所以 `_pending` 天然有序，
/// 只需要等队头的值。`vkWaitSemaphores` 打断不了，但新登记的值只会更大，所以阻塞期间
/// 来的新等待者不需要唤醒它；只有队列空的时候才靠条件变量睡。
///
/// 回调直接在 reactor 线程上执行，也就是说**下游续体会跑在这个线程上**（推演【发现 7】）。
/// 原型先不管，正式版应当在完成时切回 env 里的调度器。
class reactor : immovable
{
public:
	reactor(::VkDevice device, ::VkSemaphore timeline)
		: _device(device), _timeline(timeline), _thread([this](::std::stop_token stop) { _run(stop); })
	{
	}

	/// 调用方保证按值递增的顺序调用（`context::submit` 在队列锁里调用它）。
	auto add(waiter& w) -> void
	{
		{
			auto lock = ::std::scoped_lock{_mutex};
			_pending.push_back(&w);
		}
		_wake.notify_one();
	}

private:
	auto _run(::std::stop_token stop) -> void
	{
		auto ready = ::std::vector<waiter*>{};
		while (true)
		{
			auto target = ::std::uint64_t{};
			{
				auto lock = ::std::unique_lock{_mutex};
				if (!_wake.wait(lock, stop, [&] { return !_pending.empty(); }))
					return;
				target = _pending.front()->value;
			}

			auto const outcome = ::vkfu::wait_semaphores(_device, ::vkfu::param::semaphore_wait{
				.semaphore_count = 1, .semaphores = &_timeline, .values = &target,
			}, (::std::numeric_limits<::std::uint64_t>::max)(), ::std::nothrow);
			auto const result = outcome ? ::VK_SUCCESS : outcome.error();
			auto reached = target;
			if (result == ::VK_SUCCESS)
				(void)::vkGetSemaphoreCounterValue(_device, _timeline, &reached);

			{
				auto lock = ::std::scoped_lock{_mutex};
				// 失败时把所有等待者都放出来报错，否则只放已经到达的。
				while (!_pending.empty() && (result != ::VK_SUCCESS || _pending.front()->value <= reached))
				{
					ready.push_back(_pending.front());
					_pending.pop_front();
				}
			}
			for (auto w : ready)
				w->complete(*w, result);
			ready.clear();
		}
	}

	::VkDevice _device;
	::VkSemaphore _timeline;
	::std::mutex _mutex{};
	::std::condition_variable_any _wake{};
	::std::deque<waiter*> _pending{};
	// 最后声明：析构时最先 request_stop + join，此时其余成员都还活着。
	::std::jthread _thread;
};

inline auto check(::VkResult result, char const* operation) -> void
{
	if (result != ::VK_SUCCESS)
		throw ::std::runtime_error{operation};
}

/// 无窗口的最小 Vulkan 环境：instance、device、一个队列、一条 timeline。
///
/// 析构前必须保证所有 GPU sender 都已完成——reactor 的 join 不会等挂起的等待者。
class context : immovable
{
public:
	context()
	{
		_create_instance();
		_create_device();
		_timeline = ::vkkl::semaphore{_device.handle, ::vkfu::create_semaphore(_device.handle,
			::vkfu::param::semaphore{} | ::vkfu::param::option::semaphore_type{
				.type = ::vkfu::enums::semaphore_type::timeline,
				.initial_value = 0,
			})};
		_reactor.emplace(_device.handle, _timeline.handle);
	}

	~context() noexcept
	{
		_reactor.reset();
		if (_device.handle != VK_NULL_HANDLE)
			(void)::vkDeviceWaitIdle(_device.handle);
	}

	[[nodiscard]] auto device() const noexcept -> ::VkDevice { return _device.handle; }
	[[nodiscard]] auto physical_device() const noexcept -> ::VkPhysicalDevice { return _physical_device; }
	[[nodiscard]] auto queue_family() const noexcept -> ::std::uint32_t { return _queue_family; }

	/// 已提交的批次数，也就是 timeline 最后分配到的值。用来观察命令缓冲有没有被合并。
	[[nodiscard]] auto submissions() const -> ::std::uint64_t
	{
		auto lock = ::std::scoped_lock{_queue_mutex};
		return _last_value;
	}

	/// 提交一个命令缓冲，并把 `w` 登记在它 signal 的 timeline 值上。
	///
	/// 值的分配、submit、登记都在同一把锁里：同一 semaphore 的 signal 值必须按提交顺序
	/// 严格递增（推演【发现 5】），队列本身也要求外部同步（【发现 6】）。
	auto submit(::VkCommandBuffer command, waiter& w) -> void
	{
		namespace param = ::vkfu::param;

		auto lock = ::std::scoped_lock{_queue_mutex};
		auto const value = _last_value + 1;
		auto const command_info = ::vkfu::evaluate(param::command_buffer_submit{.command_buffer = command});
		auto const signal = ::vkfu::evaluate(param::semaphore_submit{
			.semaphore = _timeline.handle, .value = value, .stage_mask = {.all_commands = 1},
		});
		auto const submit = ::vkfu::evaluate(param::submit2{
			.command_buffer_infos = ::std::span{&command_info, 1u},
			.signal_semaphore_infos = ::std::span{&signal, 1u},
		});
		check(::vkQueueSubmit2(_queue, 1, &submit, VK_NULL_HANDLE), "gpu: queue submit");
		_last_value = value;
		w.value = value;
		_reactor->add(w);
	}

private:
	auto _create_instance() -> void
	{
		namespace param = ::vkfu::param;

		auto count = ::std::uint32_t{};
		::vkEnumerateInstanceLayerProperties(&count, nullptr);
		auto layers = ::std::vector<::VkLayerProperties>(count);
		::vkEnumerateInstanceLayerProperties(&count, layers.data());
		auto enabled = ::std::vector<char const*>{};
		if (::std::ranges::any_of(layers, [](auto const& layer)
			{
				return ::std::string_view{layer.layerName} == "VK_LAYER_KHRONOS_validation";
			}))
			enabled.push_back("VK_LAYER_KHRONOS_validation");

		_instance = ::vkkl::instance{::vkfu::create_instance(param::instance{
			.application_info = param::application{
				.name = "bvn gpu sender prototype",
				.version = VK_MAKE_API_VERSION(0, 0, 1, 0),
				.engine_name = "bvn",
				.engine_version = VK_MAKE_API_VERSION(0, 0, 1, 0),
				.api_version = VK_API_VERSION_1_3,
			},
			.enabled_layer_names = enabled,
		})};
	}

	auto _create_device() -> void
	{
		namespace param = ::vkfu::param;

		// Vulkan 1.3 把 timelineSemaphore 和 synchronization2 都列为必须支持，只查版本即可。
		auto count = ::std::uint32_t{};
		::vkEnumeratePhysicalDevices(_instance.handle, &count, nullptr);
		auto candidates = ::std::vector<::VkPhysicalDevice>(count);
		::vkEnumeratePhysicalDevices(_instance.handle, &count, candidates.data());
		for (auto const candidate : candidates)
		{
			auto properties = ::VkPhysicalDeviceProperties{};
			::vkGetPhysicalDeviceProperties(candidate, &properties);
			if (properties.apiVersion < VK_API_VERSION_1_3)
				continue;
			auto family_count = ::std::uint32_t{};
			::vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count, nullptr);
			auto families = ::std::vector<::VkQueueFamilyProperties>(family_count);
			::vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count, families.data());
			for (auto index = ::std::uint32_t{}; index < family_count; ++index)
				if ((families[index].queueFlags & (::VK_QUEUE_GRAPHICS_BIT | ::VK_QUEUE_COMPUTE_BIT)) != 0)
				{
					_physical_device = candidate;
					_queue_family = index;
					break;
				}
			if (_physical_device != VK_NULL_HANDLE)
				break;
		}
		if (_physical_device == VK_NULL_HANDLE)
			throw ::std::runtime_error{"gpu: no Vulkan 1.3 device with a graphics or compute queue"};

		auto const priority = 1.0f;
		auto const queue = ::vkfu::evaluate(param::device_queue{
			.queue_family_index = _queue_family,
			.queue_priorities = ::std::span{&priority, 1u},
		});
		_device = ::vkkl::device{::vkfu::create_device(_physical_device, ::vkfu::chain(
			param::device{.queue_create_infos = ::std::span{&queue, 1u}},
			param::feature::vulkan12{.timeline_semaphore = true},
			param::feature::vulkan13{.synchronization2 = true}
		))};
		::vkGetDeviceQueue(_device.handle, _queue_family, 0, &_queue);
	}

	::vkkl::instance _instance{};
	::VkPhysicalDevice _physical_device = VK_NULL_HANDLE;
	::std::uint32_t _queue_family = 0;
	::vkkl::device _device{};
	::VkQueue _queue = VK_NULL_HANDLE;
	::vkkl::semaphore _timeline{};
	mutable ::std::mutex _queue_mutex{};
	::std::uint64_t _last_value = 0;
	::std::optional<reactor> _reactor{};
};

/// 常驻映射的 host-visible | host-coherent buffer。coherent 省掉了 invalidate，
/// 正式版要按内存类型决定。
class host_buffer : immovable
{
public:
	host_buffer(context const& ctx, ::VkDeviceSize size) : _size(size)
	{
		namespace param = ::vkfu::param;

		auto const device = ctx.device();
		_buffer = ::vkkl::buffer{device, ::vkfu::create_buffer(device, param::buffer{
			.size = size, .usage = {.transfer_src = 1, .transfer_dst = 1},
		})};
		auto const requirements = ::vkfu::get_buffer_memory_requirements2(device,
			param::buffer_memory_requirements2{.buffer = _buffer.handle}).head().memoryRequirements;
		_memory = ::vkkl::device_memory{device, ::vkfu::allocate_memory(device, param::memory{
			.allocation_size = requirements.size,
			.type_index = _memory_type(ctx.physical_device(), requirements.memoryTypeBits),
		})};
		auto const bind = ::vkfu::evaluate(param::bind_buffer_memory{.buffer = _buffer.handle, .memory = _memory.handle});
		::vkfu::bind_buffer_memory2(device, ::std::span{&bind, 1u});
		check(::vkMapMemory(device, _memory.handle, 0, VK_WHOLE_SIZE, 0, &_mapped), "gpu: map buffer");
	}

	[[nodiscard]] auto handle() const noexcept -> ::VkBuffer { return _buffer.handle; }
	[[nodiscard]] auto size() const noexcept -> ::VkDeviceSize { return _size; }
	[[nodiscard]] auto mapped() const noexcept -> void* { return _mapped; }

private:
	static auto _memory_type(::VkPhysicalDevice physical, ::std::uint32_t allowed) -> ::std::uint32_t
	{
		constexpr auto wanted = ::VkMemoryPropertyFlags{VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT};
		auto const properties = ::vkfu::get_physical_device_memory_properties2(physical).head().memoryProperties;
		for (auto index = ::std::uint32_t{}; index < properties.memoryTypeCount; ++index)
			if ((allowed & (1u << index)) && (properties.memoryTypes[index].propertyFlags & wanted) == wanted)
				return index;
		throw ::std::runtime_error{"gpu: no host-visible coherent memory type"};
	}

	::VkDeviceSize _size;
	// 先声明内存：析构时先销毁 buffer，再释放（并隐式 unmap）内存。
	::vkkl::device_memory _memory{};
	::vkkl::buffer _buffer{};
	void* _mapped = nullptr;
};
}
