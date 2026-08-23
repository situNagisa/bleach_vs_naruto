"""Stage 2: IR + naming table -> C++ header.

Every identifier this stage writes comes from either vk.xml (native types,
members, enum constants) or the naming table (obj/param/field names). It never
derives a name itself; a missing table entry is an error, not a fallback.
"""

from __future__ import annotations

import dataclasses

from . import ir
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


class GenerationError(Exception):
	def __init__(self, missing: list[str], problems: list[str]) -> None:
		super().__init__("generation failed")
		self.missing = missing
		self.problems = problems


# ---------------------------------------------------------------- field plan


@dataclasses.dataclass
class Field:
	kind: str  # bool | scalar | flags | span | pointer | array
	member: ir.Member
	cpp_type: str  # declared type of the param field
	count_member: str | None = None  # span: the native count member it drives
	flag_bits: str | None = None  # flags: the VkXxxFlagBits enum
	layout: list[ir.Bit] | None = None  # flags: bits in ascending bitpos order


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


def _grouped(selected: list[str], table: Table) -> list[tuple[str, list[str]]]:
	"""Objects bucketed by the namespace their table name puts them in."""
	buckets: dict[str, list[str]] = {}
	for name in selected:
		buckets.setdefault(_namespace_of(table.struct_name(name)), []).append(name)
	return sorted(buckets.items(), key=lambda entry: (entry[0] != "", entry[0]))


