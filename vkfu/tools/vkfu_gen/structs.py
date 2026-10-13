"""Renderers for everything a structure or an enum turns into.

Each function renders one entity and nothing around it: no namespace, no
section. Where it lands is emit's business.
"""

from __future__ import annotations

from . import ir
from .cpp import TAB, base_type, leaf_of
from .naming import Table
from .plan import Field, Plan


def _field_default(field: Field) -> str:
	if field.kind == "bool":
		return " = false"
	if field.kind == "pointer":
		return " = nullptr"
	return "{}"


def _render_flags_type(field: Field, registry: ir.Registry, table: Table, field_name: str) -> list[str]:
	"""A bit-field struct whose layout matches the native flags word exactly.

	Every bit position up to the enum's width is spelled out -- gaps included --
	so the type has no indeterminate bits and `bit_cast` stays usable in a
	constant expression.
	"""
	width = registry.flagbits[field.flag_bits].width
	flags_type = field.member.type
	lines = [f"{TAB}struct {field_name}_type", f"{TAB}{{"]

	position = 0
	for bit in field.layout:
		if bit.bitpos > position:
			lines.append(f"{TAB}{TAB}{flags_type} _reserved_{position} : {bit.bitpos - position} = 0;")
		lines.append(f"{TAB}{TAB}{flags_type} {table.bit_name(field.flag_bits, bit.name)} : 1 = 0;")
		position = bit.bitpos + 1
	if position < width:
		lines.append(f"{TAB}{TAB}{flags_type} _reserved_{position} : {width - position} = 0;")

	lines.append(f"{TAB}}};")
	lines.append("")
	lines.append(f"{TAB}{field_name}_type {field_name}{{}};")
	return lines


def _bit_constant(bit: ir.Bit) -> str:
	"""The constant's name, or its value when only some platforms declare it."""
	return ir.bit_literal(bit.bitpos) if bit.guards else bit.name


def _render_flags_checks(field: Field, table: Table, owner: str, field_name: str) -> list[str]:
	"""Prove the bit-field layout against the real enum constants.

	Bit-field allocation order is implementation-defined, so the mapping is
	asserted rather than assumed. A platform-guarded constant is checked against
	its value instead of its name, so the file needs no macro to compile; the
	field itself is unconditional so the layout never shifts between platforms.
	"""
	nested = f"{owner}::{field_name}_type"
	lines = [
		f"static_assert(sizeof({nested}) == sizeof({field.member.type}));",
		# clang has no constexpr bit_cast over bit-fields yet, so the layout is
		# proven on the compilers that do; the runtime tests cover clang.
		"#if !defined(__clang__)",
	]
	for bit in field.layout:
		name = table.bit_name(field.flag_bits, bit.name)
		lines.append(
			f"static_assert(::std::bit_cast<{field.member.type}>({nested}{{.{name} = 1}}) == {_bit_constant(bit)});"
		)
	lines.append("#endif")
	return lines


def render_alias(
	alias: ir.Alias,
	chosen: str,
	target: str,
	slots: list[Field],
	table: Table,
	area: str,
) -> list[str]:
	"""`using` for an old spelling, in its own namespace beside the real name.

	A param with pointer slots is a template, so its alias has to be an alias
	template with the same defaults -- otherwise the old spelling would only work
	with every slot named explicitly.
	"""
	lines: list[str] = []
	leaf = leaf_of(chosen)
	if area == "param" and slots:
		declarations = ", ".join(
			f"::vkfu::reference_expression_for<::vkfu::obj::{table.struct_name(field.target)}> "
			f"{table.member_name(alias.target, field.member.name)}_expression = ::vkfu::absent_expression"
			for field in slots
		)
		arguments = ", ".join(
			f"{table.member_name(alias.target, field.member.name)}_expression" for field in slots
		)
		lines.append(f"template<{declarations}>")
		lines.append(f"using {leaf} = ::vkfu::{area}::{target}<{arguments}>;")
	else:
		lines.append(f"using {leaf} = ::vkfu::{area}::{target};")
	return lines


