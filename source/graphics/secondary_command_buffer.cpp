#include <stdexcept>

#include <bvn/graphics/secondary_command_buffer.h>
#include <bvn/graphics/environment.h>

NAGISA_BUILD_LIB_BEGIN

secondary_command_pool::secondary_command_pool(::VkDevice device, ::std::uint32_t queue_family)
	: _device(device)
{
	auto pool_info = ::VkCommandPoolCreateInfo{};
	pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	pool_info.queueFamilyIndex = queue_family;

	auto pool = ::VkCommandPool{};
	if (::vkCreateCommandPool(_device, &pool_info, nullptr, &pool) != ::VK_SUCCESS)
	{
		throw ::std::runtime_error{"failed to create secondary command pool"};
	}
	_pool = ::vkkl::command_pool{_device, pool};
}

auto secondary_command_pool::acquire() -> command_buffer
{
	auto lock = ::std::scoped_lock{_mutex};
	auto&& [index, command] = _commands.acquire([this]
	{
		auto allocate_info = ::VkCommandBufferAllocateInfo{};
		allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocate_info.commandPool = _pool.handle;
		allocate_info.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
		allocate_info.commandBufferCount = 1;

		auto handle = ::VkCommandBuffer{};
		if (::vkAllocateCommandBuffers(_device, &allocate_info, &handle) != ::VK_SUCCESS)
		{
			throw ::std::runtime_error{"failed to allocate secondary command buffer"};
		}
		return ::vkkl::command_buffer{_device, _pool.handle, handle};
	});

	if (::vkResetCommandBuffer(command.handle, 0) != ::VK_SUCCESS)
	{
		_commands.release(index);
		throw ::std::runtime_error{"failed to reset secondary command buffer"};
	}

	return command_buffer{*this, index, command.handle};
}

auto secondary_command_pool::recycle(::std::size_t index) noexcept -> void
{
	auto lock = ::std::scoped_lock{_mutex};
	_commands.release(index);
}

auto secondary_command_pool::command_buffer::recycle() noexcept -> void
{
	assert(_pool != nullptr);
	auto pool = ::std::exchange(_pool, nullptr);
	auto const index = ::std::exchange(_index, 0);
	_handle = VK_NULL_HANDLE;
	pool->recycle(index);
}

secondary_command_pool::command_buffer::~command_buffer() noexcept
{
	if (_pool != nullptr)
	{
		recycle();
	}
}

NAGISA_BUILD_LIB_END

#include <nagisa/build_lib/destruct.h>
