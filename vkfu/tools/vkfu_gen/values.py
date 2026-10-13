"""The value types every renderer shares: one struct per flags type, one enum
class per enum.

A field and a parameter of the same Vulkan type get the same C++ type, so a
value read from one goes into the other with no cast. This module is the one
place that says what that type is and how it converts back to the native one.
"""

from __future__ import annotations

from . import ir
from .naming import Table


class Values:
	"""Which flags and enum types have a vkfu spelling in this header."""

	def __init__(self, registry: ir.Registry, table: Table, flags: list[str], enums: list[str]) -> None:
		self.registry = registry
		self.table = table
		self.flags = frozenset(flags)
		self.enums = frozenset(enums)

	def kind(self, member: ir.Member) -> str | None:
		"""flags | enum for a by-value member of a type vkfu wraps, else None."""
		if member.ptr_depth or member.array_dims:
			return None
		declared = ir.resolve_type(self.registry, member.type)
		if declared in self.flags:
			return "flags"
		if declared in self.enums:
			return "enum"
		return None

	def spelled(self, member: ir.Member) -> str | None:
		"""The vkfu type a by-value parameter takes instead of the native one."""
		kind = self.kind(member)
		declared = ir.resolve_type(self.registry, member.type)
		if kind == "flags":
			return f"::vkfu::flags::{self.table.flags_name(declared)}"
		if kind == "enum":
			return f"::vkfu::enums::{self.table.enum_name(declared)}"
		return None

	def native(self, member: ir.Member, expression: str) -> str:
		"""`expression`, converted back to what the Vulkan command takes."""
		kind = self.kind(member)
		if kind == "flags":
			return f"::std::bit_cast<{member.type}>({expression})"
		if kind == "enum":
			return f"static_cast<{member.type}>({expression})"
		return expression

	def types(self, members: list[ir.Member]) -> set[str]:
		"""The wrapped types these parameters name, so their file can be included."""
		return {
			ir.resolve_type(self.registry, member.type) for member in members if self.kind(member) is not None
		}
