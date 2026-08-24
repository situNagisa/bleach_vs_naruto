"""Wrappers for vkCreate* / vkAllocate*, taking an expression where Vulkan takes
a create-info.

Every one of them has the same skeleton -- handles the caller already has, one
structure the caller fills in, an optional allocator, and the handle produced --
so there are only three shapes to render. Each gets two overloads, following
vkkl: one that throws the VkResult, and one taking std::nothrow_t that returns
std::expected.
"""

from __future__ import annotations

from . import ir
from .naming import Table

TAB = "\t"


def shape_of(command: ir.Command) -> str:
	if command.info_is_array:
		return "array"
	if command.out_from_info:
		return "counted"
	return "single"


def _guard_open(lines: list[str], guards: list[str]) -> None:
	if guards:
		lines.append("#if " + " || ".join(f"defined({guard})" for guard in guards))


def _guard_close(lines: list[str], guards: list[str]) -> None:
	if guards:
		lines.append("#endif")


def _leading(command: ir.Command) -> tuple[str, str]:
	"""(parameter list, argument list) for the handles the caller already has."""
	declaration = "".join(f"{member.type} {member.name}, " for member in command.leading)
	arguments = "".join(f"{member.name}, " for member in command.leading)
	return declaration, arguments


def _single(command: ir.Command, function: str, tag: str) -> list[str]:
	declare, pass_on = _leading(command)
	handle = command.out.type
	allocator = ", ::VkAllocationCallbacks const* allocation_callbacks" if command.allocator else ""
	allocator_argument = ", allocation_callbacks" if command.allocator else ""
	return [
		f"template<::vkfu::expression_for<{tag}> Expression>",
		f"[[nodiscard]] auto {function}({declare}Expression&& info{allocator}, ::std::nothrow_t) noexcept",
		f"{TAB}-> ::std::expected<{handle}, ::VkResult>",
		"{",
		f"{TAB}decltype(auto) storage = ::vkfu::evaluate(::std::forward<Expression>(info));",
		f"{TAB}auto produced = {handle}{{}};",
		f"{TAB}auto const result = ::{command.name}(",
		f"{TAB}{TAB}{pass_on}::std::addressof(::vkfu::unpack(storage)){allocator_argument}, ::std::addressof(produced));",
		f"{TAB}if (result != ::VK_SUCCESS)",
		f"{TAB}{{",
		f"{TAB}{TAB}return ::std::unexpected{{result}};",
		f"{TAB}}}",
		f"{TAB}return produced;",
		"}",
		"",
		f"template<::vkfu::expression_for<{tag}> Expression>",
		f"[[nodiscard]] auto {function}({declare}Expression&& info"
		+ (", ::VkAllocationCallbacks const* allocation_callbacks = nullptr" if command.allocator else "")
		+ f") -> {handle}",
		"{",
		f"{TAB}auto produced = {function}({pass_on}::std::forward<Expression>(info)"
		+ (", allocation_callbacks" if command.allocator else "")
		+ ", ::std::nothrow);",
		f"{TAB}if (!produced)",
		f"{TAB}{{",
		f"{TAB}{TAB}throw produced.error();",
		f"{TAB}}}",
		f"{TAB}return *produced;",
		"}",
	]


def _counted(command: ir.Command, function: str, tag: str) -> list[str]:
	"""The out array's length lives inside the structure, so the caller sizes it."""
	declare, pass_on = _leading(command)
	handle = command.out.type
	return [
		f"// The count comes from the structure ({command.out.length}), so `produced`",
		"// has to be sized to match it.",
		f"template<::vkfu::expression_for<{tag}> Expression>",
		f"[[nodiscard]] auto {function}({declare}Expression&& info, ::std::span<{handle}> produced, ::std::nothrow_t) noexcept",
		f"{TAB}-> ::std::expected<void, ::VkResult>",
		"{",
		f"{TAB}decltype(auto) storage = ::vkfu::evaluate(::std::forward<Expression>(info));",
		f"{TAB}auto const result = ::{command.name}({pass_on}::std::addressof(::vkfu::unpack(storage)), produced.data());",
		f"{TAB}if (result != ::VK_SUCCESS)",
		f"{TAB}{{",
		f"{TAB}{TAB}return ::std::unexpected{{result}};",
		f"{TAB}}}",
		f"{TAB}return {{}};",
		"}",
		"",
		f"template<::vkfu::expression_for<{tag}> Expression>",
		f"void {function}({declare}Expression&& info, ::std::span<{handle}> produced)",
		"{",
		f"{TAB}auto const outcome = {function}({pass_on}::std::forward<Expression>(info), produced, ::std::nothrow);",
		f"{TAB}if (!outcome)",
		f"{TAB}{{",
		f"{TAB}{TAB}throw outcome.error();",
		f"{TAB}}}",
		"}",
	]


