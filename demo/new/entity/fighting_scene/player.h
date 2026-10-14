#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "../render.h"
#include "../input.h"

struct player_base
{
	struct shared_state_base
	{
		virtual ~shared_state_base() noexcept = default;
	};
	struct operation_state_base
	{
		virtual ~operation_state_base() noexcept = default;
		[[nodiscard]] virtual ::std::unique_ptr<shared_state_base> start() & noexcept = 0;
	};

	virtual ~player_base() noexcept = default;

	virtual ::std::unique_ptr<operation_state_base> connect(frame_context& context, input& i, input::task& it, renderer& r, renderer::task& rt);
};
