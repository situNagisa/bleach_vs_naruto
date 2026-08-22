"""vkfu_gen command line.

	python -m vkfu_gen dump-ir   --xml vk.xml
	python -m vkfu_gen suggest   --xml vk.xml --table naming.toml --out naming.suggested.toml
	python -m vkfu_gen gen       --xml vk.xml --table naming.toml --out ../include/vkfu/generated/vulkan-v1.4.328.h
"""

from __future__ import annotations

import argparse
import dataclasses
import json
import sys

from . import emit, ir, naming


def _quote(key: str) -> str:
	return f'"{key}"'


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
	stats = {"structs": 0, "members": 0, "bits": 0, "review": 0}
	seen_bits: set[str] = set()
	author_tags = frozenset(registry.author_tags)
	objects = naming.suggest_object_names(registry.closure, frozenset(registry.roots), author_tags)

	for name in registry.closure:
		struct = registry.structs[name]
		plan = emit.plan_struct(struct, registry)
		struct_suggestion = objects[name]
		needs_struct = table.struct_name(name) is None
		# Fields are named relative to the object, so they stop restating it.
		leaf = struct_suggestion.value.rpartition("::")[2]
		pending: list[tuple[str, naming.Suggestion]] = []
		for field in plan.fields:
			if table.member_name(name, field.member.name) is None:
				pending.append(
					(
						field.member.name,
						naming.suggest_member(field.member.name, author_tags, leaf=leaf, kind=field.kind),
					)
				)

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
			lines.append(f"[bits.{_quote(field.flag_bits)}]")
			for bit in pending_bits:
				suggestion = naming.suggest_bit(field.flag_bits, bit.name, prefix)
				stats["bits"] += 1
				if suggestion.reviews:
					stats["review"] += 1
					lines.append(f"# REVIEW({','.join(suggestion.reviews)})")
				lines.append(f'{bit.name} = "{suggestion.value}"')
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
	stats = {"structs": 0, "members": 0, "bits": 0}

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

	return "\n".join(lines), stats


def main(argv: list[str] | None = None) -> int:
	parser = argparse.ArgumentParser(prog="vkfu_gen")
	parser.add_argument("command", choices=["dump-ir", "suggest", "promote", "gen"])
	parser.add_argument("--xml", default="vk.xml")
	parser.add_argument("--table", default="naming.toml")
	parser.add_argument("--out")
	parser.add_argument(
		"--scope",
		choices=["table", "closure"],
		default="table",
		help="table: emit exactly what naming.toml names. closure: require a name for every object reachable from a vkCreate* root.",
	)
	arguments = parser.parse_args(argv)

	registry = ir.load(arguments.xml)

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

	if arguments.command == "promote":
		text, stats = _promote(arguments.table, arguments.out or "naming.suggested.toml")
		with open(arguments.table, "w", encoding="utf-8", newline="\n") as stream:
			stream.write(text)
		print(
			f"{arguments.table}: added {stats['structs']} object names, "
			f"{stats['members']} fields, {stats['bits']} flag bits"
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
			f"{stats['bits']} flag bits still unnamed; {stats['review']} entries tagged REVIEW"
		)
		return 0

	try:
		text, warnings = emit.generate(registry, table, arguments.scope)
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
	if destination == "-":
		print(text)
	else:
		with open(destination, "w", encoding="utf-8", newline="\n") as stream:
			stream.write(text)
	print(f"{destination}: {named} object(s) generated, {unnamed} of {len(registry.closure)} still unnamed")
	return 0


if __name__ == "__main__":
	raise SystemExit(main())