def _array(command: ir.Command, function: str, singular: str, tag: str) -> list[str]:
	"""One command call for as many structures as the caller passes."""
	declare, pass_on = _leading(command)
	handle = command.out.type
	native = command.info.type
	constraint = (
		"template<class... Expressions>\n"
		f"{TAB}requires (sizeof...(Expressions) != 0 && (::vkfu::expression_for<Expressions, {tag}> && ...))"
	)
	lines = [
		constraint,
		f"[[nodiscard]] auto {function}({declare}::VkAllocationCallbacks const* allocation_callbacks,",
		f"{TAB}::std::nothrow_t, Expressions&&... infos) noexcept",
		f"{TAB}-> ::std::expected<::std::array<{handle}, sizeof...(Expressions)>, ::VkResult>",
		"{",
		# The natives keep pointing into `storages`, which outlives the call.
		f"{TAB}auto storages = ::std::tuple{{::vkfu::evaluate(::std::forward<Expressions>(infos))...}};",
		f"{TAB}auto const natives = [&]<::std::size_t... Indices>(::std::index_sequence<Indices...>)",
		f"{TAB}{{",
		f"{TAB}{TAB}return ::std::array<{native}, sizeof...(Expressions)>{{::vkfu::unpack(::std::get<Indices>(storages))...}};",
		f"{TAB}}}(::std::index_sequence_for<Expressions...>{{}});",
		f"{TAB}auto produced = ::std::array<{handle}, sizeof...(Expressions)>{{}};",
		f"{TAB}auto const result = ::{command.name}(",
		f"{TAB}{TAB}{pass_on}static_cast<::std::uint32_t>(natives.size()), natives.data(),",
		f"{TAB}{TAB}allocation_callbacks, produced.data());",
		f"{TAB}if (result != ::VK_SUCCESS)",
		f"{TAB}{{",
		f"{TAB}{TAB}return ::std::unexpected{{result}};",
		f"{TAB}}}",
		f"{TAB}return produced;",
		"}",
		"",
		constraint,
		f"[[nodiscard]] auto {function}({declare}::VkAllocationCallbacks const* allocation_callbacks,",
		f"{TAB}Expressions&&... infos) -> ::std::array<{handle}, sizeof...(Expressions)>",
		"{",
		f"{TAB}auto produced = {function}({pass_on}allocation_callbacks, ::std::nothrow, ::std::forward<Expressions>(infos)...);",
		f"{TAB}if (!produced)",
		f"{TAB}{{",
		f"{TAB}{TAB}throw produced.error();",
		f"{TAB}}}",
		f"{TAB}return *produced;",
		"}",
		"",
		f"// One structure is the common case, and reads better than a one-element array.",
		f"template<::vkfu::expression_for<{tag}> Expression>",
		f"[[nodiscard]] auto {singular}({declare}Expression&& info, ::VkAllocationCallbacks const* allocation_callbacks = nullptr)",
		f"{TAB}-> {handle}",
		"{",
		f"{TAB}return {function}({pass_on}allocation_callbacks, ::std::forward<Expression>(info))[0];",
		"}",
		"",
		f"template<::vkfu::expression_for<{tag}> Expression>",
		f"[[nodiscard]] auto {singular}({declare}Expression&& info, ::VkAllocationCallbacks const* allocation_callbacks, ::std::nothrow_t) noexcept",
		f"{TAB}-> ::std::expected<{handle}, ::VkResult>",
		"{",
		f"{TAB}auto produced = {function}({pass_on}allocation_callbacks, ::std::nothrow, ::std::forward<Expression>(info));",
		f"{TAB}if (!produced)",
		f"{TAB}{{",
		f"{TAB}{TAB}return ::std::unexpected{{produced.error()}};",
		f"{TAB}}}",
		f"{TAB}return (*produced)[0];",
		"}",
	]
	return lines


def render(command: ir.Command, table: Table) -> list[str]:
	tag = f"::vkfu::obj::{table.struct_name(command.info.type)}"
	function = table.command_name(command.name).rpartition("::")[2]
	lines: list[str] = []
	_guard_open(lines, command.guards)
	shape = shape_of(command)
	if shape == "single":
		lines.extend(_single(command, function, tag))
	elif shape == "counted":
		lines.extend(_counted(command, function, tag))
	else:
		lines.extend(_array(command, function, table.command_singular(command.name).rpartition("::")[2], tag))
	_guard_close(lines, command.guards)
	return lines