def render_param(
	struct: ir.Struct,
	plan: Plan,
	registry: ir.Registry,
	table: Table,
	object_name: str,
) -> list[str]:
	names = {field.member.name: table.member_name(struct.name, field.member.name) for field in plan.fields}

	slots = [field for field in plan.fields if field.kind == "reference"]
	parameters = {field.member.name: f"{names[field.member.name]}_expression" for field in slots}

	lines: list[str] = []
	if slots:
		# One template parameter per pointer slot, so a slot can hold a whole
		# sub-expression and the parent's storage owns what it points at.
		declarations = ", ".join(
			f"::vkfu::reference_expression_for<obj::{table.struct_name(field.target)}> "
			f"{parameters[field.member.name]} = ::vkfu::absent_expression"
			for field in slots
		)
		lines.append(f"template<{declarations}>")
	lines.append(f"struct {leaf_of(object_name)}")
	lines.append("{")
	if struct.stype is not None:
		lines.append(f"{TAB}using vulkan_tag_type = obj::{object_name};")
	if slots:
		lines.append(f"{TAB}using storage_type = ::vkfu::reference_storage<{struct.name}")
		for field in slots:
			lines.append(
				f"{TAB}{TAB}, ::vkfu::reference_slot<&{struct.name}::{field.member.name}, "
				f"::vkfu::reference_storage_of_t<{parameters[field.member.name]}>>"
			)
		lines.append(f"{TAB}{TAB}>;")
	if struct.stype is not None or slots:
		# Only worth a blank line when there is an alias to separate from the
		# fields; an sType-less param has nothing above them.
		lines.append("")

	for field in plan.fields:
		field_name = names[field.member.name]
		if field.kind == "flags":
			lines.extend(_render_flags_type(field, registry, table, field_name))
		elif field.kind == "enum":
			lines.append(f"{TAB}::vkfu::enums::{table.enum_name(field.enum_type)} {field_name}{{}};")
		elif field.kind == "reference":
			lines.append(f"{TAB}{parameters[field.member.name]} {field_name}{{}};")
		else:
			lines.append(f"{TAB}{field.cpp_type} {field_name}{_field_default(field)};")

	arrays = [field for field in plan.fields if field.kind == "array"]
	by_field = {field.member.name: field for field in plan.fields}
	# A fixed-size native array cannot be filled by a designated initialiser, so
	# only those structures need a named temporary.
	inner = f"{TAB}{TAB}{TAB}"
	if slots:
		# Not const: a sub-expression's own evaluate() need not be.
		lines.append("")
		lines.append(f"{TAB}constexpr auto evaluate() -> storage_type")
		lines.append(f"{TAB}{{")
		if arrays:
			# A designator cannot fill a C array, so the head needs a name before
			# it can go into the storage.
			lines.append(f"{TAB}{TAB}auto value = {struct.name}{{")
			inner = f"{TAB}{TAB}{TAB}"
		else:
			lines.append(f"{TAB}{TAB}return storage_type{{")
			lines.append(f"{TAB}{TAB}{TAB}{struct.name}{{")
			inner = f"{TAB}{TAB}{TAB}{TAB}"
	else:
		head = f"{TAB}{TAB}auto value = {struct.name}{{" if arrays else f"{TAB}{TAB}return {struct.name}{{"
		lines.append("")
		lines.append(f"{TAB}constexpr auto evaluate() const noexcept -> {struct.name}")
		lines.append(f"{TAB}{{")
		lines.append(head)
	if struct.stype is not None:
		lines.append(f"{inner}.sType = {struct.stype},")
		lines.append(f"{inner}.pNext = nullptr,")
	for member in struct.members:
		if member.name in ("sType", "pNext"):
			continue
		span = plan.consumed_counts.get(member.name)
		if span is not None:
			span_name = names[span.member.name]
			cast = base_type(member.type)
			lines.append(f"{inner}.{member.name} = static_cast<{cast}>({span_name}.size()),")
			continue
		field = by_field.get(member.name)
		if field is None:
			continue
		field_name = names[member.name]
		if field.kind == "array":
			# Designated as empty so the aggregate is complete; the values are
			# copied in below, because a designator cannot fill a C array.
			lines.append(f"{inner}.{member.name} = {{}},")
			continue
		if field.kind == "bool":
			lines.append(f"{inner}.{member.name} = {field_name} ? VK_TRUE : VK_FALSE,")
		elif field.kind == "span":
			lines.append(f"{inner}.{member.name} = {field_name}.data(),")
		elif field.kind == "flags":
			lines.append(f"{inner}.{member.name} = ::std::bit_cast<{member.type}>({field_name}),")
		elif field.kind == "enum":
			lines.append(f"{inner}.{member.name} = static_cast<{member.type}>({field_name}),")
		elif field.kind == "reference":
			lines.append(f"{inner}.{member.name} = nullptr,")
		else:
			lines.append(f"{inner}.{member.name} = {field_name},")

	if slots:
		if arrays:
			lines.append(f"{TAB}{TAB}}};")
			for field in arrays:
				lines.append(
					f"{TAB}{TAB}::std::ranges::copy({names[field.member.name]}, value.{field.member.name});"
				)
			lines.append(f"{TAB}{TAB}return storage_type{{")
			lines.append(f"{TAB}{TAB}{TAB}value,")
		else:
			lines.append(f"{TAB}{TAB}{TAB}}},")
		for field in slots:
			lines.append(f"{TAB}{TAB}{TAB}::vkfu::evaluate({names[field.member.name]}),")
		lines.append(f"{TAB}{TAB}}};")
		lines.append(f"{TAB}}}")
		lines.append("};")
		for field in plan.fields:
			if field.kind == "flags":
				lines.append("")
				# Every template parameter has a default, so the layout of the
				# nested bit-field type can be checked on the default instantiation.
				lines.extend(_render_flags_checks(field, table, f"{leaf_of(object_name)}<>", names[field.member.name]))
		return lines

	lines.append(f"{TAB}{TAB}}};")
	if arrays:
		for field in arrays:
			field_name = names[field.member.name]
			lines.append(f"{TAB}{TAB}::std::ranges::copy({field_name}, value.{field.member.name});")
		lines.append(f"{TAB}{TAB}return value;")
	lines.append(f"{TAB}}}")
	lines.append("};")

	# A nested class's default member initialisers are not usable inside the
	# enclosing class's body, so the layout checks sit just after it.
	for field in plan.fields:
		if field.kind == "flags":
			lines.append("")
			lines.extend(_render_flags_checks(field, table, leaf_of(object_name), names[field.member.name]))

	return lines


