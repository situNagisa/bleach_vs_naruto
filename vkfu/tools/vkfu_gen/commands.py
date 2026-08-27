"""Wrappers for the commands that are not vkCreate* / vkAllocate*.

Two substitutions, both mechanical:

  * a `const VkXxxInfo*` parameter becomes an expression, evaluated and unpacked
    at the call;
  * a pointer paired with its own count becomes one `std::span`, and the count
    argument comes from `.size()`.

Parameter *names* are the vk.xml ones, de-Hungarianised and snake_cased. They
are not part of the callable API -- C++ has no named arguments -- so unlike
every other name in the generated header they do not go through the table.
"""

from __future__ import annotations

from . import ir, naming
from .naming import Table

TAB = "\t"

# Commands returning one of these hand the value straight back; anything else
# returning VkResult gets the expected/throwing pair.
SCALAR_RETURNS = frozenset({"uint32_t", "uint64_t", "VkDeviceAddress", "VkBool32", "size_t"})

PRIMITIVES = {
	"uint8_t": "::std::uint8_t",
	"uint16_t": "::std::uint16_t",
	"uint32_t": "::std::uint32_t",
	"uint64_t": "::std::uint64_t",
	"int32_t": "::std::int32_t",
	"int64_t": "::std::int64_t",
	"size_t": "::std::size_t",
}


def _type(name: str) -> str:
	return PRIMITIVES.get(name, name)


def _identifier(member: ir.Member) -> str:
	stem = naming._HUNGARIAN.sub("", member.name)
	return naming._escape_keyword("_".join(naming._tokens(stem)))


def _declared(member: ir.Member) -> str:
	"""The C++ spelling of a pass-through parameter."""
	text = _type(member.type)
	if member.elem_const:
		text += " const"
	text += "*" * member.ptr_depth
	if member.array_dims:
		text += "*"
	return text


def _span_element(member: ir.Member) -> str:
	text = _type(member.type)
	if member.ptr_depth == 1:
		return text + (" const" if member.elem_const else "")
	text += " const" if member.elem_const else ""
	text += "*"
	text += " const" if member.ptr_const else ""
	return text


def _guard_open(lines: list[str], guards: list[str]) -> None:
	if guards:
		lines.append("#if " + " || ".join(f"defined({guard})" for guard in guards))


def _guard_close(lines: list[str], guards: list[str]) -> None:
	if guards:
		lines.append("#endif")


def _signature(command: ir.Wrapper, table: Table) -> tuple[list[str], list[str], str | None]:
	"""(template parameters, function parameters, the out argument's C++ type)."""
	templates: list[str] = []
	parameters: list[str] = []
	produced: str | None = None
	index = 0

	for argument in command.arguments:
		member = argument.member
		name = _identifier(member)
		if argument.kind == "info":
			index += 1
			tag = f"::vkfu::obj::{table.struct_name(member.type)}"
			parameter = f"Expression{index}"
			templates.append(f"::vkfu::expression_for<{tag}> {parameter}")
			parameters.append(f"{parameter}&& {name}")
		elif argument.kind == "span":
			parameters.append(f"::std::span<{_span_element(member)}> {name}")
		elif argument.kind == "allocator":
			parameters.append("::VkAllocationCallbacks const* allocation_callbacks")
		elif argument.kind == "out":
			# One indirection is the "out" itself: VkBuffer* produces a VkBuffer,
			# void** produces a void*.
			produced = _type(member.type) + "*" * (member.ptr_depth - 1)
		elif argument.kind == "out_span":
			parameters.append(f"::std::span<{_type(member.type)}> {name}")
		else:
			parameters.append(f"{_declared(member)} {name}")

	return templates, parameters, produced


def _call_arguments(command: ir.Wrapper) -> list[str]:
	"""One expression per native parameter, in the command's own order."""
	kinds = {argument.member.name: argument.kind for argument in command.arguments}
	by_name = {member.name: member for member in command.params}
	spans = {member.name: member for member in command.params}
	arguments: list[str] = []
	index = 0

	for member in command.params:
		if member.name in command.spans:
			owner = by_name[command.spans[member.name]]
			arguments.append(f"static_cast<{_type(member.type)}>({_identifier(owner)}.size())")
			continue
		kind = kinds.get(member.name, "passthrough")
		if kind == "info":
			index += 1
			arguments.append(f"::std::addressof(::vkfu::unpack(storage{index}))")
		elif kind == "span":
			arguments.append(f"{_identifier(member)}.data()")
		elif kind == "allocator":
			arguments.append("allocation_callbacks")
		elif kind == "out":
			arguments.append("::std::addressof(produced)")
		elif kind == "out_span":
			arguments.append(f"{_identifier(member)}.data()")
		else:
			arguments.append(_identifier(member))
	_ = spans
	return arguments


