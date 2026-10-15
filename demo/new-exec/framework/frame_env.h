#pragma once

#include <cstdint>

#include <stdexec/execution.hpp>

/// 帧序号查询。原来 frame_context 里的 index，换成 env 查询后谁要谁读。
struct get_frame_index_t
{
	template <class Env>
		requires requires (Env const& env) { env.query(get_frame_index_t{}); }
	constexpr auto operator()(Env const& env) const noexcept
	{
		return env.query(*this);
	}
};
inline constexpr get_frame_index_t get_frame_index{};

/// 一帧的环境，取代 frame_context。
///
/// 只放查询，不放容器：没有 roots，也没有任何 vector。scheduler 的类型是模板参数，
/// 框架不假定线程池。
template <class Scheduler, class StopToken>
[[nodiscard]] constexpr auto make_frame_env(::std::uint64_t index, Scheduler scheduler, StopToken stop_token)
{
	return ::stdexec::env{
		::stdexec::prop{ get_frame_index, index },
		::stdexec::prop{ ::stdexec::get_scheduler, scheduler },
		::stdexec::prop{ ::stdexec::get_stop_token, stop_token },
	};
}
