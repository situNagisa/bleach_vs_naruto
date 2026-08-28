"""The read side: two-call enumerates, and the commands that fill in a chain.

Two shapes, both mechanical:

  * a count the command writes and an array it then fills becomes a `count_*`
    query plus a write-into-the-caller's-storage call. Nothing is allocated, so
    the header needs no container at all;
  * a single sType out parameter becomes a `query_chain`, so the caller says
    which extra structures to have filled in and gets them back linked.

The output of an enumerate has to be contiguous, because the driver writes
through a raw pointer -- that is why the iterator overload asks for
`contiguous_iterator` rather than `output_iterator`. Its bounds contract is
`std::ranges::copy`'s: the caller guarantees the room.
"""

from __future__ import annotations

from . import commands, ir
from .naming import Table

TAB = "\t"


def _identifier(member: ir.Member) -> str:
	return commands._identifier(member)


def _type(name: str) -> str:
	return commands._type(name)


def _classified(members: list[ir.Member], registry: ir.Registry) -> list[ir.Argument]:
	"""Leading parameters: a const sType pointer is an expression, the rest pass through."""
	arguments: list[ir.Argument] = []
	for member in members:
		target = registry.structs.get(member.type)
		if member.ptr_depth == 1 and member.elem_const and target is not None and target.stype is not None:
			arguments.append(ir.Argument(kind="info", member=member))
		else:
			arguments.append(ir.Argument(kind="passthrough", member=member))
	return arguments


def _signature(arguments: list[ir.Argument], table: Table) -> tuple[list[str], list[str]]:
	templates: list[str] = []
	parameters: list[str] = []
	index = 0
	for argument in arguments:
		member = argument.member
		name = _identifier(member)
		if argument.kind == "info":
			index += 1
			tag = f"::vkfu::obj::{table.struct_name(member.type)}"
			templates.append(f"::vkfu::expression_for<{tag}> Expression{index}")
			parameters.append(f"Expression{index}&& {name}")
		elif argument.kind == "allocator":
			parameters.append("::VkAllocationCallbacks const* allocation_callbacks = nullptr")
		else:
			parameters.append(f"{commands._declared(member)} {name}")
	return templates, parameters


def _evaluations(arguments: list[ir.Argument]) -> list[str]:
	lines: list[str] = []
	index = 0
	for argument in arguments:
		if argument.kind != "info":
			continue
		index += 1
		name = _identifier(argument.member)
		lines.append(
			f"{TAB}decltype(auto) storage{index} = ::vkfu::evaluate(::std::forward<Expression{index}>({name}));"
		)
	return lines


def _call(params: list[ir.Member], arguments: list[ir.Argument], substitutions: dict[str, str]) -> str:
	kinds = {argument.member.name: argument.kind for argument in arguments}
	rendered: list[str] = []
	index = 0
	for member in params:
		if member.name in substitutions:
			rendered.append(substitutions[member.name])
			continue
		if kinds.get(member.name) == "info":
			index += 1
			rendered.append(f"::std::addressof(::vkfu::unpack(storage{index}))")
		elif kinds.get(member.name) == "allocator":
			rendered.append("allocation_callbacks")
		else:
			rendered.append(_identifier(member))
	return ", ".join(rendered)


# ---------------------------------------------------------------- enumerate