def _render_flags_checks(field: Field, table: Table, field_name: str) -> list[str]:
	"""Prove the bit-field layout against the real enum constants.

	Bit-field allocation order is implementation-defined, so the mapping is
	asserted rather than assumed. A platform-guarded constant is only asserted
	where it exists; the field itself is unconditional so the layout never
	shifts between platforms.
	"""
	lines = [
		f"{TAB}static_assert(sizeof({field_name}_type) == sizeof({field.member.type}));",
		"#if !defined(__clang__) || !defined(_MSC_VER)",
	]
	for bit in field.layout:
		name = table.bit_name(field.flag_bits, bit.name)
		_guard_open(lines, bit.guards)
		lines.append(
			f"{TAB}static_assert(::std::bit_cast<{field.member.type}>({field_name}_type{{.{name} = 1}}) == {bit.name});"
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

	lines: list[str] = []
	_guard_open(lines, struct.guards)
	lines.append(f"struct {_leaf_of(object_name)}")
	lines.append("{")
	lines.append(f"{TAB}using vulkan_tag_type = obj::{object_name};")
	lines.append("")

	for field in plan.fields:
		field_name = names[field.member.name]
		if field.kind == "flags":
			lines.extend(_render_flags_type(field, registry, table, field_name))
		else:
			lines.append(f"{TAB}{field.cpp_type} {field_name}{_field_default(field)};")

	for field in plan.fields:
		if field.kind == "flags":
			lines.append("")
			lines.extend(_render_flags_checks(field, table, names[field.member.name]))

	arrays = [field for field in plan.fields if field.kind == "array"]
	by_field = {field.member.name: field for field in plan.fields}
	# A fixed-size native array cannot be filled by a designated initialiser, so
	# only those structures need a named temporary.
	head = f"{TAB}{TAB}auto value = {struct.name}{{" if arrays else f"{TAB}{TAB}return {struct.name}{{"
	inner = f"{TAB}{TAB}{TAB}"

	lines.append("")
	lines.append(f"{TAB}constexpr auto evaluate() const noexcept -> {struct.name}")
	lines.append(f"{TAB}{{")
	lines.append(head)
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
			continue
		if field.kind == "bool":
			lines.append(f"{inner}.{member.name} = {field_name} ? VK_TRUE : VK_FALSE,")
		elif field.kind == "span":
			lines.append(f"{inner}.{member.name} = {field_name}.data(),")
		elif field.kind == "flags":
			lines.append(f"{inner}.{member.name} = ::std::bit_cast<{member.type}>({field_name}),")
		else:
			lines.append(f"{inner}.{member.name} = {field_name},")

	lines.append(f"{TAB}{TAB}}};")
	if arrays:
		for field in arrays:
			field_name = names[field.member.name]
			lines.append(f"{TAB}{TAB}::std::ranges::copy({field_name}, value.{field.member.name});")
		lines.append(f"{TAB}{TAB}return value;")
	lines.append(f"{TAB}}}")
	lines.append("};")
	_guard_close(lines, struct.guards)
	return lines


NATIVE_GLUE = """\
// vkfu::address / vkfu::set_next reach native Vulkan structures through ADL, so
// these have to live in the namespace those structures are declared in.
template<class T>
	requires requires(T& value) { value.sType; value.pNext; }
constexpr auto address(T& value) noexcept -> void const*
{
	return ::std::addressof(value);
}

template<class T>
	requires requires(T& value) { value.sType; value.pNext; }
constexpr void set_next(T& value, void const* next) noexcept
{
	if constexpr (::std::is_const_v<::std::remove_pointer_t<decltype(value.pNext)>>)
	{
		value.pNext = next;
	}
	else
	{
		value.pNext = const_cast<void*>(next);
	}
}
"""


def generate(registry: ir.Registry, table: Table, scope: str) -> tuple[str, list[str]]:
	missing: list[str] = []
	problems: list[str] = []
	warnings: list[str] = []

	for named in table.structs:
		if named not in registry.closure:
			warnings.append(f"naming.toml names {named}, which is not in the vkCreate* closure; ignored")

	if scope == "table":
		selected = [name for name in registry.closure if table.struct_name(name)]
	else:
		selected = list(registry.closure)
		for name in selected:
			if not table.struct_name(name):
				missing.append(f"struct.{name}")

	seen: dict[str, str] = {}
	for name in selected:
		object_name = table.struct_name(name)
		if object_name is None:
			continue  # already reported as missing
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
			if field_name == _leaf_of(object_name):
				# Legal for an aggregate, but the member hides the injected class
				# name inside the struct, so it is worth a look.
				warnings.append(f"{name}: field '{field_name}' has the same name as its object")
			if field.kind == "flags":
				for bit in field.layout:
					if not table.bit_name(field.flag_bits, bit.name):
						missing.append(f"bit.{field.flag_bits}.{bit.name}")

	if missing or problems:
		raise GenerationError(sorted(set(missing)), sorted(set(problems)))

	emitted = set(selected)
	lines: list[str] = [
		"#pragma once",
		"",
		f"// Generated by vkfu_gen from vk.xml (VK_HEADER_VERSION {registry.header_version}). Do not edit.",
		f"// Scope: vkCreate* create-info roots and their pNext closure ({len(selected)} objects).",
		"",
		"#include <algorithm>",
		"#include <array>",
		"#include <bit>",
		"#include <cstdint>",
		"#include <memory>",
		"#include <span>",
		"#include <type_traits>",
		"",
		"#include <vulkan/vulkan.h>",
		"",
		'#include "../branch_pipe.h"',
		'#include "../expression.h"',
		'#include "../storage.h"',
		'#include "../vulkan_object.h"',
		"",
		NATIVE_GLUE,
		"namespace vkfu::obj",
		"{",
	]

	for namespace, names in _grouped(selected, table):
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

	roots = set(registry.roots)
	branches = set(registry.branches)
	for name in selected:
		struct = registry.structs[name]
		object_name = table.struct_name(name)
		_guard_open(lines, struct.guards)
		lines.append(f"template<>")
		lines.append(f"struct vulkan_object_trait<obj::{object_name}>")
		lines.append("{")
		lines.append(f"{TAB}constexpr static auto root = {'true' if name in roots else 'false'};")
		lines.append(f"{TAB}constexpr static auto branch = {'true' if name in branches else 'false'};")
		lines.append(f"{TAB}constexpr static auto allow_duplicate = {'true' if struct.allow_duplicate else 'false'};")
		lines.append("};")
		_guard_close(lines, struct.guards)
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

	lines += ["}", "", "namespace vkfu::param", "{"]

	for namespace, names in _grouped(selected, table):
		if namespace:
			lines.append(f"namespace {namespace}")
			lines.append("{")
		for name in names:
			lines.extend(_render_param(registry.structs[name], plans[name], registry, table, table.struct_name(name)))
			lines.append("")
		if namespace:
			lines.append("}")
			lines.append("")

	lines.append("}")
	return "\n".join(lines) + "\n", warnings
