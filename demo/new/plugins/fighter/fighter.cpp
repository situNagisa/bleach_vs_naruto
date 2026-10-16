/// fighter 模块：编成独立的 dll / so，导出 "naruto"、"ichigo" 两种 entity。
///
/// 和宿主同工具链、共享头文件。vector_graph 不是 header-only，所以这个模块的工程把
/// `entity/main_menu/vector_graph.cpp` 也编进来，不依赖宿主导出符号。

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#define GLM_ENABLE_EXPERIMENTAL 1
#include <glm/glm.hpp>
#include <glm/gtx/matrix_transform_2d.hpp>

#include <stdexec/execution.hpp>

#include <vkkl/command_buffer.h>
#include <vkkl/command_pool.h>

#include "../../entity/fighting_scene/entity.h"
#include "../../entity/fighting_scene/module.h"
#include "../../entity/main_menu/buffer_type.h"
#include "../../entity/main_menu/vector_graph.h"

#include "./fighter_shared.h"

namespace
{
	struct fighter_config
	{
		input::key_code left;
		input::key_code right;
		input::key_code attack;
		float start_x;
		color body;
	};

	constexpr auto naruto_config = fighter_config{ input::key_code::a, input::key_code::d, input::key_code::j, -2.0f, { 0.98f, 0.55f, 0.1f, 1.0f } };
	constexpr auto ichigo_config = fighter_config{ input::key_code::left, input::key_code::right, input::key_code::k, 2.0f, { 0.2f, 0.3f, 0.8f, 1.0f } };

	struct fighter final : fighting_entity
	{
		entity_context _context;
		fighter_config _config;

		// 共享出去的那一份：成员，地址整帧稳定
		fighter_shared _shared{};

		// 私有：update 写、settle 读
		float _move = 0.0f;

		// 绘制资源：自己的池子，只在自己的 record 里用，不跟别人抢
		::vkkl::command_pool _pool;
		vector_graph_type _vector_graph;
		::vkkl::command_buffer _command{};
		::std::vector<buffer_type> _buffers{};

		fighter(entity_context const& context, fighter_config const& config)
			: _context(context)
			, _config(config)
			, _pool(::consumer_arch_vulkan::create_secondary_command_pool(context.draw->vulkan.global_env()))
			, _vector_graph(context.draw->vulkan.global_env().device(), context.draw->vulkan.global_env().swapchain_image_format())
		{
			_shared.x = config.start_x;
		}

		auto phases() const noexcept -> phase override
		{
			return phase::update | phase::settle | phase::record;
		}

		void publish(entity_registry& registry) override
		{
			registry.publish(_context.instance, _shared);
		}

		/// 读别人的 x（上一帧结算结果），往别人的 inbox 里打。
		auto update(frame_view const& frame) -> values_sender_type<> override
		{
			return ::stdexec::just() | ::stdexec::then([this, &frame]
				{
					auto const& current = *frame.current;
					auto const& changes = *frame.changes;
					_move = static_cast<float>(current.keyboard.key(_config.right)) - static_cast<float>(current.keyboard.key(_config.left));
					if (!changes.keyboard.key(_config.attack).pressed)
						return;
					frame.registry->each<fighter_shared>([this](::std::uint32_t instance, fighter_shared const& other)
						{
							if (instance == _context.instance)
								return;
							auto const distance = other.x - _shared.x;
							if (::std::abs(distance) < 1.2f)
								other.inbox.push(fighter_hit{ .attacker = _context.instance, .damage = 8, .knockback = distance < 0 ? -0.5f : 0.5f });
						});
				});
		}

		/// 只碰自己：移动、结算自己的 inbox。
		auto settle(frame_view const& frame) -> values_sender_type<> override
		{
			return ::stdexec::just() | ::stdexec::then([this, &frame]
				{
					auto const seconds = ::std::chrono::duration<float>(frame.time->delta).count();
					_shared.x = ::std::clamp(_shared.x + _move * 3.0f * seconds, -4.5f, 4.5f);

					// inbox 写入顺序随调度而变；按 attacker 排序后应用，结果确定。
					auto events = _shared.inbox.events();
					::std::ranges::sort(events, {}, &fighter_hit::attacker);
					for (auto const& hit : events)
					{
						_shared.hp = (::std::max)(_shared.hp - hit.damage, 0);
						_shared.x = ::std::clamp(_shared.x + hit.knockback, -4.5f, 4.5f);
					}
					_shared.inbox.clear();
				});
		}

