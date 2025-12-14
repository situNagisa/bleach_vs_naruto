
#include <string>
#include <unordered_map>
#include <vector>

#include <Windows.h>

#include "bvn/module_loader/detail/api.h"


NAGISA_BUILD_LIB_DETAIL_BEGIN
namespace
{
	auto to_dll_name(module_id id)
	{
		return ::std::string(id.vendor) + "-" + ::std::string(id.name) + ".dll";
	}

	::std::unordered_map<::std::string, ::std::vector<void const*>> module_table{};
}

bool is_loaded(module_id id) noexcept
{
	return module_table.contains(details::to_dll_name(id));
}

::std::span<void const*> load(module_id id, ::std::size_t n) noexcept
{
	auto[it, success] = module_table.emplace(details::to_dll_name(id), ::std::vector<void const*>(n, nullptr));
	return it->second;
}

void unload(module_id id) noexcept
{
	
}

NAGISA_BUILD_LIB_DETAIL_END