"""Mechanical checks on the naming table.

Every check here is decidable from the table and the IR alone -- no judgement,
no compiler. That is the point: an empty leaf, a duplicate, a C++ keyword or a
name that is both a namespace and a class are all errors a machine can find in
a second, and none of them should ever reach a build.

`gen` performs the same checks on the subset it is about to emit. This module
covers the whole table, so `rebuild` and `suggest` can fail before anyone tries
to compile anything.
"""

from __future__ import annotations

import dataclasses
import re

from . import emit, ir, producers
from .naming import CPP_KEYWORDS, Table


# Names vkfu itself declares in `namespace vkfu`. A generated command landing on
# one of these would hide a CPO rather than fail to compile, so it is checked
# rather than left to chance.
VKFU_NAMES = frozenset(
	{
		"absent_expression",
		"absent_storage",
		"address",
		"basic_storage",
		"branch_expression",
		"chain",
		"duplicates_feature",
		"enums",
		"evaluate",
		"expression",
		"expression_for",
		"expression_storage_t",
		"expression_vulkan_tag",
		"expression_vulkan_tag_t",
		"obj",
		"param",
		"reference_expression_for",
		"reference_slot",
		"reference_storage",
		"reference_storage_of_t",
		"set_next",
		"storable",
		"unpack",
		"vulkan_object",
		"vulkan_object_trait",
	}
)

_RESERVED = re.compile(r"__|^_[A-Z]")


@dataclasses.dataclass(frozen=True, order=True)
class Problem:
	severity: str  # error | warning
	kind: str
	where: str  # the vk.xml key the entry belongs to
	detail: str

	def __str__(self) -> str:
		return f"{self.severity}: {self.kind}: {self.where}: {self.detail}"


class _Sink:
	def __init__(self) -> None:
		self.problems: list[Problem] = []

	def error(self, kind: str, where: str, detail: str) -> None:
		self.problems.append(Problem("error", kind, where, detail))

	def warn(self, kind: str, where: str, detail: str) -> None:
		self.problems.append(Problem("warning", kind, where, detail))

	def name(self, where: str, value: str | None, *, qualified: bool = False) -> bool:
		"""The checks that apply to any name at all. False means unusable."""
		if value is None:
			return False
		if not value:
			self.error("empty", where, "the table entry is an empty string")
			return False
		segments = value.split("::") if qualified else [value]
		if not segments[-1]:
			self.error("empty", where, f"'{value}' has no leaf name")
			return False
		ok = True
		for segment in segments:
			if not segment:
				self.error("empty", where, f"'{value}' has an empty namespace segment")
				ok = False
			elif not segment.isidentifier():
				self.error("invalid", where, f"'{segment}' is not a C++ identifier")
				ok = False
			elif segment in CPP_KEYWORDS:
				self.error("keyword", where, f"'{segment}' is a C++ keyword")
				ok = False
			elif _RESERVED.search(segment):
				# Reserved to the implementation, so the program is ill-formed
				# even though most compilers stay quiet about it.
				self.error("reserved", where, f"'{segment}' is reserved to the implementation")
				ok = False
		return ok


def _unique(sink: _Sink, kind: str, where: str, entries: list[tuple[str, str]]) -> None:
	"""Report any name claimed twice. `entries` is (source key, chosen name)."""
	claims: dict[str, str] = {}
	for source, value in entries:
		if value in claims:
			sink.error(kind, where, f"{claims[value]} and {source} both map to '{value}'")
		else:
			claims[value] = source


