"""Stage 2: IR + naming table -> C++ headers, one per Vulkan module.

Every identifier this stage writes comes from either vk.xml (native types,
members, enum constants) or the naming table (obj/param/field names). It never
derives a name itself; a missing table entry is an error, not a fallback.

A module is a <feature> or an <extension>: VK_VERSION_1_2, VK_KHR_swapchain.
Every type and command belongs to the one that first requires it (ir.origins),
so a header holds exactly what enabling that module gives you -- its tags,
enums, params and wrappers together -- rather than one kind of thing for the
whole API.
"""

from __future__ import annotations

import dataclasses

from . import commands, ir, layout, producers, queries, structs
from .cpp import NEWLINE, TAB, bucketed, leaf_of
from .naming import Table
from .plan import Plan, plan_struct, used_enums


class GenerationError(Exception):
	def __init__(self, missing: list[str], problems: list[str]) -> None:
		super().__init__("generation failed")
		self.missing = missing
		self.problems = problems


# ---------------------------------------------------------------- selection


@dataclasses.dataclass
class Selection:
	"""What survives validation, i.e. what the headers will contain."""

	selected: list[str]  # closure objects: tag, trait, glue and a param
	queried: list[str]  # query objects: tag, trait and glue, no param
	plain: list[str]  # sType-less structures: a param only
	plans: dict[str, Plan]
	enums: list[str]
	aliases: dict[str, ir.Alias]  # alias name -> alias, for those with a table name
	commands: list[object]  # ir.Command | ir.Wrapper | ir.Query | ir.ChainQuery

	@property
	def chained(self) -> list[str]:
		return self.selected + self.queried

	@property
	def emitted(self) -> set[str]:
		return set(self.selected) | set(self.queried)


def _select(registry: ir.Registry, table: Table, scope: str) -> tuple[Selection, list[str]]:
	"""Everything that will be emitted, after every check that can stop it."""
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
			if object_name is not None and field_name == leaf_of(object_name):
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
		class_scope: dict[str, str] = {}
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
			if entry in class_scope:
				problems.append(f"{name}: '{entry}' is both a {class_scope[entry]} and a {kind}")
			else:
				class_scope[entry] = kind

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

	known = set(selected) | set(plain_selected)

	def takes_known(arguments: list[ir.Argument]) -> bool:
		return all(argument.member.type in known for argument in arguments if argument.kind == "info")

	wrapped: list[object] = [
		command
		for command in registry.producers
		if command.info.type in emitted and table.command_name(command.name)
	]
	wrapped += [
		command
		for command in registry.wrappers
		if table.command_name(command.name) and takes_known(command.arguments)
	]
	wrapped += [query for query in registry.queries if table.command_name(query.name)]
	wrapped += [
		query
		for query in registry.chain_queries
		if table.command_name(query.name) and query.out.type in emitted and takes_known(query.arguments)
	]

	selection = Selection(
		selected=selected,
		queried=queried,
		plain=plain_selected,
		plans=plans,
		enums=enums,
		aliases=by_alias,
		commands=wrapped,
	)
	return selection, warnings


# ---------------------------------------------------------------- units


@dataclasses.dataclass
class Unit:
	"""One generated file, and everything declared in it. See layout.py."""

	path: str  # khr/swapchain.h, version_1_3/synchronization2.h
	module: str  # VK_KHR_swapchain, VK_VERSION_1_3
	chained: list[str] = dataclasses.field(default_factory=list)
	selected: list[str] = dataclasses.field(default_factory=list)
	plain: list[str] = dataclasses.field(default_factory=list)
	enums: list[str] = dataclasses.field(default_factory=list)
	aliases: list[str] = dataclasses.field(default_factory=list)
	edges: list[tuple[str, str]] = dataclasses.field(default_factory=list)
	commands: list[object] = dataclasses.field(default_factory=list)
	depends: set[str] = dataclasses.field(default_factory=set)  # unit paths
	guard: str | None = None  # its own macro
	condition: list[str] = dataclasses.field(default_factory=list)  # its own plus its dependencies'


