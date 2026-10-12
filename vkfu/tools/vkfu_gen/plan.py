"""How each native member becomes a param field.

Shared by the renderer, the checker and the table rebuild: all three have to
agree on which members exist as fields and of what kind, or a table that
checks clean could still fail to generate.
"""

from __future__ import annotations

import dataclasses

from . import ir
from .cpp import base_type, pointer_type, span_element_type


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


def bit_layout(flag_bits: ir.FlagBits) -> list[ir.Bit] | None:
	"""None when the bits cannot be laid out one-per-position."""
	bits = positional_bits(flag_bits)
	positions = [bit.bitpos for bit in bits]
	if len(set(positions)) != len(positions):
		return None
	if positions and max(positions) >= flag_bits.width:
		return None
	return bits


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
			element = base_type(member.type)
			if member.ptr_depth:
				element = pointer_type(member)
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
				cpp_type=f"::std::span<{span_element_type(member)}>",
				count_member=folded_pointers[member.name],
			)
			fields.append(field)
			consumed[field.count_member] = field
			continue

		if ir.is_reference(member, registry.structs.get(member.type)):
			fields.append(Field(kind="reference", member=member, cpp_type=member.type, target=member.type))
			continue

		if member.ptr_depth >= 1:
			fields.append(Field(kind="pointer", member=member, cpp_type=pointer_type(member)))
			continue

		if member.type == "VkBool32":
			fields.append(Field(kind="bool", member=member, cpp_type="bool"))
			continue

		flag_bits = registry.bitmask_to_flagbits.get(member.type)
		if flag_bits and registry.flagbits.get(flag_bits):
			layout = bit_layout(registry.flagbits[flag_bits])
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

		fields.append(Field(kind="scalar", member=member, cpp_type=base_type(member.type)))

	return Plan(struct=struct, fields=fields, consumed_counts=consumed, unsupported=unsupported)


def used_enums(selected: list[str], registry: ir.Registry) -> list[str]:
	"""Plain enums that some selected object exposes as a field."""
	names: list[str] = []
	for name in list(selected) + list(registry.plain):
		for field in plan_struct(registry.structs[name], registry).fields:
			if field.kind == "enum" and field.enum_type not in names:
				names.append(field.enum_type)
	return sorted(names)
