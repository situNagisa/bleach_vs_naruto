#pragma once

namespace bvn::platform
{
/**
 * Owns process-wide SDL video initialization for the platform module.
 */
struct sdl_context
{
public:
	sdl_context();
	~sdl_context() noexcept;

	sdl_context(sdl_context const&) = delete;
	auto operator=(sdl_context const&) -> sdl_context& = delete;

	sdl_context(sdl_context&&) = delete;
	auto operator=(sdl_context&&) -> sdl_context& = delete;
};
}