def render_query(query: ir.Query, table: Table, registry: ir.Registry) -> list[str]:
	"""count_* to ask how many, then one call that writes where you say."""
	function = table.command_name(query.name).rpartition("::")[2]
	counter = table.command_count(query.name).rpartition("::")[2]
	arguments = _classified(query.leading, registry)
	templates, parameters = _signature(arguments, table)
	prefix = f"{', '.join(templates)}" if templates else ""

	# `void* pData` is an opaque blob sized in bytes -- a pipeline cache, a shader
	# binary. std::byte is what that is, and it keeps the cast at the boundary.
	opaque = query.out.type == "void"
	element = "::std::byte" if opaque else _type(query.out.type)
	count_type = _type(query.count.type)
	fails = query.returns == "VkResult"

	target = registry.structs.get(query.out.type)
	stype = target.stype if target is not None and target.stype is not None else None

	def declare(extra: list[str], template: bool | None = None) -> list[str]:
		"""Signature lines shared by every overload of this command."""
		use_template = bool(templates) if template is None else template
		head = [f"template<{prefix}>"] if use_template and prefix else []
		return head, "" if use_template or templates else "inline "

	lines: list[str] = []
	commands._guard_open(lines, query.guards)

	# ---------------------------------------------------------- count_*
	probe = _call(
		query.params, arguments, {query.count.name: "::std::addressof(count)", query.out.name: "nullptr"}
	)
	head, linkage = declare([])
	if fails:
		lines += head
		lines.append(
			f"[[nodiscard]] {linkage}auto {counter}({', '.join(parameters + ['::std::nothrow_t'])})"
			+ ("" if templates else " noexcept")
		)
		lines.append(f"{TAB}-> ::std::expected<{count_type}, ::VkResult>")
		lines.append("{")
		lines.extend(_evaluations(arguments))
		lines.append(f"{TAB}auto count = {count_type}{{}};")
		lines.append(f"{TAB}if (auto const outcome = ::{query.name}({probe}); outcome != ::VK_SUCCESS)")
		lines.append(f"{TAB}{{")
		lines.append(f"{TAB}{TAB}return ::std::unexpected{{outcome}};")
		lines.append(f"{TAB}}}")
		lines.append(f"{TAB}return count;")
		lines.append("}")
		lines.append("")
		forwarded = ", ".join(commands._forwarded_names(arguments))
		lines += head
		lines.append(f"[[nodiscard]] {linkage}auto {counter}({', '.join(parameters)}) -> {count_type}")
		lines.append("{")
		lines.append(f"{TAB}auto const outcome = {counter}({forwarded}{', ' if forwarded else ''}::std::nothrow);")
		lines.append(f"{TAB}if (!outcome)")
		lines.append(f"{TAB}{{")
		lines.append(f"{TAB}{TAB}throw outcome.error();")
		lines.append(f"{TAB}}}")
		lines.append(f"{TAB}return *outcome;")
		lines.append("}")
	else:
		lines += head
		quiet = "" if templates else " noexcept"
		lines.append(f"[[nodiscard]] {linkage}auto {counter}({', '.join(parameters)}){quiet} -> {count_type}")
		lines.append("{")
		lines.extend(_evaluations(arguments))
		lines.append(f"{TAB}auto count = {count_type}{{}};")
		lines.append(f"{TAB}::{query.name}({probe});")
		lines.append(f"{TAB}return count;")
		lines.append("}")
	lines.append("")

	# ---------------------------------------------------------- span overload
	span_parameters = parameters + [f"::std::span<{element}> out"]
	fetch = _call(
		query.params,
		arguments,
		{
			query.count.name: "::std::addressof(count)",
			query.out.name: "static_cast<void*>(out.data())" if opaque else "out.data()",
		},
	)
	stamp = []
	if stype is not None:
		stamp = [
			f"{TAB}for (auto& entry : out)",
			f"{TAB}{{",
			f"{TAB}{TAB}entry.sType = {stype};",
			f"{TAB}{TAB}entry.pNext = nullptr;",
			f"{TAB}}}",
		]

	head, linkage = declare([])
	if fails:
		lines += head
		lines.append(
			f"[[nodiscard]] {linkage}auto {function}({', '.join(span_parameters + ['::std::nothrow_t'])})"
			+ ("" if templates else " noexcept")
		)
		lines.append(f"{TAB}-> ::std::expected<::std::span<{element}>, ::VkResult>")
		lines.append("{")
		lines.extend(_evaluations(arguments))
		lines.extend(stamp)
		lines.append(f"{TAB}auto count = static_cast<{count_type}>(out.size());")
		lines.append(f"{TAB}auto const outcome = ::{query.name}({fetch});")
		# VK_INCOMPLETE is not a failure here: it says the span was filled and
		# there was more. Ask count_* first if that matters.
		lines.append(f"{TAB}if (outcome != ::VK_SUCCESS && outcome != ::VK_INCOMPLETE)")
		lines.append(f"{TAB}{{")
		lines.append(f"{TAB}{TAB}return ::std::unexpected{{outcome}};")
		lines.append(f"{TAB}}}")
		lines.append(f"{TAB}return out.first(count);")
		lines.append("}")
		lines.append("")
		forwarded = ", ".join(commands._forwarded_names(arguments) + ["out"])
		lines += head
		lines.append(
			f"[[nodiscard]] {linkage}auto {function}({', '.join(span_parameters)}) -> ::std::span<{element}>"
		)
		lines.append("{")
		lines.append(f"{TAB}auto const outcome = {function}({forwarded}, ::std::nothrow);")
		lines.append(f"{TAB}if (!outcome)")
		lines.append(f"{TAB}{{")
		lines.append(f"{TAB}{TAB}throw outcome.error();")
		lines.append(f"{TAB}}}")
		lines.append(f"{TAB}return *outcome;")
		lines.append("}")
	else:
		lines += head
		lines.append(
			f"[[nodiscard]] {linkage}auto {function}({', '.join(span_parameters)})"
			+ ("" if templates else " noexcept")
			+ f" -> ::std::span<{element}>"
		)
		lines.append("{")
		lines.extend(_evaluations(arguments))
		lines.extend(stamp)
		lines.append(f"{TAB}auto count = static_cast<{count_type}>(out.size());")
		lines.append(f"{TAB}::{query.name}({fetch});")
		lines.append(f"{TAB}return out.first(count);")
		lines.append("}")
	lines.append("")

	# ---------------------------------------------------------- iterator overload
	# std::ranges::copy's contract: the caller guarantees the room. Contiguous
	# rather than merely output, because the driver writes through a pointer.
	iterator_templates = templates + [
		f"::std::contiguous_iterator Out"
	]
	iterator_parameters = parameters + ["Out out"]
	forwarded = ", ".join(commands._forwarded_names(arguments))
	lines.append(f"template<{', '.join(iterator_templates)}>")
	lines.append(f"{TAB}requires ::std::same_as<::std::iter_value_t<Out>, {element}>")
	lines.append(f"[[nodiscard]] auto {function}({', '.join(iterator_parameters)}) -> Out")
	lines.append("{")
	lines.append(
		f"{TAB}auto const count = {counter}({forwarded});"
	)
	lines.append(
		f"{TAB}auto const written = {function}({forwarded}{', ' if forwarded else ''}"
		f"::std::span<{element}>{{::std::to_address(out), count}});"
	)
	lines.append(f"{TAB}return out + static_cast<::std::iter_difference_t<Out>>(written.size());")
	lines.append("}")

	commands._guard_close(lines, query.guards)
	return lines


