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

from . import commands, ir, producers, queries, structs
from .cpp import NEWLINE, TAB, bucketed, guard_close, guard_open, leaf_of, namespace_of
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


# ---------------------------------------------------------------- modules


@dataclasses.dataclass
class Module:
	"""One <feature> or <extension>, and everything it introduces."""

	name: str  # VK_VERSION_1_0, VK_KHR_swapchain
	chained: list[str] = dataclasses.field(default_factory=list)
	selected: list[str] = dataclasses.field(default_factory=list)
	plain: list[str] = dataclasses.field(default_factory=list)
	enums: list[str] = dataclasses.field(default_factory=list)
	aliases: list[str] = dataclasses.field(default_factory=list)
	edges: list[tuple[str, str]] = dataclasses.field(default_factory=list)
	commands: list[object] = dataclasses.field(default_factory=list)
	depends: set[str] = dataclasses.field(default_factory=set)

	@property
	def file(self) -> str:
		return f"{self.name.lower()}.h"


def _info_types(command: object, registry: ir.Registry) -> list[str]:
	"""The structures a wrapper names by tag, so it needs their module."""
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


def _modules(registry: ir.Registry, table: Table, selection: Selection) -> list[Module]:
	"""Selection split by owning module, in an order every include can follow.

	A module depends on another when something it declares names something the
	other declares: an enum-typed field, a slot's tag, an alias's target, the tag
	a wrapper takes. A compat edge names two tags, so it goes to whichever of
	its two modules comes later and makes that one depend on the other.
	"""
	modules = {name: Module(name) for name in registry.modules}
	rank = {name: index for index, name in enumerate(registry.modules)}
	unowned: list[str] = []

	def owner(name: str) -> Module | None:
		module = registry.origins.get(name)
		if module is None or module not in modules:
			unowned.append(name)
			return None
		return modules[module]

	def place(name: str, bucket: str) -> Module | None:
		module = owner(name)
		if module is not None:
			getattr(module, bucket).append(name)
		return module

	def needs(module: Module | None, name: str) -> None:
		other = registry.origins.get(name)
		if module is not None and other is not None and other != module.name:
			module.depends.add(other)

	for name in selection.chained:
		place(name, "chained")
	for name in selection.selected:
		module = place(name, "selected")
		for field in selection.plans[name].fields:
			if field.kind == "enum":
				needs(module, field.enum_type)
			elif field.kind == "reference":
				needs(module, field.target)
	for name in selection.plain:
		module = place(name, "plain")
		for field in plan_struct(registry.structs[name], registry).fields:
			if field.kind == "enum":
				needs(module, field.enum_type)
	for name in selection.enums:
		place(name, "enums")
	for name, alias in selection.aliases.items():
		needs(place(name, "aliases"), alias.target)
	for command in selection.commands:
		module = owner(command.name)
		if module is None:
			continue
		module.commands.append(command)
		for name in _info_types(command, registry):
			needs(module, name)

	emitted = selection.emitted
	for parent, child in registry.edges:
		if parent not in emitted or child not in emitted:
			continue
		ends = [registry.origins.get(parent), registry.origins.get(child)]
		if None in ends:
			continue
		later = max(ends, key=lambda name: rank[name])
		modules[later].edges.append((parent, child))
		needs(modules[later], parent)
		needs(modules[later], child)

	if unowned:
		raise GenerationError([], [f"{name}: no <feature> or <extension> requires it" for name in sorted(set(unowned))])

	populated = {
		name: module
		for name, module in modules.items()
		if module.chained or module.plain or module.enums or module.aliases or module.edges or module.commands
	}
	return _ordered(populated, rank)


def _ordered(modules: dict[str, Module], rank: dict[str, int]) -> list[Module]:
	"""Dependencies first; registry order wherever the includes leave a choice."""
	pending = {name: set(module.depends) & set(modules) for name, module in modules.items()}
	order: list[Module] = []
	while pending:
		ready = sorted((name for name, depends in pending.items() if not depends), key=lambda name: rank[name])
		if not ready:
			cycle = ", ".join(sorted(pending, key=lambda name: rank[name]))
			raise GenerationError([], [f"modules depend on each other in a cycle: {cycle}"])
		name = ready[0]
		order.append(modules[name])
		del pending[name]
		for depends in pending.values():
			depends.discard(name)
	return order


# ---------------------------------------------------------------- rendering


def _render_module(module: Module, registry: ir.Registry, table: Table, selection: Selection) -> list[str]:
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
					guard_open(lines, struct.guards)
					lines.append(f"struct {leaf_of(struct_name(name))}{{}};")
					guard_close(lines, struct.guards)
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


