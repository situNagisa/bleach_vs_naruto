/// ichigo：一个独立的 player 插件（ichigo.dll）。和 naruto.dll 互相访问，但两个 dll 互不链接：
/// 彼此只 include 对方的 shared 头。

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>

#include <stdexec/execution.hpp>

#include "../../framework/shared_state.h"
#include "../../entity/fighting_scene/player_sdk.h"

#include "./ichigo_shared.h"
#include "../naruto/naruto_shared.h"   // ichigo 认识 naruto：只要它的 shared 头

struct ichigo
{
	::std::uint32_t _instance;
	::std::uint32_t _opponent;
	player_drawing _drawing;

	// 跨帧状态
	player_snapshot _snapshot{ .x = 2.0f, .facing = -1.0f };
	int _bankai_frames = 0;
	bool _getsuga_active = false;
	float _getsuga_x = 0.0f;
	float _getsuga_direction = 0.0f;

	// 本帧暂存
	ichigo_shared* _published = nullptr;
	float _move = 0.0f;
	bool _fire = false;

	ichigo(::std::uint32_t instance, ::std::uint32_t opponent, renderer& draw)
		: _instance(instance)
		, _opponent(opponent)
		, _drawing(draw)
	{}

	// ------------------------------------------------------------ 模拟

	[[nodiscard]] auto frame(frame_input in)
	{
		return ::publish(_instance,
			[this]
			{
				return ichigo_shared{
					.previous = _snapshot,
					.getsuga_active = _getsuga_active,
					.getsuga_x = _getsuga_x,
				};
			},
			[this, in](ichigo_shared& self, auto& registry)
			{
				_published = &self;
				return ::stdexec::read_env(::stdexec::get_scheduler)
					| ::stdexec::let_value([this, in, &self, &registry](auto scheduler)
						{
							return ::stdexec::schedule(scheduler)
								| ::stdexec::then([this, in, &self, &registry] { _think(in, self, registry); });
						});
			});
	}

	template <class Registry>
	void _think(frame_input in, ichigo_shared& self, Registry& registry)
	{
		auto const& current = *in.current;
		auto const& changes = *in.changes;

		_move = static_cast<float>(current.keyboard.key(input::key_code::right)) - static_cast<float>(current.keyboard.key(input::key_code::left));

		// L 卍解：120 帧。
		if (changes.keyboard.key(input::key_code::l).pressed && _bankai_frames == 0)
			_bankai_frames = 120;
		self.bankai.store(_bankai_frames > 0, ::std::memory_order_relaxed);

		// O 月牙天冲：同时只能有一发。道具在 settle 里推进，下一帧才对别人可见。
		_fire = changes.keyboard.key(input::key_code::o).pressed && !_getsuga_active;

		auto* enemy = ::find_shared<naruto_shared>(registry, _opponent);
		if (enemy == nullptr)
			return;

		// 面朝对手。
		_snapshot.facing = enemy->previous.x < _snapshot.x ? -1.0f : 1.0f;

		// K 斩击。
		auto const distance = enemy->previous.x - _snapshot.x;
		if (changes.keyboard.key(input::key_code::k).pressed && ::std::abs(distance) < 1.6f)
		{
			// naruto 正在蓄力：打断它，多一点击退。读到旧值只是晚一帧生效。
			auto const interrupt = enemy->charging.load(::std::memory_order_relaxed);
			enemy->inbox.push(hit_event{
				.attacker = _instance,
				.damage = _bankai_frames > 0 ? 14 : 9,
				.knockback = (interrupt ? 1.0f : 0.5f) * _snapshot.facing,
			});
		}

		// 月牙命中判定：本体或者任意一个分身挡住都算命中（naruto 的规则在它的 settle 里）。
		if (_getsuga_active)
		{
			auto blocked = ::std::abs(enemy->previous.x - _getsuga_x) < 0.5f;
			for (auto index = ::std::uint32_t{}; index < enemy->clone_count; ++index)
				blocked |= ::std::abs(enemy->clones[index] - _getsuga_x) < 0.5f;
			if (blocked)
			{
				enemy->inbox.push(hit_event{ .attacker = _instance, .damage = 18, .knockback = 1.2f * _getsuga_direction });
				_getsuga_active = false;   // 自己的跨帧状态：只有自己写
			}
		}
	}

	void settle(input::state const&)
	{
		assert(_published != nullptr);
		auto next = _published->previous;
		next.facing = _snapshot.facing;
		next.x = ::std::clamp(next.x + (_bankai_frames > 0 ? 0.09f : 0.06f) * _move, -4.5f, 4.5f);
		for (auto const& hit : _published->inbox.events())
		{
			next.hp -= _bankai_frames > 0 ? hit.damage / 2 : hit.damage;
			next.x = ::std::clamp(next.x + hit.knockback, -4.5f, 4.5f);
		}

		if (_fire)
		{
			_getsuga_active = true;
			_getsuga_x = next.x + 0.5f * next.facing;
			_getsuga_direction = next.facing;
		}
		else if (_getsuga_active)
		{
			_getsuga_x += 0.15f * _getsuga_direction;
			_getsuga_active = ::std::abs(_getsuga_x) < 5.0f;
		}

		if (_bankai_frames > 0)
			--_bankai_frames;
		_snapshot = next;
		_published = nullptr;
	}

	// ------------------------------------------------------------ 绘制

	[[nodiscard]] auto record(frame_slot&, command_list& list)
	{
		return ::stdexec::read_env(::stdexec::get_scheduler)
			| ::stdexec::let_value([this, &list](auto scheduler)
				{
					return ::stdexec::schedule(scheduler)
						| ::stdexec::then([this, &list]
							{
								list.put(_instance, _drawing.record([this](point viewport, ::glm::mat3 const& transform)
									{
										auto const body = _bankai_frames > 0 ? color{ 0.08f, 0.08f, 0.1f, 1.0f } : color{ 0.2f, 0.3f, 0.8f, 1.0f };
										_drawing.body(viewport, transform, _snapshot, body);
										if (_getsuga_active)
											_drawing.rectangle(viewport, transform, _getsuga_x, 0.2f, { 0.25f, 0.9f }, { 0.6f, 0.9f, 1.0f, 0.85f });
									}));
							});
				});
	}

	[[nodiscard]] auto snapshot() const noexcept -> player_snapshot const& { return _snapshot; }
};

BVN_EXPORT_PLAYER(ichigo)
