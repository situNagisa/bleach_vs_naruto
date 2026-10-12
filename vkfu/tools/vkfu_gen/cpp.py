"""C++ spelling shared by every renderer.

Nothing here knows Vulkan semantics; it only knows how C++ is written.
"""

from __future__ import annotations

from typing import Callable, Iterable

from . import ir

TAB = "\t"
NEWLINE = chr(10)

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


def base_type(name: str) -> str:
	return PRIMITIVES.get(name, name)


def pointer_type(member: ir.Member) -> str:
	text = base_type(member.type)
	if member.elem_const:
		text += " const"
	text += "*"
	if member.ptr_depth >= 2:
		if member.ptr_const:
			text += " const"
		text += "*"
	return text


def span_element_type(member: ir.Member) -> str:
	text = base_type(member.type)
	if member.ptr_depth == 1:
		return text + (" const" if member.elem_const else "")
	text += " const" if member.elem_const else ""
	text += "*"
	text += " const" if member.ptr_const else ""
	return text


def guard_open(lines: list[str], guards: list[str]) -> None:
	if guards:
		# Reachable through any one of them, so OR rather than AND.
		lines.append("#if " + " || ".join(f"defined({guard})" for guard in guards))


def guard_close(lines: list[str], guards: list[str]) -> None:
	if guards:
		lines.append("#endif")


def namespace_of(qualified: str) -> str:
	return qualified.rpartition("::")[0]


def leaf_of(qualified: str) -> str:
	return qualified.rpartition("::")[2]


def bucketed(keys: Iterable[str], qualified: Callable[[str], str]) -> list[tuple[str, list[str]]]:
	"""Keys grouped by the namespace of their qualified name, the unnamespaced
	group first. Keys keep their given order inside a group."""
	buckets: dict[str, list[str]] = {}
	for key in keys:
		buckets.setdefault(namespace_of(qualified(key)), []).append(key)
	return sorted(buckets.items(), key=lambda entry: (entry[0] != "", entry[0]))