def render_enum(enum_type: ir.EnumType, table: Table) -> list[str]:
	"""A scoped enum over the native one.

	The underlying type is taken from the native enum so the static_cast back is
	always value-preserving. An enumerator only some platforms declare is
	written as its value, so the enum is the same everywhere and needs no macro.
	"""
	lines: list[str] = []
	lines += [
		f"enum class {table.enum_name(enum_type.name)} : ::std::underlying_type_t<{enum_type.name}>",
		"{",
	]
	for value in enum_type.values:
		constant = value.name if not value.guards else f"static_cast<{enum_type.name}>({value.literal})"
		lines.append(f"{TAB}{table.value_name(enum_type.name, value.name)} = {constant},")
	lines.append("};")
	return lines


def render_native_glue(struct: ir.Struct) -> list[str]:
	"""The evaluate / pNext hooks for one native structure.

	One overload per structure rather than a constrained template: a template
	would answer for every Vulkan structure in existence, including ones vkfu
	knows nothing about. These names live in the global namespace because that is
	where the structures are declared and so where ADL looks, and they are
	prefixed so that nothing else can collide with them.
	"""
	pnext = next((member for member in struct.members if member.name == "pNext"), None)
	assign = "value.pNext = next;" if pnext is not None and pnext.elem_const else "value.pNext = const_cast<void*>(next);"
	lines: list[str] = []
	lines += [
		f"constexpr auto _vkfu_address({struct.name}& value) noexcept -> void const*",
		"{",
		f"{TAB}return ::std::addressof(value);",
		"}",
		"",
		f"constexpr void _vkfu_set_next({struct.name}& value, void const* next) noexcept",
		"{",
		f"{TAB}{assign}",
		"}",
		"",
		f"constexpr auto _vkfu_evaluate({struct.name} const& value) noexcept -> {struct.name}",
		"{",
		f"{TAB}return value;",
		"}",
	]
	return lines


_CORE_VERSION = {
	"VK_VERSION_1_0": "VK_API_VERSION_1_0",
	"VK_VERSION_1_1": "VK_API_VERSION_1_1",
	"VK_VERSION_1_2": "VK_API_VERSION_1_2",
	"VK_VERSION_1_3": "VK_API_VERSION_1_3",
	"VK_VERSION_1_4": "VK_API_VERSION_1_4",
}


def render_extensions(name: str, tag: str, provenance: ir.Provenance, guards: list[str]) -> list[str]:
	"""What has to be enabled before this object exists.

	Emitted for everything, including the objects that need nothing: an empty
	`names` with a core version is the answer "this is core, you are fine", and
	it is the specialization that makes required_extensions() stop asking.
	"""
	core = _CORE_VERSION.get(provenance.core or "", "0")
	lines: list[str] = []
	lines.append("template<>")
	lines.append(f"struct vulkan_object_extensions<obj::{tag}>")
	lines.append("{")
	count = len(provenance.extensions)
	if count:
		listed = ", ".join(f'"{entry}"' for entry in provenance.extensions)
		lines.append(f"{TAB}constexpr static ::std::array<char const*, {count}> names{{{listed}}};")
	else:
		lines.append(f"{TAB}constexpr static ::std::array<char const*, 0> names{{}};")
	lines.append(f"{TAB}constexpr static ::std::uint32_t core = {core};")
	lines.append(f"{TAB}constexpr static auto instance = {'true' if provenance.instance else 'false'};")
	lines.append("};")
	return lines