		auto record(frame_slot&, ::VkCommandBuffer& out) -> values_sender_type<> override
		{
			return ::stdexec::just() | ::stdexec::then([this, &out] { out = _record(); });
		}

		auto _record() -> ::VkCommandBuffer
		{
			using namespace ::vkfu::enums;
			auto const global = _context.draw->vulkan.global_env();
			auto const device = global.device();
			auto const physical_device = global.physical_device();
			auto const extent = global.swapchain_extent();

			// 覆盖上一帧的：它的 fence 在 renderer::end 里已经等过。
			_buffers.clear();
			_command = ::vkkl::command_buffer{ device, _pool.handle, VK_NULL_HANDLE };
			::vkfu::allocate_command_buffers(device, ::vkfu::param::command_buffer{ .command_pool = _pool.handle, .level = command_buffer_level::secondary, .command_buffer_count = 1 }, ::std::span{ &_command.handle, 1u });
			::vkfu::begin_command_buffer(_command.handle, ::vkfu::param::command_buffer_begin{
				.flags = {.one_time_submit = 1, .render_pass_continue = 1},
				.inheritance_info =
					::vkfu::param::command_buffer_inheritance{}
					| ::vkfu::param::option::command_buffer_inheritance_rendering{
						.color_attachment_formats = ::std::array{ global.swapchain_image_format() },
						.rasterization_samples = sample_count::count_1,
					}
				});
			::vkfu::cmd_set_viewport(_command.handle, 0, ::std::array{ ::vkfu::evaluate(::vkfu::param::viewport{.width = static_cast<float>(extent.width), .height = static_cast<float>(extent.height), .max_depth = 1.0f }) });
			::vkfu::cmd_set_scissor(_command.handle, 0, ::std::array{ ::vkfu::evaluate(::vkfu::param::rect_2d{.extent = extent }) });

			auto const viewport = point(extent.width, extent.height);
			auto const viewport_transform = ::glm::scale(
				::glm::translate(::glm::mat3{ 1.0f }, ::glm::vec2{ -1.0f, -1.0f }),
				::glm::vec2{ 2.0f / viewport.x, 2.0f / viewport.y });

			// 世界坐标 x ∈ [-5, 5] 映射到屏幕宽度，角色站在 80% 高度的地面上。
			auto const unit = viewport.x / 10.0f;
			auto const ground = viewport.y * 0.8f;
			auto const rectangle = [&](float center, float bottom, point size, color fill)
				{
					auto const left = (center + 5.0f) * unit - size.x * unit * 0.5f;
					auto const top = ground - (bottom + size.y) * unit;
					_buffers.emplace_back(_vector_graph.draw_rectangle(device, physical_device, _command.handle,
						{ size.x * unit, size.y * unit }, fill, ::glm::translate(viewport_transform, ::glm::vec2{ left, top })));
				};
			rectangle(_shared.x, 0.0f, { 0.6f, 1.2f }, _config.body);
			auto const hp = static_cast<float>(_shared.hp) / 100.0f;
			rectangle(_shared.x - 0.3f * (1.0f - hp), 1.35f, { 0.6f * hp, 0.08f }, { 0.9f, 0.2f, 0.2f, 1.0f });

			::consumer_arch_vulkan::check(::vkEndCommandBuffer(_command.handle), "fighter: end draw");
			return _command.handle;
		}
	};

	auto create(::std::string_view kind, entity_context const& context) -> ::std::unique_ptr<fighting_entity>
	{
		if (kind == "naruto")
			return ::std::make_unique<fighter>(context, naruto_config);
		if (kind == "ichigo")
			return ::std::make_unique<fighter>(context, ichigo_config);
		return nullptr;
	}
}

BVN_ENTITY_MODULE(create)
