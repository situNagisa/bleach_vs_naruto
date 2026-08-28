"""Stage 2: IR + naming table -> C++ header.

Every identifier this stage writes comes from either vk.xml (native types,
members, enum constants) or the naming table (obj/param/field names). It never
derives a name itself; a missing table entry is an error, not a fallback.
"""

from __future__ import annotations

import dataclasses

from . import commands, ir, producers, queries
from .naming import Table


PRIMITIVES = {
	"uint8_t": "::std::uint8_t",
	"uint16_t": "::std::uint16_t",
	"uint32_t": "::std::uint32_t",
	"uint64_t": "::std::uint64_t",
	"int8_t": "::std::int8_t",
	"int16_t": "::std::int16_t",
	"int32_t": "::std::int32_t",
	"int64_t": "::std::int64_t",
	"size_t": "::std::size_t",
}

TAB = "\t"
NEWLINE = chr(10)


class GenerationError(Exception):
	def __init__(self, missing: list[str], problems: list[str]) -> None:
		super().__init__("generation failed")
		self.missing = missing
		self.problems = problems


# ---------------------------------------------------------------- field plan


@dataclasses.dataclass
class Field:
	kind: str  # bool | scalar | enum | flags | span | pointer | reference | array
	member: ir.Member
	cpp_type: str  # declared type of the param field
	count_member: str | None = None  # span: the native count member it drives
	flag_bits: str | None = None  # flags: the VkXxxFlagBits enum
	layout: list[ir.Bit] | None = None  # flags: bits in ascending bitpos order
	enum_type: str | None = None  # enum: the VkXxx plain enum it wraps
	target: str | None = None  # reference: the VkXxx the pointer names


@dataclasses.dataclass
class Plan:
	struct: ir.Struct
	fields: list[Field]
	consumed_counts: dict[str, Field]  # native count member -> owning span field
	unsupported: list[str]


def _base_type(name: str) -> str:
	return PRIMITIVES.get(name, name)


def _pointer_type(member: ir.Member) -> str:
	text = _base_type(member.type)
	if member.elem_const:
		text += " const"
	text += "*"
	if member.ptr_depth >= 2:
		if member.ptr_const:
			text += " const"
		text += "*"
	return text


def _span_element_type(member: ir.Member) -> str:
	text = _base_type(member.type)
	if member.ptr_depth == 1:
		return text + (" const" if member.elem_const else "")
	text += " const" if member.elem_const else ""
	text += "*"
	text += " const" if member.ptr_const else ""
	return text


def plan_struct(struct: ir.Struct, registry: ir.Registry) -> Plan:
	by_name = {member.name: member for member in struct.members}
	unsupported: list[str] = []

	# A count member folds into a span only if exactly one pointer references it
	# and the element type can be spanned. Anything else stays verbatim.
	references: dict[str, list[ir.Member]] = {}
	for member in struct.members:
		if member.ptr_depth < 1 or not member.length or member.array_dims:
			continue
		token = member.length.split(",")[0]
		target = by_name.get(token)
		if target is None or target.ptr_depth != 0 or target.array_dims:
			continue
		if member.type == "void":
			continue
		if member.optional:
			# An optional pointer's count is independent of it: dynamic viewport
			# state means viewportCount == 1 with pViewports == NULL, which a
			# span cannot say. Keep both members verbatim.
			continue
		references.setdefault(token, []).append(member)

	foldable = {count: pointers[0] for count, pointers in references.items() if len(pointers) == 1}
	folded_pointers = {pointer.name: count for count, pointer in foldable.items()}

	fields: list[Field] = []
	consumed: dict[str, Field] = {}
	for member in struct.members:
		if member.name in ("sType", "pNext"):
			continue
		if member.name in foldable:
			continue  # emitted from the owning span's size()

		if member.array_dims:
			if len(member.array_dims) != 1:
				unsupported.append(f"{struct.name}.{member.name}: multi-dimensional array")
				continue
			element = _base_type(member.type)
			if member.ptr_depth:
				element = _pointer_type(member)
			fields.append(
				Field(
					kind="array",
					member=member,
					cpp_type=f"::std::array<{element}, {member.array_dims[0]}>",
				)
			)
			continue

		if member.name in folded_pointers:
			field = Field(
				kind="span",
				member=member,
				cpp_type=f"::std::span<{_span_element_type(member)}>",
				count_member=folded_pointers[member.name],
			)
			fields.append(field)
			consumed[field.count_member] = field
			continue

		if ir.is_reference(member, registry.structs.get(member.type)):
			fields.append(Field(kind="reference", member=member, cpp_type=member.type, target=member.type))
			continue

		if member.ptr_depth >= 1:
			fields.append(Field(kind="pointer", member=member, cpp_type=_pointer_type(member)))
			continue

		if member.type == "VkBool32":
			fields.append(Field(kind="bool", member=member, cpp_type="bool"))
			continue

		flag_bits = registry.bitmask_to_flagbits.get(member.type)
		if flag_bits and registry.flagbits.get(flag_bits):
			layout = _bit_layout(registry.flagbits[flag_bits])
			if layout:
				fields.append(
					Field(
						kind="flags",
						member=member,
						cpp_type=member.type,
						flag_bits=flag_bits,
						layout=layout,
					)
				)
				continue
			if layout is None:
				unsupported.append(
					f"{struct.name}.{member.name}: {flag_bits} bits do not fit one per position"
				)

		if member.type in registry.enums and registry.enums[member.type].values:
			fields.append(Field(kind="enum", member=member, cpp_type=member.type, enum_type=member.type))
			continue

		fields.append(Field(kind="scalar", member=member, cpp_type=_base_type(member.type)))

	return Plan(struct=struct, fields=fields, consumed_counts=consumed, unsupported=unsupported)


