#pragma once

#include <cstdint>
#include <format>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

/// 整个原型里"vulkan 调用"的替身。不链接任何 vulkan 头文件或 loader——每一次
/// "本该发给 GPU 驱动"的调用只是把一行可读文本记进日志，main.cpp 靠这份日志断言
/// 调用的种类、顺序、次数是否符合设计。命令录下即视为完成，没有真正的异步 GPU、
/// 没有真正的队列——这份原型只验证 CPO/concept/算法的类型流转和调用序列是否
/// 符合设计，不验证真实的 GPU 时序，也不需要这台机器有 Vulkan loader 或显卡。
namespace gpu::mock
{
using image_handle = ::std::uint64_t;
using buffer_handle = ::std::uint64_t;
using command_buffer_handle = ::std::uint64_t;
using signal_handle = ::std::uint64_t;

enum class pipeline_stage : ::std::uint32_t
{
	top_of_pipe,
	color_attachment_output,
	bottom_of_pipe,
	transfer,
	host,
};

enum class access : ::std::uint32_t
{
	none,
	color_attachment_write,
	transfer_write,
	host_read,
};

enum class image_layout : ::std::uint32_t
{
	undefined,
	color_attachment_optimal,
	transfer_dst_optimal,
	present_src,
};

/// 调用序列日志：main.cpp 靠它断言"这条链真的按预期录了/等了/提交了这些东西"。
class log
{
public:
	void record(::std::string line)
	{
		auto lock = ::std::scoped_lock{_mutex};
		_lines.push_back(::std::move(line));
		::std::cout << "  [mock] " << _lines.back() << '\n';
	}

	[[nodiscard]] ::std::vector<::std::string> const& lines() const
	{
		return _lines;
	}

private:
	::std::mutex _mutex{};
	::std::vector<::std::string> _lines{};
};

inline log global_log{};

inline ::std::uint64_t next_handle() noexcept
{
	static auto counter = ::std::uint64_t{0};
	return ++counter;
}

inline void cmd_pipeline_barrier(
	command_buffer_handle cmd,
	pipeline_stage src_stage, access src_access,
	pipeline_stage dst_stage, access dst_access,
	image_layout old_layout, image_layout new_layout,
	image_handle image
)
{
	global_log.record(::std::format(
		"cmd_pipeline_barrier(cmd={}, image={}, stage {}->{}, access {}->{}, layout {}->{})",
		cmd, image,
		static_cast<int>(src_stage), static_cast<int>(dst_stage),
		static_cast<int>(src_access), static_cast<int>(dst_access),
		static_cast<int>(old_layout), static_cast<int>(new_layout)));
}

inline void cmd_buffer_barrier(
	command_buffer_handle cmd,
	pipeline_stage src_stage, access src_access,
	pipeline_stage dst_stage, access dst_access,
	buffer_handle buffer
)
{
	global_log.record(::std::format(
		"cmd_buffer_barrier(cmd={}, buffer={}, stage {}->{}, access {}->{})",
		cmd, buffer, static_cast<int>(src_stage), static_cast<int>(dst_stage),
		static_cast<int>(src_access), static_cast<int>(dst_access)));
}

inline void cmd_clear_color_image(command_buffer_handle cmd, image_handle image)
{
	global_log.record(::std::format("cmd_clear_color_image(cmd={}, image={})", cmd, image));
}

/// 提交当前命令缓冲；waits 是这一批之前记下的、要等的外部信号。返回这一批完成时
/// 会产生的信号——真实实现里这是一个 timeline 值，这里只是个递增计数器。
inline signal_handle queue_submit(command_buffer_handle cmd, ::std::vector<signal_handle> const& waits)
{
	auto const signal = next_handle();
	global_log.record(::std::format("queue_submit(cmd={}, waits={}, signal={})", cmd, waits.size(), signal));
	return signal;
}

/// CPU 侧真正等一个信号——真实实现里这是 vkWaitSemaphores，这里同步立即"完成"。
inline void wait_signal(signal_handle signal)
{
	global_log.record(::std::format("host_wait(signal={})", signal));
}

struct swapchain
{
	::std::vector<image_handle> images{next_handle(), next_handle(), next_handle()};
	::std::size_t next_index = 0;
};

struct acquire_result
{
	image_handle image;
	signal_handle image_available;
};

inline acquire_result acquire_next_image(swapchain& sc)
{
	auto const index = sc.next_index;
	sc.next_index = (sc.next_index + 1) % sc.images.size();
	auto const signal = next_handle();
	global_log.record(::std::format("acquire_next_image(image={}, signal={})", sc.images[index], signal));
	return {sc.images[index], signal};
}

/// 真实的 vkQueuePresentKHR 要的是 swapchain 内的索引，不是 image 句柄本身；这里
/// 直接用句柄，是这份mock特意简化掉的一个真实 API 细节——它不影响这个原型要验证的
/// 东西（CPO/concept/算法的类型流转和调用序列），犯不着为了单独传一个索引再给
/// tracked_value/pending_value 的 Handle 额外包一层结构。
inline void queue_present(swapchain&, image_handle image, signal_handle wait)
{
	global_log.record(::std::format("queue_present(image={}, wait={})", image, wait));
}
}
