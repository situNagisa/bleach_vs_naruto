"""vkfu_gen command line.

	python -m vkfu_gen dump-ir   --xml vk.xml
	python -m vkfu_gen suggest   --xml vk.xml --table naming.toml --out naming.suggested.toml
	python -m vkfu_gen gen       --xml vk.xml --table naming.toml --out ../include/vkfu/generated/vulkan-v1.4.328.h
"""

from __future__ import annotations

import argparse
import collections
import dataclasses
import re
import json
import pathlib
import sys
import tomllib

from . import check, emit, ir, naming, producers


NEWLINE = chr(10)


def _quote(key: str) -> str:
	return f'"{key}"'


def _pipeline_states(registry: ir.Registry) -> frozenset[str]:
	"""Pipeline state objects: the pointer-slot targets plus their pNext children."""
	slots = list(registry.references) + list(registry.elements)
	states = {target for _, _, target in slots if target.startswith("VkPipeline")}
	# A sType-less structure named VkPipeline<something>State is pipeline state
	# too -- VkPipelineColorBlendAttachmentState is the obvious one.
	states |= {
		name for name in registry.plain if re.fullmatch(r"VkPipeline.+State", name)
	}
	frontier = set(states)
	while frontier:
		children = {child for parent, child in registry.edges if parent in frontier and child not in states}
		states |= children
		frontier = children
	return frozenset(states)


