#include <cassert>
#include <memory>
#include <typeindex>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

#include <entt/entt.hpp>

#include "./dynamic_when_all.h"



struct frame_context
{
	struct entity_control_block
	{
		void* object{};
		void (*build_task)(frame_context&, ::entt::registry::context&){};
		bool is_built{};
		::entt::registry tasks{};
	};

	::std::vector<entity_control_block> _entities{};

	template <class Entity>
	auto entity() -> ::entity<Object>*
	{
		auto found = _by_type.find(typeid(Object));
		return found == _by_type.end() ? nullptr : static_cast<::entity<Object>*>(found->second);
	}

	template <class Object>
	auto _append(Object& object) -> void
	{
		auto slot = ::std::make_unique<::entity<Object>>();
		slot->_frame = this;
		slot->_obj = &object;
		_by_type.emplace(typeid(Object), slot.get());
		_entities.push_back(::std::move(slot));
	}
};

auto build(frame_context& context) -> void
{
	for (auto&& slot : context._entities)
		if (!slot->task_built())
			slot->build_task();
}

auto run(frame_context&) -> void
{
}

int main()
{
}
