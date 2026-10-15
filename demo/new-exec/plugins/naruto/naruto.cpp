/// naruto：一个独立的 player 插件（naruto.dll）。
///
/// 写法就是一个普通 entity：拿 renderer 自己画、从 env 拿 scheduler、用 registry 查别人。
/// 跟宿主唯一的约定是 player_sdk.h 里那个形状，加上最后一行导出宏。

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>

#include <stdexec/execution.hpp>

#include "../../framework/shared_state.h"
#include "../../entity/fighting_scene/player_sdk.h"

#include "./naruto_shared.h"
#include "../ichigo/ichigo_shared.h"   // naruto 认识 ichigo：只要它的 shared 头

struct naruto
{
	::std::uint32_t _instance;
	::std::uint32_t _opponent;
	player_drawing _drawing;

	// 跨帧状态
	player_snapshot _snapshot{ .x = -2.0f, .facing = 1.0f };
	::std::array<float, 2> _clones{};
	::std::uint32_t _clone_count = 0;
	int _chakra = 0;

	// 本帧暂存：只在 think（本帧并发阶段）里自己写、settle 里自己读
	naruto_shared* _published = nullptr;
	float _move = 0.0f;
	bool _summon = false;

	naruto(::std::uint32_t instance, ::std::uint32_t opponent, renderer& draw)
		: _instance(instance)
		, _opponent(opponent)
		, _drawing(draw)
	{}

	// ------------------------------------------------------------ 模拟

	[[nodiscard]] auto frame(frame_input in)
	{
		// publish 在最外层：connect 时就登记，早于任何 player start。
		return ::publish(_instance,
			[this]
			{
				return naruto_shared{
					.previous = _snapshot,
					.clones = _clones,
					.clone_count = _clone_count,
				};
			},
			[this, in](naruto_shared& self, auto& registry)
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
	void _think(frame_input in, naruto_shared& self, Registry& registry)
	{
		auto const& current = *in.current;
		auto const& changes = *in.changes;

		_move = static_cast<float>(current.keyboard.key(input::key_code::d)) - static_cast<float>(current.keyboard.key(input::key_code::a));

		// S 按住蓄查克拉。意图写成 atomic，ichigo 可以据此决定要不要来打断。
		auto const charging = current.keyboard.key(input::key_code::s) && _move == 0.0f;
		self.charging.store(charging, ::std::memory_order_relaxed);
		if (charging)
			_chakra = (::std::min)(_chakra + 1, 120);

		// U 放影分身：消耗查克拉。分身在 settle 里生成，下一帧才对别人可见。
		_summon = changes.keyboard.key(input::key_code::u).pressed && _chakra >= 60 && _clone_count < 2;

		auto* enemy = ::find_shared<ichigo_shared>(registry, _opponent);
		if (enemy == nullptr)
			return;   // 对手这帧不在：照样走路、蓄力，不出招

		// J 普攻。距离用上一帧的位置算：确定性。
		auto const distance = enemy->previous.x - _snapshot.x;
		if (changes.keyboard.key(input::key_code::j).pressed && ::std::abs(distance) < 1.2f)
		{
			// 读本帧意图：卍解中的 ichigo 吃不到额外伤害。非确定性，只影响手感。
			auto const bonus = enemy->bankai.load(::std::memory_order_relaxed) ? 0 : 2;
			enemy->inbox.push(hit_event{ .attacker = _instance, .damage = 6 + bonus, .knockback = 0.4f * _snapshot.facing });
		}

		// 分身贴身时自动补一刀。
		for (auto index = ::std::uint32_t{}; index < _clone_count; ++index)
		{
			if (::std::abs(enemy->previous.x - _clones[index]) < 0.8f)
				enemy->inbox.push(hit_event{ .attacker = _instance, .damage = 2, .knockback = 0.1f * _snapshot.facing });
		}

		// 看见月牙天冲过来了：蓄力被打断（不靠伤害，靠读 ichigo 公开的道具位置）。
		if (enemy->getsuga_active && ::std::abs(enemy->getsuga_x - _snapshot.x) < 1.0f)
			_chakra = 0;
	}

	void settle(input::state const&)
	{
		assert(_published != nullptr);
		auto next = _published->previous;
		next.x = ::std::clamp(next.x + 0.06f * _move, -4.5f, 4.5f);

		auto clones_hit = false;
		for (auto const& hit : _published->inbox.events())
		{
			// 打中分身就只消掉一个分身，本体不掉血。
			if (_clone_count > 0)
			{
				--_clone_count;
				clones_hit = true;
				continue;
			}
			next.hp -= hit.damage;
			next.x = ::std::clamp(next.x + hit.knockback, -4.5f, 4.5f);
		}

		if (_summon && !clones_hit)
		{
			_clones[_clone_count++] = next.x + 0.8f * next.facing * static_cast<float>(_clone_count + 1);
			_chakra -= 60;
		}
		for (auto index = ::std::uint32_t{}; index < _clone_count; ++index)
			_clones[index] = ::std::clamp(_clones[index] + 0.06f * _move, -4.5f, 4.5f);

		_snapshot = next;
		_published = nullptr;   // 共享状态马上随 operation_state 析构
	}

	// ------------------------------------------------------------ 绘制

	/// 跑在 settle 之后，画的是本帧结算后的结果。
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
										auto const orange = color{ 0.98f, 0.55f, 0.1f, 1.0f };
										for (auto index = ::std::uint32_t{}; index < _clone_count; ++index)
											_drawing.rectangle(viewport, transform, _clones[index], 0.0f, { 0.6f, 1.2f }, { 0.98f, 0.55f, 0.1f, 0.45f });
										_drawing.body(viewport, transform, _snapshot, orange);
										// 查克拉条
										auto const chakra = static_cast<float>(_chakra) / 120.0f;
										_drawing.rectangle(viewport, transform, _snapshot.x - 0.3f * (1.0f - chakra), 1.25f, { 0.6f * chakra, 0.05f }, { 0.2f, 0.6f, 1.0f, 1.0f });
									}));
							});
				});
	}

	[[nodiscard]] auto snapshot() const noexcept -> player_snapshot const& { return _snapshot; }
};

BVN_EXPORT_PLAYER(naruto)