def _suggest(registry: ir.Registry, table: naming.Table) -> tuple[str, dict[str, int]]:
	lines = [
		"# Suggested naming entries, produced by `vkfu_gen suggest`.",
		"# This file is review material, never an input to `gen`. Read each entry,",
		"# fix what reads badly, then move it into naming.toml.",
		"#",
		"# REVIEW tags mean the suggester could not be trusted:",
		"#   acronym  - adjacent capitals the lexicon does not know, so the split is a guess",
		"#   lone     - the split produced a single-letter word",
		"#   wordrun  - a long word with no boundary the lexicon can account for",
		"#   keyword  - collides with a C++ keyword (a trailing _ was added)",
		"#   prefix   - the enum constant does not start with its flag type's prefix",
		"#   invalid  - not a usable identifier",
		"",
	]
	stats = {"structs": 0, "members": 0, "bits": 0, "enums": 0, "values": 0, "review": 0}
	seen_bits: set[str] = set()
	author_tags = frozenset(registry.author_tags)
	states = _pipeline_states(registry)
	members = (
		frozenset(target for _, _, target in list(registry.references) + list(registry.elements))
		| frozenset(registry.plain)
	) - states
	objects = naming.suggest_object_names(
		list(registry.closure) + list(registry.plain) + list(registry.query_closure),
		frozenset(registry.roots),
		states,
		members,
		author_tags,
		frozenset(registry.query_closure),
	)

	for name in registry.query_closure:
		if table.struct_name(name) is not None:
			continue
		suggestion = objects[name]
		stats["structs"] += 1
		lines.append(f"[struct.{_quote(name)}]")
		if suggestion.reviews:
			stats["review"] += 1
			lines.append(f"# REVIEW({','.join(suggestion.reviews)})")
		lines.append(f'name = "{suggestion.value}"')
		lines.append("")

	for name in list(registry.closure) + list(registry.plain):
		struct = registry.structs[name]
		plan = emit.plan_struct(struct, registry)
		struct_suggestion = objects[name]
		needs_struct = table.struct_name(name) is None
		# Fields are named relative to the object, so they stop restating it.
		leaf = struct_suggestion.value.rpartition("::")[2]
		proposed = {
			field.member.name: naming.suggest_member(
				field.member.name, author_tags, leaf=leaf, kind=field.kind
			)
			for field in plan.fields
		}
		# `pGeometries` and `ppGeometries` differ only by the Hungarian prefix, so
		# stripping it collapses two different members into one name.
		taken = collections.Counter(suggestion.value for suggestion in proposed.values())
		for member, suggestion in proposed.items():
			if taken[suggestion.value] > 1 and "collision" not in suggestion.reviews:
				suggestion.reviews.append("collision")
		pending = [
			(field.member.name, proposed[field.member.name])
			for field in plan.fields
			if table.member_name(name, field.member.name) is None
		]

		if needs_struct or pending:
			lines.append(f"[struct.{_quote(name)}]")
			if needs_struct:
				stats["structs"] += 1
				if struct_suggestion.reviews:
					stats["review"] += 1
					lines.append(f"# REVIEW({','.join(struct_suggestion.reviews)})")
				lines.append(f'name = "{struct_suggestion.value}"')
			if pending:
				lines.append(f"[struct.{_quote(name)}.members]")
				for member, suggestion in pending:
					stats["members"] += 1
					if suggestion.reviews:
						stats["review"] += 1
						lines.append(f"# REVIEW({','.join(suggestion.reviews)})")
					lines.append(f'{member} = "{suggestion.value}"')
			lines.append("")

		for field in plan.fields:
			if field.kind != "flags" or field.flag_bits in seen_bits:
				continue
			bits = emit.positional_bits(registry.flagbits[field.flag_bits])
			pending_bits = [bit for bit in bits if table.bit_name(field.flag_bits, bit.name) is None]
			if not pending_bits:
				continue
			seen_bits.add(field.flag_bits)
			prefix = naming.bit_prefix(field.flag_bits, [bit.name for bit in bits], author_tags)
			named_bits = naming.suggest_enum_values([bit.name for bit in bits], prefix, author_tags)
			lines.append(f"[bits.{_quote(field.flag_bits)}]")
			for bit in pending_bits:
				suggestion = named_bits[bit.name]
				stats["bits"] += 1
				if suggestion.reviews:
					stats["review"] += 1
					lines.append(f"# REVIEW({','.join(suggestion.reviews)})")
				lines.append(f'{bit.name} = "{suggestion.value}"')
			lines.append("")


	enum_types = emit.used_enums(registry.closure, registry)
	enum_names = naming.suggest_enum_names(enum_types, author_tags)
	for enum_type in enum_types:
		values = registry.enums[enum_type].values
		pending_values = [v for v in values if table.value_name(enum_type, v.name) is None]
		needs_enum = table.enum_name(enum_type) is None
		if not needs_enum and not pending_values:
			continue
		lines.append(f"[enum.{_quote(enum_type)}]")
		if needs_enum:
			suggestion = enum_names[enum_type]
			stats["enums"] += 1
			if suggestion.reviews:
				stats["review"] += 1
				lines.append(f"# REVIEW({','.join(suggestion.reviews)})")
			lines.append(f'name = "{suggestion.value}"')
		if pending_values:
			prefix = naming.bit_prefix(enum_type, [v.name for v in values], author_tags)
			named = naming.suggest_enum_values([v.name for v in values], prefix, author_tags)
			lines.append(f"[enum.{_quote(enum_type)}.values]")
			for value in pending_values:
				suggestion = named[value.name]
				stats["values"] += 1
				if suggestion.reviews:
					stats["review"] += 1
					lines.append(f"# REVIEW({','.join(suggestion.reviews)})")
				lines.append(f'{value.name} = "{suggestion.value}"')
		lines.append("")

	return "\n".join(lines) + "\n", stats


TABLE_HEADER = """\
# vkfu naming table -- the sole authority for every name in generated code.
#
# Keys are verbatim vk.xml identifiers. Values are what you want to type. There
# is no algorithm behind this file and therefore no exceptions to one: an
# identifier that is absent is simply not generated, and `gen --scope closure`
# fails on it.
#
# Entries arrive here by review: `vkfu_gen suggest` writes candidates with
# REVIEW tags, a human reads them, and `vkfu_gen promote` folds the reviewed
# file in. Editing a name afterwards is normal -- nothing derives these.
"""