def _info_types(command: object, registry: ir.Registry) -> list[str]:
	"""The structures a wrapper names by tag, so it needs their unit."""
	if isinstance(command, ir.Command):
		return [command.info.type]
	if isinstance(command, ir.Query):
		return [
			argument.member.type
			for argument in queries._classified(command.leading, registry)
			if argument.kind == "info"
		]
	names = [argument.member.type for argument in command.arguments if argument.kind == "info"]
	if isinstance(command, ir.ChainQuery):
		names.append(command.out.type)
	return names


def _units(registry: ir.Registry, selection: Selection) -> list[Unit]:
	"""Selection split by owning unit, in an order every include can follow.

	A unit depends on another when something it declares names something the
	other declares: an enum-typed field, a slot's tag, an alias's target, the tag
	a wrapper takes. A compat edge names two tags, so it goes to one of its two
	units and makes that one depend on the other: to the guarded one if only one
	is guarded, so an unguarded unit never inherits a platform macro, otherwise
	to the later one.
	"""
	placement = layout.place(registry.origins)
	units: dict[str, Unit] = {}
	unowned: list[str] = []

	def unit_of(path: str) -> Unit:
		if path not in units:
			units[path] = Unit(path=path, module=placement.modules[path], guard=placement.guards.get(path))
		return units[path]

	def owner(name: str) -> Unit | None:
		path = placement.units.get(name)
		if path is None:
			unowned.append(name)
			return None
		return unit_of(path)

	def put(name: str, bucket: str) -> Unit | None:
		unit = owner(name)
		if unit is not None:
			getattr(unit, bucket).append(name)
		return unit

	def needs(unit: Unit | None, name: str) -> None:
		other = placement.units.get(name)
		if unit is not None and other is not None and other != unit.path:
			unit.depends.add(other)

	for name in selection.chained:
		put(name, "chained")
	for name in selection.selected:
		unit = put(name, "selected")
		for field in selection.plans[name].fields:
			if field.kind == "enum":
				needs(unit, field.enum_type)
			elif field.kind == "reference":
				needs(unit, field.target)
	for name in selection.plain:
		unit = put(name, "plain")
		for field in plan_struct(registry.structs[name], registry).fields:
			if field.kind == "enum":
				needs(unit, field.enum_type)
	for name in selection.enums:
		put(name, "enums")
	for name, alias in selection.aliases.items():
		needs(put(name, "aliases"), alias.target)
	for command in selection.commands:
		unit = owner(command.name)
		if unit is None:
			continue
		unit.commands.append(command)
		for name in _info_types(command, registry):
			needs(unit, name)

	emitted = selection.emitted
	for parent, child in registry.edges:
		if parent not in emitted or child not in emitted:
			continue
		ends = [placement.units.get(parent), placement.units.get(child)]
		if None in ends:
			continue
		chosen = max(ends, key=lambda path: (path in placement.guards, placement.rank[path]))
		unit = unit_of(chosen)
		unit.edges.append((parent, child))
		needs(unit, parent)
		needs(unit, child)

	if unowned:
		raise GenerationError([], [f"{name}: no <feature> or <extension> requires it" for name in sorted(set(unowned))])

	ordered = _ordered(units, placement.rank)
	# A unit is only usable where everything it includes is, so its condition is
	# its own macro plus every dependency's -- computed in include order, so each
	# dependency's condition is already final.
	by_path = {unit.path: unit for unit in ordered}
	for unit in ordered:
		condition = {unit.guard} if unit.guard else set()
		for path in unit.depends:
			condition |= set(by_path[path].condition)
		unit.condition = sorted(condition)
	return ordered