def _namespace_conflicts(sink: _Sink, area: str, names: dict[str, str]) -> None:
	"""A leaf and a namespace segment cannot be the same identifier.

	`param::feature` as a struct and `param::feature::nv` as a namespace is a
	redefinition of `feature` as a different kind of symbol -- a hard error, and
	one that only shows up once both entries exist.
	"""
	leaves: dict[str, str] = {}
	namespaces: dict[str, str] = {}
	for source, value in names.items():
		segments = value.split("::")
		leaves.setdefault("::".join(segments), source)
		for depth in range(1, len(segments)):
			namespaces.setdefault("::".join(segments[:depth]), source)
	for path, source in leaves.items():
		if path in namespaces:
			sink.error(
				"namespace",
				area,
				f"'{path}' is a class ({source}) and a namespace ({namespaces[path]})",
			)


def check(registry: ir.Registry, table: Table, scope: str = "closure") -> list[Problem]:
	"""Every mechanical complaint about `table`, worst first."""
	sink = _Sink()

	chain_objects = list(registry.closure) + list(registry.plain)
	in_scope = set(chain_objects) | set(registry.query_closure)

	# ------------------------------------------------------------ objects
	object_names: dict[str, str] = {}
	for name in sorted(in_scope):
		value = table.struct_name(name)
		if value is None:
			if scope != "table":
				sink.error("missing", f"struct.{name}", "no name in the table")
			continue
		if sink.name(f"struct.{name}", value, qualified=True):
			object_names[name] = value

	_unique(sink, "duplicate", "objects", sorted(object_names.items()))
	_namespace_conflicts(sink, "objects", object_names)

	for name in sorted(table.structs):
		if name not in in_scope:
			sink.warn("orphan", f"struct.{name}", "named in the table but not in scope")

	# ------------------------------------------------------------ fields
	for name in sorted(chain_objects):
		if name not in registry.structs:
			continue
		plan = emit.plan_struct(registry.structs[name], registry)
		for entry in plan.unsupported:
			sink.error("shape", f"struct.{name}", entry)

		claimed: list[tuple[str, str]] = []
		# Everything the param declares at class scope has to be distinct, not
		# just the fields: a flags field adds `<field>_type`, a slot adds
		# `<field>_expression`, and the aliases are always there.
		scope_entries: list[tuple[str, str]] = [("vulkan_tag_type", "<alias>")]
		if any(field.kind == "reference" for field in plan.fields):
			scope_entries.append(("storage_type", "<alias>"))

		for field in plan.fields:
			key = f"member.{name}.{field.member.name}"
			value = table.member_name(name, field.member.name)
			if value is None:
				if scope != "table":
					sink.error("missing", key, "no name in the table")
				continue
			if not sink.name(key, value):
				continue
			claimed.append((field.member.name, value))
			scope_entries.append((value, field.member.name))
			if field.kind == "flags":
				scope_entries.append((f"{value}_type", f"{field.member.name} (flags type)"))
			if field.kind == "reference":
				scope_entries.append((f"{value}_expression", f"{field.member.name} (slot)"))
			if value == object_names.get(name, "").rpartition("::")[2]:
				sink.warn("shadow", key, f"'{value}' is also the object's own name")
			if field.kind == "enum" and value == table.enum_name(field.enum_type):
				sink.warn("shadow", key, f"'{value}' hides the enum class it is typed with")

		_unique(sink, "duplicate", f"struct.{name}", claimed)
		_unique(sink, "scope", f"struct.{name}", [(source, entry) for entry, source in scope_entries])

	# ------------------------------------------------------------ flag bits
	used_bits: dict[str, str] = {}
	for name in chain_objects:
		if name not in registry.structs:
			continue
		for field in emit.plan_struct(registry.structs[name], registry).fields:
			if field.kind == "flags":
				used_bits.setdefault(field.flag_bits, name)

	for flag_bits in sorted(used_bits):
		layout = emit._bit_layout(registry.flagbits[flag_bits]) or []
		claimed = []
		for bit in layout:
			key = f"bit.{flag_bits}.{bit.name}"
			value = table.bit_name(flag_bits, bit.name)
			if value is None:
				if scope != "table":
					sink.error("missing", key, "no name in the table")
				continue
			if sink.name(key, value):
				claimed.append((bit.name, value))
		_unique(sink, "duplicate", f"bits.{flag_bits}", claimed)

	# ------------------------------------------------------------ enums
	enum_types = emit.used_enums(registry.closure, registry)
	enum_names: list[tuple[str, str]] = []
	for enum_type in enum_types:
		key = f"enum.{enum_type}"
		value = table.enum_name(enum_type)
		if value is None:
			if scope != "table":
				sink.error("missing", key, "no name in the table")
		elif sink.name(key, value):
			enum_names.append((enum_type, value))

		claimed = []
		for entry in registry.enums[enum_type].values:
			value_key = f"value.{enum_type}.{entry.name}"
			named = table.value_name(enum_type, entry.name)
			if named is None:
				if scope != "table":
					sink.error("missing", value_key, "no name in the table")
				continue
			if sink.name(value_key, named):
				claimed.append((entry.name, named))
		_unique(sink, "duplicate", f"enum.{enum_type}", claimed)

	_unique(sink, "duplicate", "enums", enum_names)

	# ------------------------------------------------------------ commands
	command_names: list[tuple[str, str]] = []
	for command in list(registry.producers) + list(registry.wrappers) + list(registry.queries) + list(registry.chain_queries):
		key = f"command.{command.name}"
		value = table.command_name(command.name)
		if value is None:
			if scope != "table":
				sink.error("missing", key, "no name in the table")
			continue
		if not sink.name(key, value, qualified=True):
			continue
		command_names.append((command.name, value))
		if "::" not in value and value in VKFU_NAMES:
			sink.error("shadow", key, f"'{value}' is already declared in namespace vkfu")
		if isinstance(command, ir.Query):
			count_key = f"command.{command.name}.count"
			counted = table.command_count(command.name)
			if counted is None:
				if scope != "table":
					sink.error("missing", count_key, "no name in the table")
			elif sink.name(count_key, counted, qualified=True):
				command_names.append((f"{command.name} (count)", counted))
		if isinstance(command, ir.Command) and producers.shape_of(command) == "array":
			singular_key = f"command.{command.name}.singular"
			singular = table.command_singular(command.name)
			if singular is None:
				if scope != "table":
					sink.error("missing", singular_key, "no name in the table")
			elif sink.name(singular_key, singular, qualified=True):
				command_names.append((f"{command.name} (singular)", singular))

	_unique(sink, "duplicate", "commands", command_names)
	_namespace_conflicts(sink, "commands", dict(command_names))

	# ------------------------------------------------------------ aliases
	# An alias shares a namespace with the objects, so it competes with them for
	# names -- and a `using` that collides with a real structure is a redefinition.
	claimed = {value: source for source, value in object_names.items()}
	everything = dict(object_names)
	for alias in sorted(registry.aliases, key=lambda entry: entry.name):
		key = f"alias.{alias.name}"
		value = table.alias_name(alias.name)
		if value is None:
			continue  # an alias without a name is simply not generated
		if alias.target not in object_names:
			sink.warn("orphan", key, f"aliases {alias.target}, which is not in scope")
			continue
		if not sink.name(key, value, qualified=True):
			continue
		if value in claimed:
			sink.error("duplicate", "aliases", f"{claimed[value]} and {alias.name} both map to '{value}'")
		else:
			claimed[value] = alias.name
			everything[alias.name] = value

	_namespace_conflicts(sink, "aliases", everything)

	for name in sorted(table.aliases):
		if all(alias.name != name for alias in registry.aliases):
			sink.warn("orphan", f"alias.{name}", "named in the table but not an alias in scope")

	return sorted(set(sink.problems), key=lambda problem: (problem.severity != "error", problem.kind, problem.where))


def summarise(problems: list[Problem]) -> tuple[int, int]:
	errors = sum(1 for problem in problems if problem.severity == "error")
	return errors, len(problems) - errors