def _promote(table_path: str, suggested_path: str) -> tuple[str, dict[str, int]]:
	"""Fold a reviewed suggestion file into the table, keeping existing entries."""
	existing = naming.load_table(table_path)
	reviewed = naming.load_table(suggested_path)

	structs = dict(existing.structs)
	members = {name: dict(fields) for name, fields in existing.members.items()}
	bits = {name: dict(entries) for name, entries in existing.bits.items()}
	enums = dict(existing.enums)
	values = {name: dict(entries) for name, entries in existing.values.items()}
	stats = {"structs": 0, "members": 0, "bits": 0, "enums": 0, "values": 0}

	for name, value in reviewed.structs.items():
		if name not in structs:
			structs[name] = value
			stats["structs"] += 1
	for name, fields in reviewed.members.items():
		target = members.setdefault(name, {})
		for member, value in fields.items():
			if member not in target:
				target[member] = value
				stats["members"] += 1
	for name, entries in reviewed.bits.items():
		target = bits.setdefault(name, {})
		for bit, value in entries.items():
			if bit not in target:
				target[bit] = value
				stats["bits"] += 1

	for name, value in reviewed.enums.items():
		if name not in enums:
			enums[name] = value
			stats["enums"] += 1
	for name, entries in reviewed.values.items():
		target = values.setdefault(name, {})
		for value_name, value in entries.items():
			if value_name not in target:
				target[value_name] = value
				stats["values"] += 1

	lines = [TABLE_HEADER]
	for name in sorted(structs):
		lines.append(f"[struct.{_quote(name)}]")
		lines.append(f'name = "{structs[name]}"')
		fields = members.get(name) or {}
		if fields:
			lines.append(f"[struct.{_quote(name)}.members]")
			lines.extend(f'{member} = "{value}"' for member, value in fields.items())
		lines.append("")
	for name in sorted(bits):
		lines.append(f"[bits.{_quote(name)}]")
		lines.extend(f'{bit} = "{value}"' for bit, value in bits[name].items())
		lines.append("")
	for name in sorted(enums):
		lines.append(f"[enum.{_quote(name)}]")
		lines.append(f'name = "{enums[name]}"')
		entries = values.get(name) or {}
		if entries:
			lines.append(f"[enum.{_quote(name)}.values]")
			lines.extend(f'{value_name} = "{value}"' for value_name, value in entries.items())
		lines.append("")

	return "\n".join(lines), stats