def _ordered(units: dict[str, Unit], rank: dict[str, tuple[int, int]]) -> list[Unit]:
	"""Dependencies first; registry order wherever the includes leave a choice."""
	pending = {path: set(unit.depends) & set(units) for path, unit in units.items()}
	order: list[Unit] = []
	while pending:
		ready = sorted((path for path, depends in pending.items() if not depends), key=lambda path: rank[path])
		if not ready:
			cycle = ", ".join(sorted(pending, key=lambda path: rank[path]))
			raise GenerationError([], [f"generated files depend on each other in a cycle: {cycle}"])
		path = ready[0]
		order.append(units[path])
		del pending[path]
		for depends in pending.values():
			depends.discard(path)
	return order


# ---------------------------------------------------------------- rendering


def _render_module(module: Unit, registry: ir.Registry, table: Table, selection: Selection) -> list[str]:
	"""Everything one module declares, each kind in the order the next needs it:
	glue and tags, enums, traits and edges, aliases, params, then wrappers."""
	lines: list[str] = []
	struct_name = table.struct_name

	for name in module.chained:
		lines.extend(structs.render_native_glue(registry.structs[name]))
		lines.append("")

	if module.enums:
		lines += ["namespace vkfu::enums", "{"]
		for enum_type in module.enums:
			lines.extend(structs.render_enum(registry.enums[enum_type], table))
			lines.append("")
		lines += ["}", ""]

	if module.chained or module.edges:
		if module.chained:
			lines += ["namespace vkfu::obj", "{"]
			for namespace, names in bucketed(module.chained, struct_name):
				if namespace:
					lines += [f"namespace {namespace}", "{"]
				for name in names:
					struct = registry.structs[name]
					lines.append(f"struct {leaf_of(struct_name(name))}{{}};")
				if namespace:
					lines.append("}")
			lines += ["}", ""]
		lines += ["namespace vkfu", "{"]
		lines.extend(_render_traits(module, registry, table))
		lines.append("}")
		lines.append("")

	if module.aliases:
		lines += ["namespace vkfu::obj", "{"]
		for namespace, names in bucketed(module.aliases, table.alias_name):
			if namespace:
				lines += [f"namespace {namespace}", "{"]
			for name in names:
				alias = selection.aliases[name]
				lines.extend(
					structs.render_alias(alias, table.alias_name(name), struct_name(alias.target), [], table, "obj")
				)
			if namespace:
				lines.append("}")
		lines += ["}", ""]

	param_targets = set(selection.selected) | set(selection.plain)
	param_aliases = [name for name in module.aliases if selection.aliases[name].target in param_targets]
	if module.selected or module.plain or param_aliases:
		lines += ["namespace vkfu::param", "{"]
		lines.extend(_render_params(module, param_aliases, registry, table, selection))
		lines += ["}", ""]

	if module.commands:
		lines += ["namespace vkfu", "{"]
		lines.extend(_render_commands(module.commands, registry, table))
		lines += ["}", ""]

	while lines and lines[-1] == "":
		lines.pop()
	return lines


def _render_traits(module: Unit, registry: ir.Registry, table: Table) -> list[str]:
	lines: list[str] = []
	# A query chain needs a head as much as a create chain does, so the structures
	# a command writes into count as roots too.
	roots = set(registry.roots) | set(registry.query_roots)
	branches = set(registry.branches)
	for name in module.chained:
		struct = registry.structs[name]
		lines.append("template<>")
		lines.append(f"struct vulkan_object_trait<obj::{table.struct_name(name)}>")
		lines.append("{")
		lines.append(f"{TAB}constexpr static auto root = {'true' if name in roots else 'false'};")
		lines.append(f"{TAB}constexpr static auto branch = {'true' if name in branches else 'false'};")
		lines.append(f"{TAB}constexpr static auto allow_duplicate = {'true' if struct.allow_duplicate else 'false'};")
		lines.append("};")
		lines.append("")

	for name in module.chained:
		struct = registry.structs[name]
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
		lines.append("")

	for name in module.chained:
		provenance = registry.provenance.get(name)
		if provenance is None:
			continue
		lines.extend(
			structs.render_extensions(name, table.struct_name(name), provenance, registry.structs[name].guards)
		)
		lines.append("")

	for parent, child in module.edges:
		guards = sorted(set(registry.structs[parent].guards) | set(registry.structs[child].guards))
		lines.append("template<>")
		lines.append(
			f"inline constexpr auto is_vulkan_object_compatible_with_v<obj::{table.struct_name(parent)}, obj::{table.struct_name(child)}> = true;"
		)
	return lines