def _render_traits(module: Module, registry: ir.Registry, table: Table) -> list[str]:
	lines: list[str] = []
	# A query chain needs a head as much as a create chain does, so the structures
	# a command writes into count as roots too.
	roots = set(registry.roots) | set(registry.query_roots)
	branches = set(registry.branches)
	for name in module.chained:
		struct = registry.structs[name]
		guard_open(lines, struct.guards)
		lines.append("template<>")
		lines.append(f"struct vulkan_object_trait<obj::{table.struct_name(name)}>")
		lines.append("{")
		lines.append(f"{TAB}constexpr static auto root = {'true' if name in roots else 'false'};")
		lines.append(f"{TAB}constexpr static auto branch = {'true' if name in branches else 'false'};")
		lines.append(f"{TAB}constexpr static auto allow_duplicate = {'true' if struct.allow_duplicate else 'false'};")
		lines.append("};")
		guard_close(lines, struct.guards)
		lines.append("")

	for name in module.chained:
		struct = registry.structs[name]
		guard_open(lines, struct.guards)
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
		guard_close(lines, struct.guards)
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
		guard_open(lines, guards)
		lines.append("template<>")
		lines.append(
			f"inline constexpr auto is_vulkan_object_compatible_with_v<obj::{table.struct_name(parent)}, obj::{table.struct_name(child)}> = true;"
		)
		guard_close(lines, guards)
	return lines


def _render_params(
	module: Module, aliases: list[str], registry: ir.Registry, table: Table, selection: Selection
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

# The vkfu core headers are small and each is #pragma once, so every module
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
	'#include "{prefix}branch_pipe.h"',
	'#include "{prefix}chain.h"',
	'#include "{prefix}expression.h"',
	'#include "{prefix}extension.h"',
	'#include "{prefix}query.h"',
	'#include "{prefix}reference.h"',
	'#include "{prefix}storage.h"',
	'#include "{prefix}vulkan_object.h"',
]


def _banner(registry: ir.Registry) -> str:
	return f"// Generated by vkfu_gen from vk.xml (VK_HEADER_VERSION {registry.header_version}). Do not edit."


def _preamble(registry: ir.Registry, prefix: str, summary: str | None = None) -> list[str]:
	lines = ["#pragma once", "", _banner(registry)]
	if summary:
		lines.append(f"// {summary}")
	lines.append("")
	lines += [entry.format(prefix=prefix) for entry in _CORE_INCLUDES]
	lines.append("")
	return lines


def _summary(module: Module, registry: ir.Registry) -> str:
	counts = [
		(len(module.chained), "object"),
		(len(module.plain), "plain structure"),
		(len(module.enums), "enum"),
		(len(module.aliases), "alias"),
		(len(module.commands), "command"),
	]
	parts = [f"{count} {noun}{'' if count == 1 else 's'}" for count, noun in counts if count]
	return f"{module.name}: " + (", ".join(parts) if parts else "compatibility edges only") + "."


def _build(registry: ir.Registry, table: Table, scope: str) -> tuple[list[Module], dict[str, list[str]], list[str]]:
	selection, warnings = _select(registry, table, scope)
	modules = _modules(registry, table, selection)
	bodies = {module.name: _render_module(module, registry, table, selection) for module in modules}
	return modules, bodies, warnings


def generate(
	registry: ir.Registry, table: Table, scope: str, prefix: str = "../"
) -> tuple[str, list[str]]:
	"""One header, every module in include order."""
	modules, bodies, warnings = _build(registry, table, scope)
	lines = _preamble(registry, prefix)
	for module in modules:
		lines.append(f"// ---------------------------------------------------------------- {module.name}")
		lines.append("")
		lines.extend(bodies[module.name])
		lines.append("")
	return NEWLINE.join(lines), warnings


def generate_split(
	registry: ir.Registry, table: Table, scope: str, stem: str, prefix: str = "../../"
) -> tuple[dict[str, str], list[str]]:
	"""One file per module under `<stem>/`, plus `<stem>.h` including them all.

	A translation unit that only touches core 1.0 and VK_KHR_swapchain includes
	those two and parses nothing else; each module includes what it depends on.
	"""
	modules, bodies, warnings = _build(registry, table, scope)
	files: dict[str, str] = {}
	for module in modules:
		lines = _preamble(registry, prefix, _summary(module, registry))
		depends = [other for other in modules if other.name in module.depends]
		if depends:
			lines += [f'#include "{other.file}"' for other in depends]
			lines.append("")
		lines.extend(bodies[module.name])
		files[module.file] = NEWLINE.join(lines) + NEWLINE
	umbrella = [
		"#pragma once",
		"",
		_banner(registry),
		"// Every module. Include one from the directory beside this file instead",
		"// when that is all you need; each brings in the modules it depends on.",
		"",
	]
	umbrella += [f'#include "{stem}/{module.file}"' for module in modules]
	files[f"{stem}.h"] = NEWLINE.join(umbrella) + NEWLINE
	return files, warnings