def _rebuild(registry: ir.Registry, overrides_path: str) -> tuple[str, dict[str, int]]:
	"""naming.toml = what the rules propose, with the human decisions applied.

	Keeping the judgement calls in one small file means a change to the bucketing
	rules is one command, not a five-thousand-line hand edit.
	"""
	try:
		with open(overrides_path, "rb") as stream:
			overrides = tomllib.load(stream)
	except FileNotFoundError:
		overrides = {}
	struct_leaves = overrides.get("struct") or {}
	member_names = overrides.get("member") or {}
	bit_names = overrides.get("bit") or {}
	value_names = overrides.get("value") or {}
	command_names = overrides.get("command") or {}

	author_tags = frozenset(registry.author_tags)
	states = _pipeline_states(registry)
	members = (
		frozenset(target for _, _, target in list(registry.references) + list(registry.elements))
		| frozenset(registry.plain)
	) - states
	named_objects = list(registry.closure) + list(registry.plain)
	# Query objects need a name so a chain can refer to them, but no field names:
	# a vkGet* fills them in, so there is no param to declare.
	all_objects = named_objects + list(registry.query_closure)
	proposed = naming.suggest_object_names(
		all_objects,
		frozenset(registry.roots),
		states,
		members,
		author_tags,
		frozenset(registry.query_closure),
	)

	objects: dict[str, str] = {}
	for name in all_objects:
		namespace, _, leaf = proposed[name].value.rpartition("::")
		leaf = struct_leaves.get(name, leaf)
		objects[name] = f"{namespace}::{leaf}" if namespace else leaf

	stats = {"structs": len(objects), "members": 0, "bits": 0, "enums": 0, "values": 0, "commands": 0}
	lines = [TABLE_HEADER]
	for name in sorted(registry.query_closure):
		lines.append(f"[struct.{_quote(name)}]")
		lines.append(f'name = "{objects[name]}"')
		lines.append("")
	for name in sorted(named_objects):
		lines.append(f"[struct.{_quote(name)}]")
		lines.append(f'name = "{objects[name]}"')
		fields = emit.plan_struct(registry.structs[name], registry).fields
		if fields:
			lines.append(f"[struct.{_quote(name)}.members]")
			leaf = objects[name].rpartition("::")[2]
			for field in fields:
				override = member_names.get(f"{name}.{field.member.name}")
				value = override or naming.suggest_member(
					field.member.name, author_tags, leaf=leaf, kind=field.kind
				).value
				lines.append(f'{field.member.name} = "{value}"')
				stats["members"] += 1
		lines.append("")

	used_bits = sorted({
		field.flag_bits
		for name in named_objects
		for field in emit.plan_struct(registry.structs[name], registry).fields
		if field.kind == "flags"
	})
	for flag_bits in used_bits:
		layout = emit._bit_layout(registry.flagbits[flag_bits]) or []
		prefix = naming.bit_prefix(flag_bits, [bit.name for bit in layout], author_tags)
		lines.append(f"[bits.{_quote(flag_bits)}]")
		named_bits = naming.suggest_enum_values([bit.name for bit in layout], prefix, author_tags)
		for bit in layout:
			override = bit_names.get(f"{flag_bits}.{bit.name}")
			value = override or named_bits[bit.name].value
			lines.append(f'{bit.name} = "{value}"')
			stats["bits"] += 1
		lines.append("")

	enum_types = emit.used_enums(registry.closure, registry)
	enum_labels = naming.suggest_enum_names(enum_types, author_tags)
	for enum_type in enum_types:
		lines.append(f"[enum.{_quote(enum_type)}]")
		lines.append(f'name = "{enum_labels[enum_type].value}"')
		stats["enums"] += 1
		values = registry.enums[enum_type].values
		prefix = naming.bit_prefix(enum_type, [value.name for value in values], author_tags)
		named = naming.suggest_enum_values([value.name for value in values], prefix, author_tags)
		lines.append(f"[enum.{_quote(enum_type)}.values]")
		for value in values:
			override = value_names.get(f"{enum_type}.{value.name}")
			lines.append(f'{value.name} = "{override or named[value.name].value}"')
			stats["values"] += 1
		lines.append("")

	for command in list(registry.producers) + list(registry.wrappers) + list(registry.queries) + list(registry.chain_queries):
		proposed_name, proposed_singular = naming.suggest_command_name(command.name, author_tags)
		chosen = command_names.get(command.name, proposed_name)
		lines.append(f"[command.{_quote(command.name)}]")
		lines.append(f'name = "{chosen}"')
		if isinstance(command, ir.Command) and producers.shape_of(command) == "array":
			key = f"{command.name}.singular"
			lines.append(f'singular = "{command_names.get(key, proposed_singular)}"')
		if isinstance(command, ir.Query):
			# The count twin. Same words, different verb.
			key = f"{command.name}.count"
			lines.append(f'count = "{command_names.get(key, naming.suggest_count_name(chosen))}"')
		lines.append("")
		stats["commands"] = stats.get("commands", 0) + 1

	# Aliases last, because their names are derived from the objects above and a
	# collision with a real object means the alias is the one that has to move.
	alias_names = overrides.get("alias") or {}
	claimed = set(objects.values())
	for alias in registry.aliases:
		target = objects.get(alias.target)
		if target is None:
			continue
		proposed = naming.suggest_alias_name(alias.name, target, author_tags)
		chosen = alias_names.get(alias.name, proposed)
		if not chosen:
			continue
		if chosen in claimed:
			stats["alias_collisions"] = stats.get("alias_collisions", 0) + 1
			continue
		claimed.add(chosen)
		lines.append(f"[alias.{_quote(alias.name)}]")
		lines.append(f'name = "{chosen}"')
		lines.append("")
		stats["aliases"] = stats.get("aliases", 0) + 1

	unused = (
		{key for key in struct_leaves if key not in registry.structs}
		| {key.rsplit(".", 1)[0] for key in member_names} - set(registry.closure)
	)
	if unused:
		stats["stale"] = len(unused)
	return NEWLINE.join(lines), stats