# ---------------------------------------------------------------- chain


def render_chain_query(query: ir.ChainQuery, table: Table, registry: ir.Registry) -> list[str]:
	"""One sType out parameter, turned into a chain the caller shapes."""
	function = table.command_name(query.name).rpartition("::")[2]
	head = f"::vkfu::obj::{table.struct_name(query.out.type)}"
	templates, parameters = _signature(query.arguments, table)
	chain = f"::vkfu::query_chain<{head}, Extras...>"

	# The pack goes first: everything after it is deduced from the call, which is
	# what lets the caller name the extras explicitly and nothing else.
	declarations = [f"::vkfu::query_extension_of<{head}>... Extras"] + templates
	template_line = f"template<{', '.join(declarations)}>"
	substitutions = {query.out.name: "::std::addressof(chain.head())"}
	call = _call(query.params, query.arguments, substitutions)

	lines: list[str] = []
	commands._guard_open(lines, query.guards)

	if query.returns == "VkResult":
		lines.append(template_line)
		lines.append(
			f"[[nodiscard]] auto {function}({', '.join(parameters + ['::std::nothrow_t'])})"
		)
		lines.append(f"{TAB}-> ::std::expected<{chain}, ::VkResult>")
		lines.append("{")
		lines.extend(_evaluations(query.arguments))
		lines.append(f"{TAB}auto chain = {chain}{{}};")
		lines.append(f"{TAB}if (auto const outcome = ::{query.name}({call}); outcome != ::VK_SUCCESS)")
		lines.append(f"{TAB}{{")
		lines.append(f"{TAB}{TAB}return ::std::unexpected{{outcome}};")
		lines.append(f"{TAB}}}")
		lines.append(f"{TAB}return chain;")
		lines.append("}")
		lines.append("")

		lines.append(template_line)
		forwarded = ", ".join(commands._forwarded_names(query.arguments))
		lines.append(f"[[nodiscard]] auto {function}({', '.join(parameters)}) -> {chain}")
		lines.append("{")
		lines.append(
			f"{TAB}auto outcome = {function}<Extras...>({forwarded}{', ' if forwarded else ''}::std::nothrow);"
		)
		lines.append(f"{TAB}if (!outcome)")
		lines.append(f"{TAB}{{")
		lines.append(f"{TAB}{TAB}throw outcome.error();")
		lines.append(f"{TAB}}}")
		lines.append(f"{TAB}return *::std::move(outcome);")
		lines.append("}")
	else:
		lines.append(template_line)
		lines.append(f"[[nodiscard]] auto {function}({', '.join(parameters)}) -> {chain}")
		lines.append("{")
		lines.extend(_evaluations(query.arguments))
		lines.append(f"{TAB}auto chain = {chain}{{}};")
		lines.append(f"{TAB}::{query.name}({call});")
		lines.append(f"{TAB}return chain;")
		lines.append("}")

	commands._guard_close(lines, query.guards)
	return lines