# ---------------------------------------------------------------- rendering


def _guard_open(lines: list[str], guards: list[str]) -> None:
	if guards:
		# Reachable through any one of them, so OR rather than AND.
		lines.append("#if " + " || ".join(f"defined({guard})" for guard in guards))


def _guard_close(lines: list[str], guards: list[str]) -> None:
	if guards:
		lines.append("#endif")


def _field_default(field: Field) -> str:
	if field.kind == "bool":
		return " = false"
	if field.kind == "pointer":
		return " = nullptr"
	return "{}"


def positional_bits(flag_bits: ir.FlagBits) -> list[ir.Bit]:
	"""The bits that can occupy a fixed bit-field position.

	Entries defined by `value` rather than `bitpos` are excluded: a zero
	sentinel (VK_..._DEFAULT / _UNKNOWN / _INVALID) sets no bits at all, and a
	compound alias (VK_SHADER_STAGE_ALL_GRAPHICS) sets several. Both are
	expressible by the individual bits, neither is a bit-field.
	"""
	return sorted(
		(bit for bit in flag_bits.bits if bit.bitpos is not None),
		key=lambda bit: bit.bitpos,
	)


def _bit_layout(flag_bits: ir.FlagBits) -> list[ir.Bit] | None:
	"""None when the bits cannot be laid out one-per-position."""
	bits = positional_bits(flag_bits)
	positions = [bit.bitpos for bit in bits]
	if len(set(positions)) != len(positions):
		return None
	if positions and max(positions) >= flag_bits.width:
		return None
	return bits


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


def _namespace_of(qualified: str) -> str:
	return qualified.rpartition("::")[0]


def _leaf_of(qualified: str) -> str:
	return qualified.rpartition("::")[2]


def _grouped_by(names: dict[str, str]) -> list[tuple[str, list[str]]]:
	"""Keys bucketed by the namespace of their chosen name."""
	buckets: dict[str, list[str]] = {}
	for key, value in names.items():
		buckets.setdefault(_namespace_of(value), []).append(key)
	return sorted(
		((namespace, sorted(keys)) for namespace, keys in buckets.items()),
		key=lambda entry: (entry[0] != "", entry[0]),
	)