def _report(
	problems: list[check.Problem], label: str, limit: int = 40, verbose: bool = False
) -> int:
	"""Print the mechanical findings. Errors are fatal, warnings are not.

	Errors are always listed, because every one of them has to be dealt with.
	Warnings are legal code a human already looked at once, so they are counted
	rather than repeated on every run.
	"""
	errors, warnings = check.summarise(problems)
	shown = [p for p in problems if verbose or p.severity == "error"]
	for problem in shown[:limit]:
		print(f"  {problem}", file=sys.stderr)
	if len(shown) > limit:
		print(f"  ... and {len(shown) - limit} more", file=sys.stderr)
	if warnings and not verbose:
		print(f"  ({warnings} warning(s) suppressed; pass --verbose to see them)", file=sys.stderr)
	print(f"{label}: {errors} error(s), {warnings} warning(s)", file=sys.stderr if errors else sys.stdout)
	return 1 if errors else 0


def main(argv: list[str] | None = None) -> int:
	parser = argparse.ArgumentParser(prog="vkfu_gen")
	parser.add_argument("command", choices=["dump-ir", "suggest", "promote", "rebuild", "check", "gen"])
	parser.add_argument("--xml", default="vk.xml")
	parser.add_argument("--table", default="naming.toml")
	parser.add_argument("--overrides", default="naming.overrides.toml")
	parser.add_argument("--out")
	parser.add_argument(
		"--verbose",
		action="store_true",
		help="list warnings as well as errors",
	)
	parser.add_argument(
		"--include-prefix",
		default=None,
		help="how the generated headers reach the vkfu core headers; defaults to the in-tree layout",
	)
	parser.add_argument(
		"--split",
		action="store_true",
		help="write one header per section plus an umbrella, instead of a single file",
	)
	parser.add_argument(
		"--core",
		type=int,
		default=None,
		help="highest Vulkan 1.x minor version to generate for; omit for every version vk.xml declares",
	)
	parser.add_argument(
		"--extensions",
		default=None,
		help="comma-separated extension names to include; omit for all of them, pass an empty string for none",
	)
	parser.add_argument(
		"--scope",
		choices=["table", "closure"],
		default="table",
		help="table: emit exactly what naming.toml names. closure: require a name for every object reachable from a vkCreate* root.",
	)
	arguments = parser.parse_args(argv)

	profile = ir.Profile(
		core=arguments.core,
		extensions=(
			None
			if arguments.extensions is None
			else frozenset(filter(None, arguments.extensions.split(",")))
		),
	)
	registry = ir.load(arguments.xml, profile=profile)

	if arguments.command == "dump-ir":
		payload = json.dumps(dataclasses.asdict(registry), indent="\t", sort_keys=True)
		if arguments.out:
			with open(arguments.out, "w", encoding="utf-8", newline="\n") as stream:
				stream.write(payload + "\n")
		else:
			print(payload)
		print(
			f"vk.xml {registry.header_version}: {len(registry.roots)} roots, "
			f"{len(registry.closure)} objects, {len(registry.branches)} branches, {len(registry.edges)} edges",
			file=sys.stderr,
		)
		return 0

	if arguments.command == "rebuild":
		text, stats = _rebuild(registry, arguments.overrides)
		with open(arguments.table, "w", encoding="utf-8", newline=NEWLINE) as stream:
			stream.write(text)
		print(
			f"{arguments.table}: rebuilt {stats['structs']} objects, {stats['members']} fields, "
			f"{stats['bits']} flag bits, {stats['enums']} enums, {stats['values']} enumerators, "
			f"{stats['commands']} commands, {stats.get('aliases', 0)} aliases"
			+ (f"; {stats['alias_collisions']} alias(es) skipped for colliding" if stats.get("alias_collisions") else "")
			+ (f"; {stats['stale']} stale override(s)" if stats.get("stale") else "")
		)
		# A rebuild that produced an unusable table should say so now, not when
		# somebody eventually tries to compile the header.
		return _report(
			check.check(registry, naming.load_table(arguments.table), arguments.scope),
			arguments.table,
			verbose=arguments.verbose,
		)

	if arguments.command == "check":
		return _report(
			check.check(registry, naming.load_table(arguments.table), arguments.scope),
			arguments.table,
			limit=200,
			verbose=arguments.verbose,
		)

	if arguments.command == "promote":
		text, stats = _promote(arguments.table, arguments.out or "naming.suggested.toml")
		with open(arguments.table, "w", encoding="utf-8", newline="\n") as stream:
			stream.write(text)
		print(
			f"{arguments.table}: added {stats['structs']} object names, "
			f"{stats['members']} fields, {stats['bits']} flag bits, "
			f"{stats['enums']} enums, {stats['values']} enumerators"
		)
		return 0

	table = naming.load_table(arguments.table)

	if arguments.command == "suggest":
		text, stats = _suggest(registry, table)
		destination = arguments.out or "naming.suggested.toml"
		with open(destination, "w", encoding="utf-8", newline="\n") as stream:
			stream.write(text)
		print(
			f"{destination}: {stats['structs']} object names, {stats['members']} fields, "
			f"{stats['bits']} flag bits, {stats['enums']} enums, {stats['values']} enumerators "
			f"still unnamed; {stats['review']} entries tagged REVIEW"
		)
		# Check what a promote would produce, so a bad suggestion is caught here
		# rather than after it has been folded in.
		merged = naming.load_table(arguments.table)
		proposed = naming.load_table(destination)
		merged.structs.update(proposed.structs)
		for owner, fields in proposed.members.items():
			merged.members.setdefault(owner, {}).update(fields)
		for owner, entries in proposed.bits.items():
			merged.bits.setdefault(owner, {}).update(entries)
		merged.enums.update(proposed.enums)
		for owner, entries in proposed.values.items():
			merged.values.setdefault(owner, {}).update(entries)
		return _report(check.check(registry, merged, "table"), destination, verbose=arguments.verbose)

	try:
		stem = pathlib.Path(arguments.out or "generated").stem
		if arguments.split:
			prefix = arguments.include_prefix or "../../"
			files, warnings = emit.generate_split(registry, table, arguments.scope, stem, prefix)
			text = None
		else:
			text, warnings = emit.generate(
				registry, table, arguments.scope, arguments.include_prefix or "../"
			)
	except emit.GenerationError as error:
		print(f"generation failed: {len(error.missing)} missing name(s), {len(error.problems)} problem(s)", file=sys.stderr)
		for entry in error.missing[:40]:
			print(f"  missing: {entry}", file=sys.stderr)
		if len(error.missing) > 40:
			print(f"  ... and {len(error.missing) - 40} more", file=sys.stderr)
		for entry in error.problems[:40]:
			print(f"  problem: {entry}", file=sys.stderr)
		print("run `vkfu_gen suggest` to produce review material for the missing names", file=sys.stderr)
		return 1

	for warning in warnings:
		print(f"warning: {warning}", file=sys.stderr)

	named = sum(1 for name in registry.closure if table.struct_name(name))
	unnamed = len(registry.closure) - named
	destination = arguments.out or "-"
	if text is None:
		# The umbrella keeps the single-file path working; the parts live in a
		# directory beside it, named after it.
		root = pathlib.Path(destination).parent
		(root / stem).mkdir(parents=True, exist_ok=True)
		for name, body in sorted(files.items()):
			target = root / name if name.endswith(f"{stem}.h") else root / stem / name
			with open(target, "w", encoding="utf-8", newline=NEWLINE) as stream:
				stream.write(body)
		print(
			f"{destination}: {len(files)} file(s), "
			+ ", ".join(f"{name} {body.count(NEWLINE)} lines" for name, body in sorted(files.items()))
		)
		return 0
	if destination == "-":
		print(text)
	else:
		with open(destination, "w", encoding="utf-8", newline=NEWLINE) as stream:
			stream.write(text)
	print(f"{destination}: {named} object(s) generated, {unnamed} of {len(registry.closure)} still unnamed")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