def _render_params(
	module: Unit, aliases: list[str], registry: ir.Registry, table: Table, selection: Selection
) -> list[str]:
	lines: list[str] = []
	for namespace, names in bucketed(module.selected, table.struct_name):
		if namespace:
			lines += [f"namespace {namespace}", "{"]
		# Argument-dependent lookup only associates a class's innermost enclosing
		# namespace, so vkfu::operator| has to be visible from each one.
		lines.append("using ::vkfu::operator|;")
		lines.append("")
		for name in names:
			lines.extend(
				structs.render_param(registry.structs[name], selection.plans[name], registry, table, table.struct_name(name))
			)
			lines.append("")
		if namespace:
			lines += ["}", ""]

	# Structures with no sType: a param and an evaluate(), nothing else. They have
	# no pNext to manage, so no tag, no trait and no chain hooks.
	for namespace, names in bucketed(module.plain, table.struct_name):
		if namespace:
			lines += [f"namespace {namespace}", "{"]
		for name in names:
			struct = registry.structs[name]
			lines.extend(structs.render_param(struct, plan_struct(struct, registry), registry, table, table.struct_name(name)))
			lines.append("")
		if namespace:
			lines += ["}", ""]

	for namespace, names in bucketed(aliases, table.alias_name):
		if namespace:
			lines += [f"namespace {namespace}", "{"]
		for name in names:
			alias = selection.aliases[name]
			slots = [
				field
				for field in plan_struct(registry.structs[alias.target], registry).fields
				if field.kind == "reference"
			]
			lines.extend(
				structs.render_alias(alias, table.alias_name(name), table.struct_name(alias.target), slots, table, "param")
			)
		if namespace:
			lines.append("}")
		lines.append("")

	while lines and lines[-1] == "":
		lines.pop()
	return lines


def _render_commands(wrapped: list[object], registry: ir.Registry, table: Table) -> list[str]:
	lines: list[str] = []
	for namespace, group in bucketed(wrapped, lambda command: table.command_name(command.name)):
		if namespace:
			lines += [f"namespace {namespace}", "{"]
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
			lines += ["}", ""]
	while lines and lines[-1] == "":
		lines.pop()
	return lines


# ---------------------------------------------------------------- files

# The vkfu core headers are small and each is #pragma once, so every unit
# includes all of them rather than tracking which it uses; what is worth
# splitting is the generated bulk.
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
	"#include <vkfu/branch_pipe.h>",
	"#include <vkfu/chain.h>",
	"#include <vkfu/expression.h>",
	"#include <vkfu/extension.h>",
	"#include <vkfu/query.h>",
	"#include <vkfu/reference.h>",
	"#include <vkfu/storage.h>",
	"#include <vkfu/vulkan_object.h>",
]


def _banner(registry: ir.Registry) -> str:
	return f"// Generated by vkfu_gen from vk.xml (VK_HEADER_VERSION {registry.header_version}). Do not edit."


def _condition(guards: list[str]) -> str:
	# A unit needs every macro its includes need, so AND rather than OR.
	return "#if " + " && ".join(f"defined({guard})" for guard in guards)


def _summary(unit: Unit) -> str:
	counts = [
		(len(unit.chained), "object"),
		(len(unit.plain), "plain structure"),
		(len(unit.enums), "enum"),
		(len(unit.aliases), "alias"),
		(len(unit.commands), "command"),
	]
	parts = [f"{count} {noun}{'' if count == 1 else 's'}" for count, noun in counts if count]
	text = f"{unit.module}: " + (", ".join(parts) if parts else "compatibility edges only") + "."
	if unit.condition:
		text += "\n// Needs " + " and ".join(unit.condition) + "; the umbrella only includes it then."
	return text