def _evaluations(command: ir.Wrapper) -> list[str]:
	lines: list[str] = []
	index = 0
	for argument in command.arguments:
		if argument.kind != "info":
			continue
		index += 1
		name = _identifier(argument.member)
		lines.append(
			f"{TAB}decltype(auto) storage{index} = ::vkfu::evaluate(::std::forward<Expression{index}>({name}));"
		)
	return lines


def render(command: ir.Wrapper, table: Table) -> list[str]:
	function = table.command_name(command.name).rpartition("::")[2]
	templates, parameters, produced = _signature(command, table)
	call = ", ".join(_call_arguments(command))
	template_line = f"template<{', '.join(templates)}>" if templates else None

	# A wrapper with no expression parameter is not a template, so it needs to be
	# inline or every translation unit emits its own definition.
	linkage = "" if template_line else "inline "

	lines: list[str] = []
	_guard_open(lines, command.guards)

	if command.returns == "VkResult":
		result = produced or "void"
		# nothrow: the VkResult comes back as the error.
		if template_line:
			lines.append(template_line)
		lines.append(
			f"[[nodiscard]] {linkage}auto {function}({', '.join(parameters + ['::std::nothrow_t'])}) noexcept"
		)
		lines.append(f"{TAB}-> ::std::expected<{result}, ::VkResult>")
		lines.append("{")
		lines.extend(_evaluations(command))
		if result != "void":
			# Declared, not `auto produced = T{}`: `void*{}` is not an expression.
			lines.append(f"{TAB}{result} produced{{}};")
		lines.append(f"{TAB}auto const outcome = ::{command.name}({call});")
		lines.append(f"{TAB}if (outcome != ::VK_SUCCESS)")
		lines.append(f"{TAB}{{")
		lines.append(f"{TAB}{TAB}return ::std::unexpected{{outcome}};")
		lines.append(f"{TAB}}}")
		lines.append(f"{TAB}return {'produced' if result != 'void' else '{}'};")
		lines.append("}")
		lines.append("")

		if template_line:
			lines.append(template_line)
		forwarded = ", ".join(
			(
				f"::std::forward<Expression{i}>({name})"
				if (i := _forward_index(command, name))
				else name
			)
			for name in _parameter_names(command)
		)
		declaration = ", ".join(_defaulted(parameters))
		if result == "void":
			lines.append(f"{linkage}void {function}({declaration})")
		else:
			lines.append(f"[[nodiscard]] {linkage}auto {function}({declaration}) -> {result}")
		lines.append("{")
		lines.append(f"{TAB}auto outcome = {function}({forwarded}, ::std::nothrow);")
		lines.append(f"{TAB}if (!outcome)")
		lines.append(f"{TAB}{{")
		lines.append(f"{TAB}{TAB}throw outcome.error();")
		lines.append(f"{TAB}}}")
		if result != "void":
			lines.append(f"{TAB}return *outcome;")
		lines.append("}")
	else:
		# void or a plain value: one function, nothing to fail.
		if template_line:
			lines.append(template_line)
		returns = command.returns if command.returns != "void" else (produced or "void")
		if returns == "void":
			lines.append(f"{linkage}void {function}({', '.join(_defaulted(parameters))})")
		else:
			lines.append(f"[[nodiscard]] {linkage}auto {function}({', '.join(_defaulted(parameters))}) -> {_type(returns)}")
		lines.append("{")
		lines.extend(_evaluations(command))
		if command.returns != "void":
			lines.append(f"{TAB}return ::{command.name}({call});")
		elif produced and produced != "void":
			lines.append(f"{TAB}{produced} produced{{}};")
			lines.append(f"{TAB}::{command.name}({call});")
			lines.append(f"{TAB}return produced;")
		else:
			lines.append(f"{TAB}::{command.name}({call});")
		lines.append("}")

	_guard_close(lines, command.guards)
	return lines


def _parameter_names(command: ir.Wrapper) -> list[str]:
	names: list[str] = []
	for argument in command.arguments:
		if argument.kind == "out":
			continue
		if argument.kind == "allocator":
			names.append("allocation_callbacks")
		else:
			names.append(_identifier(argument.member))
	return names


def _forward_index(command: ir.Wrapper, name: str) -> int:
	index = 0
	for argument in command.arguments:
		if argument.kind != "info":
			continue
		index += 1
		if _identifier(argument.member) == name:
			return index
	return 0


def _defaulted(parameters: list[str]) -> list[str]:
	"""The allocator is optional, and it is always the last thing Vulkan takes."""
	if parameters and parameters[-1].endswith("allocation_callbacks"):
		return parameters[:-1] + [parameters[-1] + " = nullptr"]
	return parameters
