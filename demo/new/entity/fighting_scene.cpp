#include <algorithm>
#include <ranges>
#include <stdexcept>
#include <utility>

#include <stdexec/execution.hpp>

#include <nagisa/concurrency/when_all_range.h>

#include "./fighting_scene.h"

namespace
{
	/// 一步：每个参与者一个 sender，各自调度到 worker 上，when_all_range 等全部完成。
	///
	/// 用 `schedule | let_value` 而不是 `starts_on`：结果要擦除成 values_sender_type，
	/// 而"带查询的 any_sender + starts_on / continues_on"会撞 stdexec 的上游 bug
	/// （见 demo/job-arch/node_sender.h 的 env_gate_sender）。
	template <class Run>
	auto parallel(any_scheduler_type const& workers, ::std::span<fighting_entity* const> members, Run run)
	{
		return ::nagisa::concurrency::when_all_range(members
			| ::std::views::transform([&workers, run](fighting_entity* member)
				{
					return ::stdexec::schedule(workers)
						| ::stdexec::let_value([run, member] { return run(*member); });
				}));
	}
}

fighting_scene::fighting_scene(input& controls, renderer& draw, module_cache& modules, any_scheduler_type workers)
	: _controls(controls)
	, _draw(draw)
	, _modules(modules)
	, _workers(::std::move(workers))
{
	// 开场：同一个模块里的两种 player。
	auto const fighters = module_path("plugins", "fighter");
	request_spawn(fighters, "naruto");
	request_spawn(fighters, "ichigo");
	apply_structural_changes();
}

values_sender_type<fighting_scene::state> fighting_scene::update(app_state::fighting_type& shared, frame_clock::frame_time const& time, input::state const& current, input::delta const& changes)
{
	return ::stdexec::just() | ::stdexec::let_value([&]
		{
			shared.back_requested = changes.keyboard.key(input::key_code::escape).pressed;   // 本帧输出：整体覆盖
			_frame = frame_view{ .time = &time, .current = &current, .changes = &changes, .registry = &_registry };

			// publish：同步、按 instance 序。封存之后才进入并发阶段。
			_registry.clear();
			for (auto&& entry : _entities)
				entry.entity->publish(_registry);
			_registry.seal();

			return parallel(_workers, _updating, [this](fighting_entity& entity) { return entity.update(_frame); })
				| ::stdexec::let_value([this]
					{
						return parallel(_workers, _settling, [this](fighting_entity& entity) { return entity.settle(_frame); });
					})
				| ::stdexec::then([] { return state{}; });
		});
}

values_sender_type<fighting_scene::recording> fighting_scene::record(frame_slot& slot, state const&)
{
	return ::stdexec::just() | ::stdexec::let_value([this, &slot]
		{
			::std::ranges::fill(_commands, VK_NULL_HANDLE);
			// 每个参与者写 _commands 里属于自己的那一格：互不加锁。
			return ::nagisa::concurrency::when_all_range(::std::views::iota(::std::size_t{}, _recording.size())
					| ::std::views::transform([this, &slot](::std::size_t index)
						{
							return ::stdexec::schedule(_workers)
								| ::stdexec::let_value([this, &slot, index] { return _recording[index]->record(slot, _commands[index]); });
						}))
				| ::stdexec::then([this]
					{
						// 挤掉本帧没画的空槽；剩下的顺序就是 instance 序 = 绘制序。
						auto const kept = ::std::ranges::remove(_commands, VK_NULL_HANDLE);
						auto const count = static_cast<::std::size_t>(kept.begin() - _commands.begin());
						return recording{ ._commands = ::std::span{ _commands }.first(count) };
					});
		});
}

auto fighting_scene::request_spawn(::std::filesystem::path module, ::std::string kind) -> ::std::uint32_t
{
	auto const instance = _next_instance++;
	_pending_spawns.push_back({ .instance = instance, .module = ::std::move(module), .kind = ::std::move(kind), .entity = nullptr });
	return instance;
}

auto fighting_scene::request_spawn(::std::unique_ptr<fighting_entity> entity) -> ::std::uint32_t
{
	auto const instance = _next_instance++;
	_pending_spawns.push_back({ .instance = instance, .module = {}, .kind = {}, .entity = ::std::move(entity) });
	return instance;
}

void fighting_scene::request_despawn(::std::uint32_t instance)
{
	_pending_despawns.push_back(instance);
}

void fighting_scene::apply_structural_changes()
{
	if (_pending_spawns.empty() && _pending_despawns.empty())
		return;

	// 先 despawn：entity 析构，但 dll 由 module_cache 留到下一帧开头才可能卸载。
	for (auto const instance : ::std::exchange(_pending_despawns, {}))
		::std::erase_if(_entities, [instance](slot const& entry) { return entry.instance == instance; });

	for (auto&& pending : ::std::exchange(_pending_spawns, {}))
	{
		auto entry = slot{ .instance = pending.instance, .module = nullptr, .entity = ::std::move(pending.entity) };
		if (entry.entity == nullptr)
		{
			entry.module = _modules.load(pending.module);
			entry.entity = entry.module->create(pending.kind, entity_context{ .instance = pending.instance, .draw = &_draw, .controls = &_controls });
			if (entry.entity == nullptr)
				throw ::std::runtime_error{ "fighting_scene: " + pending.module.string() + " 不认识 kind " + pending.kind };
		}
		_entities.push_back(::std::move(entry));   // instance 单调递增，追加即保持有序
	}

	_updating.clear();
	_settling.clear();
	_recording.clear();
	for (auto&& entry : _entities)
	{
		auto const phases = entry.entity->phases();
		if (has(phases, phase::update))
			_updating.push_back(entry.entity.get());
		if (has(phases, phase::settle))
			_settling.push_back(entry.entity.get());
		if (has(phases, phase::record))
			_recording.push_back(entry.entity.get());
	}
	_commands.resize(_recording.size());
}