def _build(registry: ir.Registry, table: Table, scope: str) -> tuple[list[Unit], dict[str, list[str]], list[str]]:
	selection, warnings = _select(registry, table, scope)
	units = _units(registry, selection)
	bodies = {unit.path: _render_module(unit, registry, table, selection) for unit in units}
	return units, bodies, warnings


def _included(lines: list[str], includes: list[tuple[str, list[str]]]) -> None:
	"""`#include` each path, consecutive ones under the same condition sharing one #if."""
	current: list[str] = []
	for path, condition in includes:
		if condition != current:
			if current:
				lines.append("#endif")
			if condition:
				lines.append(_condition(condition))
			current = condition
		lines.append(f'#include "{path}"')
	if current:
		lines.append("#endif")


# Where the tree goes, relative to the directory that holds vkfu's own headers.
# Consumers write `#include <vkfu/vulkan/v1.4.328.h>`.
OUTPUT_DIRECTORY = "vulkan"


def stem_of(registry: ir.Registry) -> str:
	"""v1.4.328: names both the umbrella and the directory beside it."""
	return f"v{registry.api_version}"


def generate(
	registry: ir.Registry, table: Table, scope: str
) -> tuple[dict[str, str], list[str]]:
	"""The tree under `vulkan/v<version>/`, plus `vulkan/v<version>.h` including everything.

	    vulkan/v1.4.328.h                      every core version and every vendor
	    vulkan/v1.4.328/version_1_0.h          a core version's umbrella
	    vulkan/v1.4.328/version_1_0/buffer.h   cut by vk.xml's <require> blocks
	    vulkan/v1.4.328/khr.h                  a vendor's umbrella
	    vulkan/v1.4.328/khr/swapchain.h        one extension

	Paths in the result are relative to the directory holding vkfu's headers.
	The umbrella's own includes are relative to it, so nothing below depends
	on where that directory is.
	"""
	stem = stem_of(registry)
	units, bodies, warnings = _build(registry, table, scope)
	files: dict[str, str] = {}
	groups: dict[str, list[Unit]] = {}
	for unit in units:
		lines = ["#pragma once", "", _banner(registry), f"// {_summary(unit)}", ""] + _CORE_INCLUDES + [""]
		depends = [other for other in units if other.path in unit.depends]
		if depends:
			lines += [f'#include "{layout.relative(unit.path, other.path)}"' for other in depends]
			lines.append("")
		lines.extend(bodies[unit.path])
		files[f"{stem}/{unit.path}"] = NEWLINE.join(lines) + NEWLINE
		groups.setdefault(layout.umbrella_of(unit.path), []).append(unit)

	def umbrella(summary: list[str], includes: list[tuple[str, list[str]]]) -> str:
		lines = ["#pragma once", "", _banner(registry)] + [f"// {line}" for line in summary] + [""]
		_included(lines, includes)
		return NEWLINE.join(lines) + NEWLINE

	# Core versions first, then vendors alphabetically.
	names = sorted(groups, key=lambda name: (not name.startswith("version_"), name))
	for name in names:
		members = groups[name]
		label = members[0].module if layout.is_core(members[0].module) else f"Every {name[:-2].upper()} extension"
		files[f"{stem}/{name}"] = umbrella(
			[f"{label}. Each file below includes what it depends on, so including one", "of them directly works too."],
			[(layout.relative(name, unit.path), unit.condition) for unit in members],
		)
	files[f"{stem}.h"] = umbrella(
		["Every core version and every vendor's extensions."],
		[(f"{stem}/{name}", []) for name in names],
	)
	return {f"{OUTPUT_DIRECTORY}/{path}": body for path, body in files.items()}, warnings