def _render_alias(
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
	_guard_open(lines, alias.guards)
	leaf = _leaf_of(chosen)
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
	_guard_close(lines, alias.guards)
	return lines


def _grouped(selected: list[str], table: Table) -> list[tuple[str, list[str]]]:
	"""Objects bucketed by the namespace their table name puts them in."""
	buckets: dict[str, list[str]] = {}
	for name in selected:
		buckets.setdefault(_namespace_of(table.struct_name(name)), []).append(name)
	return sorted(buckets.items(), key=lambda entry: (entry[0] != "", entry[0]))


def _render_flags_checks(field: Field, table: Table, owner: str, field_name: str) -> list[str]:
	"""Prove the bit-field layout against the real enum constants.

	Bit-field allocation order is implementation-defined, so the mapping is
	asserted rather than assumed. A platform-guarded constant is only asserted
	where it exists; the field itself is unconditional so the layout never
	shifts between platforms.
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
		_guard_open(lines, bit.guards)
		lines.append(
			f"static_assert(::std::bit_cast<{field.member.type}>({nested}{{.{name} = 1}}) == {bit.name});"
		)
		_guard_close(lines, bit.guards)
	lines.append("#endif")
	return lines


def _render_param(
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
	_guard_open(lines, struct.guards)
	if slots:
		# One template parameter per pointer slot, so a slot can hold a whole
		# sub-expression and the parent's storage owns what it points at.
		declarations = ", ".join(
			f"::vkfu::reference_expression_for<obj::{table.struct_name(field.target)}> "
			f"{parameters[field.member.name]} = ::vkfu::absent_expression"
			for field in slots
		)
		lines.append(f"template<{declarations}>")
	lines.append(f"struct {_leaf_of(object_name)}")
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
			cast = _base_type(member.type)
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
				lines.extend(_render_flags_checks(field, table, f"{_leaf_of(object_name)}<>", names[field.member.name]))
		_guard_close(lines, struct.guards)
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
			lines.extend(_render_flags_checks(field, table, _leaf_of(object_name), names[field.member.name]))

	_guard_close(lines, struct.guards)
	return lines


def used_enums(selected: list[str], registry: ir.Registry) -> list[str]:
	"""Plain enums that some selected object exposes as a field."""
	names: list[str] = []
	for name in list(selected) + list(registry.plain):
		for field in plan_struct(registry.structs[name], registry).fields:
			if field.kind == "enum" and field.enum_type not in names:
				names.append(field.enum_type)
	return sorted(names)


def _render_enum(enum_type: ir.EnumType, table: Table) -> list[str]:
	"""A scoped enum over the native one.

	The underlying type is taken from the native enum so the static_cast back is
	always value-preserving.
	"""
	lines: list[str] = []
	_guard_open(lines, enum_type.guards)
	lines += [
		f"enum class {table.enum_name(enum_type.name)} : ::std::underlying_type_t<{enum_type.name}>",
		"{",
	]
	for value in enum_type.values:
		_guard_open(lines, value.guards)
		lines.append(f"{TAB}{table.value_name(enum_type.name, value.name)} = {value.name},")
		_guard_close(lines, value.guards)
	lines.append("};")
	_guard_close(lines, enum_type.guards)
	return lines


def _render_native_glue(struct: ir.Struct) -> list[str]:
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
	_guard_open(lines, struct.guards)
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
	_guard_close(lines, struct.guards)
	return lines



_CORE_VERSION = {
	"VK_VERSION_1_0": "VK_API_VERSION_1_0",
	"VK_VERSION_1_1": "VK_API_VERSION_1_1",
	"VK_VERSION_1_2": "VK_API_VERSION_1_2",
	"VK_VERSION_1_3": "VK_API_VERSION_1_3",
	"VK_VERSION_1_4": "VK_API_VERSION_1_4",
}


def _render_extensions(name: str, tag: str, provenance: ir.Provenance, guards: list[str]) -> list[str]:
	"""What has to be enabled before this object exists.

	Emitted for everything, including the objects that need nothing: an empty
	`names` with a core version is the answer "this is core, you are fine", and
	it is the specialization that makes required_extensions() stop asking.
	"""
	core = _CORE_VERSION.get(provenance.core or "", "0")
	lines: list[str] = []
	_guard_open(lines, guards)
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
	_guard_close(lines, guards)
	return lines


def _build(registry: ir.Registry, table: Table, scope: str) -> tuple[dict[str, list[str]], list[str]]:
	missing: list[str] = []
	problems: list[str] = []
	warnings: list[str] = []

	known = set(registry.closure) | set(registry.plain) | set(registry.query_closure)
	for named in table.structs:
		if named not in known:
			warnings.append(f"naming.toml names {named}, which is not in scope; ignored")

	if scope == "table":
		selected = [name for name in registry.closure if table.struct_name(name)]
	else:
		selected = list(registry.closure)
		for name in selected:
			if not table.struct_name(name):
				missing.append(f"struct.{name}")

	# Read-side objects: a tag, a trait and the pNext hooks, so a query chain can
	# be built and linked. No param -- a vkGet* fills these in, so there are no
	# fields for a caller to set and no field names to look up.
	queried = [name for name in registry.query_closure if table.struct_name(name)]
	if scope != "table":
		for name in registry.query_closure:
			if not table.struct_name(name):
				missing.append(f"struct.{name}")

	seen: dict[str, str] = {}
	for name in list(selected) + [n for n in list(registry.plain) + queried if table.struct_name(n)]:
		object_name = table.struct_name(name)
		if object_name is None:
			continue  # already reported as missing
		if not object_name.rpartition("::")[2]:
			problems.append(f"{name}: '{object_name}' has no leaf name")
			continue
		if not all(segment.isidentifier() for segment in object_name.split("::")):
			problems.append(f"{name}: '{object_name}' is not a valid qualified name")
		if object_name in seen:
			problems.append(f"name collision: {seen[object_name]} and {name} both map to '{object_name}'")
		else:
			seen[object_name] = name

	plans: dict[str, Plan] = {}
	for name in selected:
		object_name = table.struct_name(name)
		plan = plan_struct(registry.structs[name], registry)
		problems.extend(plan.unsupported)
		plans[name] = plan
		used: dict[str, str] = {}
		for field in plan.fields:
			field_name = table.member_name(name, field.member.name)
			if not field_name:
				missing.append(f"member.{name}.{field.member.name}")
				continue
			if field_name in used:
				problems.append(f"field collision in {name}: {used[field_name]} and {field.member.name} -> '{field_name}'")
			else:
				used[field_name] = field.member.name
			if field.kind == "enum" and field_name == table.enum_name(field.enum_type):
				# Legal, because generated code always writes ::vkfu::enums::x in
				# full, but the member does hide the enum name in class scope.
				warnings.append(f"{name}: field '{field_name}' hides the enum class it is typed with")
			# object_name is None when the struct itself is unnamed, which is
			# already reported; keep going rather than crashing on it.
			if object_name is not None and field_name == _leaf_of(object_name):
				# Legal for an aggregate, but the member hides the injected class
				# name inside the struct, so it is worth a look.
				warnings.append(f"{name}: field '{field_name}' has the same name as its object")
			if field.kind == "flags":
				seen_bits: dict[str, str] = {}
				for bit in field.layout:
					bit_name = table.bit_name(field.flag_bits, bit.name)
					if not bit_name:
						missing.append(f"bit.{field.flag_bits}.{bit.name}")
					elif bit_name in seen_bits:
						problems.append(
							f"bit collision in {field.flag_bits}: {seen_bits[bit_name]} and {bit.name} -> '{bit_name}'"
						)
					else:
						seen_bits[bit_name] = bit.name

	plain_selected = [name for name in registry.plain if table.struct_name(name)]
	if scope != "table":
		for name in registry.plain:
			if not table.struct_name(name):
				missing.append(f"struct.{name}")
	for name in plain_selected:
		plan = plan_struct(registry.structs[name], registry)
		problems.extend(plan.unsupported)
		used_names: dict[str, str] = {}
		for field in plan.fields:
			field_name = table.member_name(name, field.member.name)
			if not field_name:
				missing.append(f"member.{name}.{field.member.name}")
				continue
			if field_name in used_names:
				problems.append(f"field collision in {name}: {used_names[field_name]} and {field.member.name} -> '{field_name}'")
			else:
				used_names[field_name] = field.member.name
			if field.kind == "flags":
				for bit in field.layout:
					if not table.bit_name(field.flag_bits, bit.name):
						missing.append(f"bit.{field.flag_bits}.{bit.name}")

	enums = used_enums(selected, registry)
	for enum_type in enums:
		if not table.enum_name(enum_type):
			missing.append(f"enum.{enum_type}")
			continue
		seen_values: dict[str, str] = {}
		for value in registry.enums[enum_type].values:
			name = table.value_name(enum_type, value.name)
			if not name:
				missing.append(f"value.{enum_type}.{value.name}")
			elif name in seen_values:
				problems.append(f"enumerator collision in {enum_type}: {seen_values[name]} and {value.name} -> '{name}'")
			else:
				seen_values[name] = value.name

	for command in list(registry.queries) + list(registry.chain_queries):
		if not table.command_name(command.name):
			missing.append(f"command.{command.name}")

	for command in registry.producers:
		if command.info.type not in selected:
			continue
		if not table.command_name(command.name):
			missing.append(f"command.{command.name}")
		elif producers.shape_of(command) == "array" and not table.command_singular(command.name):
			missing.append(f"command.{command.name}.singular")

	# Everything a param struct declares at class scope has to be distinct: the
	# fields, the nested `<field>_type` of a flags field, the `<field>_expression`
	# template parameter of a slot, and the fixed aliases.
	for name in selected:
		plan = plans[name]
		scope: dict[str, str] = {}
		entries = [("vulkan_tag_type", "alias")]
		if any(field.kind == "reference" for field in plan.fields):
			entries.append(("storage_type", "alias"))
		for field in plan.fields:
			field_name = table.member_name(name, field.member.name)
			if not field_name:
				continue
			entries.append((field_name, "field"))
			if field.kind == "flags":
				entries.append((f"{field_name}_type", "nested type"))
			if field.kind == "reference":
				entries.append((f"{field_name}_expression", "template parameter"))
		for entry, kind in entries:
			if entry in scope:
				problems.append(f"{name}: '{entry}' is both a {scope[entry]} and a {kind}")
			else:
				scope[entry] = kind

	if missing or problems:
		raise GenerationError(sorted(set(missing)), sorted(set(problems)))

	emitted = set(selected) | set(queried)
	# An alias only makes sense once its target is being emitted, and only if the
	# table gives it somewhere to live.
	by_alias = {
		alias.name: alias
		for alias in registry.aliases
		if alias.target in emitted and table.alias_name(alias.name)
	}
	aliased = {name: table.alias_name(name) for name in by_alias}
	param_targets = set(selected) | {n for n in registry.plain if table.struct_name(n)}
	# Four bodies rather than one, because they depend on each other in one
	# direction only: params and commands both need the tags, params also need the
	# enums, and nothing needs the params. Written to one file or four.
	sections: dict[str, list[str]] = {name: [] for name in SECTION_ORDER}
	lines = sections["objects"]

	chained = list(selected) + queried
	for name in chained:
		lines.extend(_render_native_glue(registry.structs[name]))
		lines.append("")

	if enums:
		lines = sections["enums"]
		lines += ["namespace vkfu::enums", "{"]
		for enum_type in enums:
			lines.extend(_render_enum(registry.enums[enum_type], table))
			lines.append("")
		lines += ["}", ""]

	lines = sections["objects"]
	lines += ["namespace vkfu::obj", "{"]

	for namespace, names in _grouped(chained, table):
		if namespace:
			lines.append(f"namespace {namespace}")
			lines.append("{")
		for name in names:
			struct = registry.structs[name]
			_guard_open(lines, struct.guards)
			lines.append(f"struct {_leaf_of(table.struct_name(name))}{{}};")
			_guard_close(lines, struct.guards)
		if namespace:
			lines.append("}")

	lines += ["}", "", "namespace vkfu", "{"]

	# A query chain needs a head as much as a create chain does, so the structures
	# a command writes into count as roots too.
	roots = set(registry.roots) | set(registry.query_roots)
	branches = set(registry.branches)
	for name in chained:
		struct = registry.structs[name]
		object_name = table.struct_name(name)
		_guard_open(lines, struct.guards)
		lines.append("template<>")
		lines.append(f"struct vulkan_object_trait<obj::{object_name}>")
		lines.append("{")
		lines.append(f"{TAB}constexpr static auto root = {'true' if name in roots else 'false'};")
		lines.append(f"{TAB}constexpr static auto branch = {'true' if name in branches else 'false'};")
		lines.append(f"{TAB}constexpr static auto allow_duplicate = {'true' if struct.allow_duplicate else 'false'};")
		lines.append("};")
		_guard_close(lines, struct.guards)
		lines.append("")

	for name in chained:
		struct = registry.structs[name]
		_guard_open(lines, struct.guards)
		lines.append("template<>")
		lines.append(f"struct expression_vulkan_tag<{name}>")
		lines.append("{")
		lines.append(f"{TAB}using type = obj::{table.struct_name(name)};")
		lines.append("};")
		lines.append("")
		# The other direction, which a query chain needs: it is handed tags and
		# has to declare the native structures they stand for.
		lines.append("template<>")
		lines.append(f"struct vulkan_object_native<obj::{table.struct_name(name)}>")
		lines.append("{")
		lines.append(f"{TAB}using type = {name};")
		lines.append(f"{TAB}constexpr static auto structure_type = {struct.stype};")
		lines.append("};")
		_guard_close(lines, struct.guards)
		lines.append("")

	for name in chained:
		provenance = registry.provenance.get(name)
		if provenance is None:
			continue
		lines.extend(
			_render_extensions(name, table.struct_name(name), provenance, registry.structs[name].guards)
		)
		lines.append("")

	for parent, child in registry.edges:
		if parent not in emitted or child not in emitted:
			continue
		guards = sorted(set(registry.structs[parent].guards) | set(registry.structs[child].guards))
		_guard_open(lines, guards)
		lines.append("template<>")
		lines.append(
			f"inline constexpr auto is_vulkan_object_compatible_with_v<obj::{table.struct_name(parent)}, obj::{table.struct_name(child)}> = true;"
		)
		_guard_close(lines, guards)

	# Close `namespace vkfu` here: the traits and the compat edges are the end of
	# the objects section, and it has to stand on its own as a file.
	lines.append("}")

	# Old spellings of promoted types. The tag alias always applies; the param
	# alias only when the target has a param at all.
	if aliased:
		lines += ["", "namespace vkfu::obj", "{"]
		for namespace, names in _grouped_by(aliased):
			if namespace:
				lines.append(f"namespace {namespace}")
				lines.append("{")
			for name in names:
				alias = by_alias[name]
				lines.extend(
					_render_alias(alias, aliased[name], table.struct_name(alias.target), [], table, "obj")
				)
			if namespace:
				lines.append("}")
		lines.append("}")

	lines = sections["param"]
	lines += ["", "namespace vkfu::param", "{"]

	for namespace, names in _grouped(selected, table):
		if namespace:
			lines.append(f"namespace {namespace}")
			lines.append("{")
		# Argument-dependent lookup only associates a class's innermost enclosing
		# namespace, so vkfu::operator| has to be visible from each one.
		lines.append("using ::vkfu::operator|;")
		lines.append("")
		for name in names:
			lines.extend(_render_param(registry.structs[name], plans[name], registry, table, table.struct_name(name)))
			lines.append("")
		if namespace:
			lines.append("}")
			lines.append("")

	# Structures with no sType: a param and an evaluate(), nothing else. They have
	# no pNext to manage, so no tag, no trait and no chain hooks.
	for namespace, names in _grouped(plain_selected, table):
		if namespace:
			lines.append(f"namespace {namespace}")
			lines.append("{")
		for name in names:
			lines.extend(
				_render_param(
					registry.structs[name],
					plan_struct(registry.structs[name], registry),
					registry,
					table,
					table.struct_name(name),
				)
			)
			lines.append("")
		if namespace:
			lines.append("}")
			lines.append("")

	if aliased:
		param_aliases = {n: v for n, v in aliased.items() if by_alias[n].target in param_targets}
		for namespace, names in _grouped_by(param_aliases):
			if namespace:
				lines.append(f"namespace {namespace}")
				lines.append("{")
			for name in names:
				alias = by_alias[name]
				slots = [
					field
					for field in plan_struct(registry.structs[alias.target], registry).fields
					if field.kind == "reference"
				]
				lines.extend(
					_render_alias(
						alias, aliased[name], table.struct_name(alias.target), slots, table, "param"
					)
				)
			if namespace:
				lines.append("}")
			lines.append("")

	lines.append("}")

	wrapped = [
		command
		for command in registry.producers
		if command.info.type in emitted and table.command_name(command.name)
	]
	known = set(selected) | set(plain_selected)
	general = [
		command
		for command in registry.wrappers
		if table.command_name(command.name)
		and all(
			argument.member.type in known
			for argument in command.arguments
			if argument.kind == "info"
		)
	]
	enumerates = [
		query for query in registry.queries if table.command_name(query.name)
	]
	chains = [
		query
		for query in registry.chain_queries
		if table.command_name(query.name)
		and query.out.type in emitted
		and all(
			argument.member.type in known
			for argument in query.arguments
			if argument.kind == "info"
		)
	]
	if wrapped or general or enumerates or chains:
		lines = sections["commands"]
		lines += ["namespace vkfu", "{"]
		grouped: dict[str, list[object]] = {}
		for command in wrapped:
			grouped.setdefault(_namespace_of(table.command_name(command.name)), []).append(command)
		for command in general + enumerates + chains:
			grouped.setdefault(_namespace_of(table.command_name(command.name)), []).append(command)
		for namespace, group in sorted(grouped.items(), key=lambda entry: (entry[0] != "", entry[0])):
			if namespace:
				lines.append(f"namespace {namespace}")
				lines.append("{")
			for command in group:
				if isinstance(command, ir.Command):
					lines.extend(producers.render(command, table))
				elif isinstance(command, ir.Query):
					lines.extend(queries.render_query(command, table, registry))
				elif isinstance(command, ir.ChainQuery):
					lines.extend(queries.render_chain_query(command, table, registry))
				else:
					lines.extend(commands.render(command, table))
				lines.append("")
			if namespace:
				lines.append("}")
				lines.append("")
		lines.append("}")

	return sections, warnings


SECTION_ORDER = ["objects", "enums", "param", "commands"]

# What each body needs on its own. The vkfu core headers are small and each is
# #pragma once, so they go into all of them rather than being tracked; what is
# worth splitting is the generated bulk.
_CORE_INCLUDES = [
	"#include <algorithm>",
	"#include <array>",
	"#include <bit>",
	"#include <cstddef>",
	"#include <cstdint>",
	"#include <expected>",
	"#include <iterator>",
	"#include <memory>",
	"#include <new>",
	"#include <span>",
	"#include <type_traits>",
	"",
	"#include <vulkan/vulkan.h>",
	"",
	'#include "{prefix}branch_pipe.h"',
	'#include "{prefix}chain.h"',
	'#include "{prefix}expression.h"',
	'#include "{prefix}extension.h"',
	'#include "{prefix}query.h"',
	'#include "{prefix}reference.h"',
	'#include "{prefix}storage.h"',
	'#include "{prefix}vulkan_object.h"',
]

SECTION_DEPENDENCIES = {
	"objects": [],
	"enums": [],
	"param": ["objects", "enums"],
	"commands": ["objects"],
}

SECTION_SUMMARY = {
	"objects": "tags, traits, extension requirements and the pNext hooks",
	"enums": "scoped enums over the native ones",
	"param": "the structures you fill in",
	"commands": "the wrappers that take expressions",
}


def _preamble(registry: ir.Registry, name: str, prefix: str, siblings: dict[str, str] | None) -> list[str]:
	lines = [
		"#pragma once",
		"",
		f"// Generated by vkfu_gen from vk.xml (VK_HEADER_VERSION {registry.header_version}). Do not edit.",
	]
	if siblings is not None:
		lines.append(f"// {name}: {SECTION_SUMMARY[name]}.")
	lines.append("")
	lines += [entry.format(prefix=prefix) for entry in _CORE_INCLUDES]
	if siblings is not None:
		for dependency in SECTION_DEPENDENCIES[name]:
			lines.append(f'#include "{siblings[dependency]}"')
	lines.append("")
	return lines


def generate(
	registry: ir.Registry, table: Table, scope: str, prefix: str = "../"
) -> tuple[str, list[str]]:
	"""One header, every section in dependency order."""
	sections, warnings = _build(registry, table, scope)
	lines = _preamble(registry, "objects", prefix, None)
	for name in SECTION_ORDER:
		lines.extend(sections[name])
	return NEWLINE.join(lines) + NEWLINE, warnings


def generate_split(
	registry: ir.Registry, table: Table, scope: str, stem: str, prefix: str = "../../"
) -> tuple[dict[str, str], list[str]]:
	"""One file per section, plus an umbrella that includes them in order.

	The point is that a translation unit which only records commands need not
	parse eight hundred param structures to do it.
	"""
	sections, warnings = _build(registry, table, scope)
	siblings = {name: f"{stem}.{name}.h" for name in SECTION_ORDER}
	files: dict[str, str] = {}
	for name in SECTION_ORDER:
		lines = _preamble(registry, name, prefix, siblings)
		lines.extend(sections[name])
		files[siblings[name]] = NEWLINE.join(lines) + NEWLINE
	umbrella = [
		"#pragma once",
		"",
		f"// Generated by vkfu_gen from vk.xml (VK_HEADER_VERSION {registry.header_version}). Do not edit.",
		"// Everything. Include one of the parts instead when that is all you need.",
		"",
	]
	umbrella += [f'#include "{stem}/{siblings[name]}"' for name in SECTION_ORDER]
	files[f"{stem}.h"] = NEWLINE.join(umbrella) + NEWLINE
	return files, warnings
