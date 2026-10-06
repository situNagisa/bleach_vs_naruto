#pragma once

#include <cstddef>
#include <exception>
#include <memory>
#include <optional>
#include <utility>

#include <stdexec/execution.hpp>
#include <exec/any_sender_of.hpp>
#include <exec/split.hpp>

#include "./input.h"
#include "./render.h"

#include "./detail/immovable.h"

struct frame_context;

/// 主菜单 entity，持有跨帧状态与绘制资源。
struct main_menu : immovable
{
	struct state
	{
		::std::size_t selected = 0;
		::std::optional<::std::size_t> activated{};
	};

	struct delta
	{
		bool moved = false;
		::std::optional<::std::size_t> activated{};
	};

	struct task
	{
		struct _buffer_type
		{
			::vkkl::buffer handle{};
			::vkkl::device_memory memory{};

			_buffer_type(VkDevice device, VkPhysicalDevice physical_device, ::std::span<::std::byte const> data, ::vkfu::param::buffer::usage_type usage)
			{
				namespace param = ::vkfu::param;
				auto memory_type = [](::VkPhysicalDevice physical, ::std::uint32_t allowed, ::VkMemoryPropertyFlags flags)
					{
						auto const properties = ::vkfu::get_physical_device_memory_properties2(physical).head().memoryProperties;
						for (auto index = ::std::uint32_t{}; index < properties.memoryTypeCount; ++index)
							if ((allowed & (1u << index)) && (properties.memoryTypes[index].propertyFlags & flags) == flags)
								return index;
						throw ::std::runtime_error{ "main menu: no compatible Vulkan memory type" };
					};

				assert(!data.empty());
				handle = ::vkkl::buffer{ device, ::vkfu::create_buffer(device, param::buffer{.size = data.size(), .usage = usage}) };
				auto requirements = ::vkfu::get_buffer_memory_requirements2(device, param::buffer_memory_requirements2{ .buffer = handle.handle }).head().memoryRequirements;
				memory = ::vkkl::device_memory{ device, ::vkfu::allocate_memory(device, param::memory{
					.allocation_size = requirements.size,
					.type_index = memory_type(physical_device, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
				}) };
				auto const bind = ::vkfu::evaluate(param::bind_buffer_memory{ .buffer = handle.handle, .memory = memory.handle });
				::vkfu::bind_buffer_memory2(device, ::std::span{ &bind, 1u });
				auto mapped = static_cast<void*>(nullptr);
				::consumer_arch_vulkan::check(::vkMapMemory(device, memory.handle, 0, data.size(), 0, &mapped), "main menu: map buffer");
				::std::memcpy(mapped, data.data(), data.size());
				::vkUnmapMemory(device, memory.handle);
			}
		};

		::nagisa::concurrency::details::manual_lifetime<_buffer_type> _buffer;
		::vkkl::command_pool _pool;
		::vkkl::command_buffer _command{};
		decltype(::exec::split(::std::declval<values_sender_type<state, delta>>())) _update;

		[[nodiscard]] auto update() const noexcept { return _update; }

		explicit task(main_menu& menu, input::task& controls, renderer& r, renderer::task& draw) noexcept;
	};

	main_menu(renderer& r);
	~main_menu();

	void build_task(task& t, renderer& r, renderer::task& rt);

	state const& current_state() const noexcept;

	struct implementation;
	::std::unique_ptr<implementation> _impl;
};
