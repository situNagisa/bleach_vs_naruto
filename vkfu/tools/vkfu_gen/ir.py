"""Stage 1: vk.xml -> IR.

This stage is purely mechanical. Every key in the IR is a verbatim Vulkan
identifier; no name is invented, shortened, or re-cased here. Anything that
would require human judgement belongs in the naming table, not in this file.
"""

from __future__ import annotations

import dataclasses
import re
import xml.etree.ElementTree as ET


# ---------------------------------------------------------------- model


@dataclasses.dataclass
class Member:
	name: str  # verbatim Vulkan identifier
	type: str  # verbatim Vulkan type name
	ptr_depth: int
	elem_const: bool
	ptr_const: bool  # `T const* const*`, i.e. the inner pointer is const
	array_dims: list[str]
	length: str | None  # `len` attribute, verbatim
	altlen: str | None
	optional: bool
	values: str | None  # sType's fixed enum value


@dataclasses.dataclass
class Struct:
	name: str
	members: list[Member]
	structextends: list[str]
	allow_duplicate: bool
	returnedonly: bool
	stype: str | None
	guards: list[str]  # empty == always available


@dataclasses.dataclass
class Bit:
	name: str  # VK_..._BIT
	guards: list[str]
	bitpos: int | None = None  # None for value-defined entries
	value: str | None = None  # zero sentinels and compound aliases


@dataclasses.dataclass
class FlagBits:
	name: str  # VkXxxFlagBits
	bits: list[Bit]
	width: int  # 32 or 64


@dataclasses.dataclass
class EnumValue:
	name: str  # VK_FORMAT_R8G8B8A8_SRGB
	guards: list[str]


@dataclasses.dataclass
class EnumType:
	name: str  # VkFormat
	values: list[EnumValue]
	guards: list[str]  # the type itself can be platform-gated


@dataclasses.dataclass
class Command:
	name: str  # vkCreateBuffer
	returns: str  # VkResult, or void
	leading: list[Member]  # parameters before the info
	info: Member  # the structure the caller fills in
	info_is_array: bool  # pCreateInfos rather than pCreateInfo
	count: Member | None  # the createInfoCount that goes with an array
	allocator: bool  # takes a pAllocator
	out: Member  # the handle(s) the command produces
	out_from_info: bool  # the out array's length lives inside the info
	guards: list[str]


@dataclasses.dataclass
class Registry:
	header_version: str
	author_tags: list[str]  # vk.xml <tags>, e.g. KHR / EXT / HUAWEI
	structs: dict[str, Struct]
	bitmask_to_flagbits: dict[str, str]  # VkXxxFlags -> VkXxxFlagBits
	flagbits: dict[str, FlagBits]
	enums: dict[str, EnumType]  # plain (non-bitmask) enums
	roots: list[str]
	root_commands: dict[str, list[str]]  # root struct -> commands that take it
	producers: list[Command]  # vkCreate* / vkAllocate* in a shape vkfu can wrap
	closure: list[str]
	branches: list[str]
	references: list[tuple[str, str, str]]  # (owner, member, target) pointer slots
	elements: list[tuple[str, str, str]]  # (owner, member, target) array element types
	edges: list[tuple[str, str]]  # (parent, child) from structextends


# ---------------------------------------------------------------- parsing


def _api_allows(element: ET.Element, api: str) -> bool:
	value = element.get("api")
	return value is None or api in value.split(",")


def _parse_decl(element: ET.Element) -> Member:
	"""Read one <member>/<param> declaration without normalising anything."""
	before = element.text or ""
	between = ""
	after = ""
	seen_name = False
	for child in element:
		if child.tag == "type":
			between = child.tail or ""
			continue
		if child.tag == "name":
			seen_name = True
			after += child.tail or ""
			continue
		if child.tag == "comment":
			continue
		if seen_name:
			after += (child.text or "") + (child.tail or "")

	return Member(
		name=element.findtext("name") or "",
		type=element.findtext("type") or "",
		ptr_depth=between.count("*"),
		elem_const="const" in before,
		ptr_const="const" in between,
		array_dims=re.findall(r"\[([^\]]*)\]", after),
		length=element.get("len"),
		altlen=element.get("altlen"),
		optional=element.get("optional") == "true",
		values=element.get("values"),
	)


def _platform_guards(root: ET.Element) -> dict[str, str]:
	return {p.get("name"): p.get("protect") for p in root.iter("platform") if p.get("protect")}


def _extension_guard(extension: ET.Element, platforms: dict[str, str]) -> str | None:
	explicit = extension.get("protect")
	if explicit:
		return explicit
	platform = extension.get("platform")
	if platform:
		return platforms.get(platform)
	return None


def _collect_requirements(root: ET.Element, api: str) -> dict[str, list[str | None]]:
	"""type name -> list of guards it is reachable through (None == unguarded)."""
	platforms = _platform_guards(root)
	requirements: dict[str, list[str | None]] = {}

	def record(name: str | None, guard: str | None) -> None:
		if name:
			requirements.setdefault(name, []).append(guard)

	for feature in root.iter("feature"):
		if not _api_allows(feature, api):
			continue
		for require in feature.findall("require"):
			if not _api_allows(require, api):
				continue
			for type_element in require.findall("type"):
				record(type_element.get("name"), None)

	for extension in root.iter("extension"):
		supported = extension.get("supported")
		if supported and api not in supported.split(","):
			continue
		guard = _extension_guard(extension, platforms)
		for require in extension.findall("require"):
			if not _api_allows(require, api):
				continue
			for type_element in require.findall("type"):
				record(type_element.get("name"), guard)

	return requirements


def _guards_for(requirements: dict[str, list[str | None]], name: str) -> list[str]:
	sources = requirements.get(name)
	if not sources:
		# Not reachable from any feature/extension for this api. Treat as guarded
		# by nothing but flag it by returning an empty list; callers filter these
		# out via the closure instead.
		return []
	if any(source is None for source in sources):
		return []
	return sorted({source for source in sources if source})


def _bit_position(enum: ET.Element) -> dict[str, object]:
	bitpos = enum.get("bitpos")
	return {
		"bitpos": int(bitpos) if bitpos is not None else None,
		"value": enum.get("value") if bitpos is None else None,
	}


def _collect_flagbits(root: ET.Element, api: str, requirements: dict[str, list[str | None]]) -> dict[str, FlagBits]:
	collected: dict[str, FlagBits] = {}

	for enums in root.iter("enums"):
		if enums.get("type") != "bitmask":
			continue
		name = enums.get("name")
		if not name:
			continue
		width = 64 if enums.get("bitwidth") == "64" else 32
		bits: list[Bit] = []
		for enum in enums.findall("enum"):
			if not _api_allows(enum, api) or enum.get("alias"):
				continue
			bit_name = enum.get("name")
			if bit_name:
				bits.append(Bit(name=bit_name, guards=[], **_bit_position(enum)))
		collected[name] = FlagBits(name=name, bits=bits, width=width)

	# Bits contributed by features/extensions live outside the <enums> block.
	platforms = _platform_guards(root)

	def absorb(container: ET.Element, guard: str | None) -> None:
		for require in container.findall("require"):
			if not _api_allows(require, api):
				continue
			for enum in require.findall("enum"):
				extends = enum.get("extends")
				bit_name = enum.get("name")
				if not extends or not bit_name or enum.get("alias"):
					continue
				if not _api_allows(enum, api):
					continue
				target = collected.get(extends)
				if target is None:
					continue
				if any(bit.name == bit_name for bit in target.bits):
					continue
				target.bits.append(Bit(name=bit_name, guards=[guard] if guard else [], **_bit_position(enum)))

	for feature in root.iter("feature"):
		if _api_allows(feature, api):
			absorb(feature, None)
	for extension in root.iter("extension"):
		supported = extension.get("supported")
		if supported and api not in supported.split(","):
			continue
		absorb(extension, _extension_guard(extension, platforms))

	# A bit is only usable if its own enum constant is reachable unguarded.
	for flag_bits in collected.values():
		for bit in flag_bits.bits:
			if not bit.guards:
				bit.guards = _guards_for(requirements, bit.name)

	return collected


def _collect_enums(root: ET.Element, api: str, requirements: dict[str, list[str | None]]) -> dict[str, EnumType]:
	"""Plain enums, i.e. everything <enums> declares that is not a bitmask."""
	collected: dict[str, EnumType] = {}
	for enums in root.iter("enums"):
		if enums.get("type") != "enum" or not enums.get("name"):
			continue
		values = [
			EnumValue(name=enum.get("name"), guards=[])
			for enum in enums.findall("enum")
			if enum.get("name") and not enum.get("alias") and _api_allows(enum, api)
		]
		collected[enums.get("name")] = EnumType(
			name=enums.get("name"),
			values=values,
			guards=_guards_for(requirements, enums.get("name")),
		)

	platforms = _platform_guards(root)

	def absorb(container: ET.Element, guard: str | None) -> None:
		for require in container.findall("require"):
			if not _api_allows(require, api):
				continue
			for enum in require.findall("enum"):
				extends = enum.get("extends")
				name = enum.get("name")
				if not extends or not name or enum.get("alias") or not _api_allows(enum, api):
					continue
				target = collected.get(extends)
				if target is None or any(value.name == name for value in target.values):
					continue
				target.values.append(EnumValue(name=name, guards=[guard] if guard else []))

	for feature in root.iter("feature"):
		if _api_allows(feature, api):
			absorb(feature, None)
	for extension in root.iter("extension"):
		supported = extension.get("supported")
		if supported and api not in supported.split(","):
			continue
		absorb(extension, _extension_guard(extension, platforms))

	for enum_type in collected.values():
		for value in enum_type.values:
			if not value.guards:
				value.guards = _guards_for(requirements, value.name)

	return collected


def _flag_bits_as_enums(flagbits: dict[str, FlagBits], requirements: dict[str, list[str | None]]) -> dict[str, EnumType]:
	"""A FlagBits type used directly as a member is a single choice, not a mask.

	VkPipelineMultisampleStateCreateInfo::rasterizationSamples is declared
	VkSampleCountFlagBits, meaning one sample count -- so it reads as an enum.
	"""
	return {
		name: EnumType(
			name=name,
			values=[EnumValue(name=bit.name, guards=bit.guards) for bit in spec.bits],
			guards=_guards_for(requirements, name),
		)
		for name, spec in flagbits.items()
	}


def is_reference(member: Member, target: Struct | None) -> bool:
	"""A single const pointer naming another sType-carrying structure.

	`len` means it is an array, which stays a span; no sType means it is a plain
	value structure like VkPhysicalDeviceFeatures, which stays a pointer.
	"""
	return (
		target is not None
		and target.stype is not None
		and member.ptr_depth == 1
		and member.elem_const
		and not member.array_dims
		and (member.length is None or member.length == "1")
	)


def _producers(
	root: ET.Element,
	structs: dict[str, Struct],
	api: str,
	requirements: dict[str, list[str | None]],
) -> tuple[list[Command], list[str]]:
	"""vkCreate* / vkAllocate* commands, split into the shapes vkfu can wrap.

	Every one of them has the same skeleton: some handles the caller already has,
	one structure the caller fills in, an optional allocator, and the handle the
	command produces. Anything that does not fit is reported rather than guessed.
	"""
	commands: list[Command] = []
	skipped: list[str] = []
	for element in root.iter("command"):
		if not _api_allows(element, api):
			continue
		proto = element.find("proto")
		if proto is None:
			continue
		name = proto.findtext("name") or ""
		if not (name.startswith("vkCreate") or name.startswith("vkAllocate")):
			continue

		params = [_parse_decl(p) for p in element.findall("param") if _api_allows(p, api)]
		infos = [
			p
			for p in params
			if p.ptr_depth == 1 and p.elem_const and (structs.get(p.type) or Struct("", [], [], False, False, None, [])).stype
		]
		outs = [p for p in params if p.ptr_depth == 1 and not p.elem_const]
		if len(infos) != 1 or len(outs) != 1:
			skipped.append(name)
			continue

		info, out = infos[0], outs[0]
		index = params.index(info)
		leading = [
			p for p in params[:index] if p.type != "VkAllocationCallbacks" and p.name != info.length
		]
		count = next((p for p in params if info.length and p.name == info.length), None)
		commands.append(
			Command(
				name=name,
				returns=proto.findtext("type") or "void",
				leading=leading,
				info=info,
				info_is_array=info.length is not None,
				count=count,
				allocator=any(p.type == "VkAllocationCallbacks" for p in params),
				out=out,
				out_from_info=bool(out.length and "->" in out.length),
				guards=sorted(
					set(_guards_for(requirements, name))
					| set(structs[info.type].guards if info.type in structs else [])
				),
			)
		)
	return commands, skipped


def is_element(member: Member, target: Struct | None) -> bool:
	"""An array of another sType-carrying structure.

	It stays a span of native values -- a span borrows, so the parent cannot own
	the elements -- but the element type still deserves a param of its own.
	"""
	return (
		target is not None
		and target.stype is not None
		and member.ptr_depth == 1
		and member.elem_const
		and not member.array_dims
		and member.length is not None
		and member.length != "1"
	)


def _info_roots(root: ET.Element, structs: dict[str, Struct], api: str) -> tuple[list[str], dict[str, list[str]]]:
	"""Roots are the structures a caller builds and hands to a command.

	The rule is judgement-free: a const pointer parameter whose struct type has
	an sType member. That excludes pAllocator (VkAllocationCallbacks has no
	sType) without naming it, and it does not care which command family the
	parameter belongs to -- vkCreate*, vkAllocate*, vkCmd* and vkQueue* all take
	structures the caller fills in the same way.
	"""
	roots: list[str] = []
	commands: dict[str, list[str]] = {}
	for command in root.iter("command"):
		if not _api_allows(command, api):
			continue
		proto = command.find("proto")
		if proto is None:
			continue
		name = proto.findtext("name") or ""
		for param in command.findall("param"):
			if not _api_allows(param, api):
				continue
			declaration = _parse_decl(param)
			if declaration.ptr_depth != 1 or not declaration.elem_const:
				continue
			struct = structs.get(declaration.type)
			if struct is None or struct.stype is None:
				continue
			if struct.name not in roots:
				roots.append(struct.name)
			commands.setdefault(struct.name, []).append(name)
	return roots, commands


def load(path: str, api: str = "vulkan") -> Registry:
	root = ET.parse(path).getroot()
	requirements = _collect_requirements(root, api)

	header_version = "unknown"
	for type_element in root.iter("type"):
		if type_element.get("category") == "define" and type_element.findtext("name") == "VK_HEADER_VERSION":
			tail = (type_element.find("name").tail or "").strip()
			header_version = tail
			break

	structs: dict[str, Struct] = {}
	bitmask_to_flagbits: dict[str, str] = {}

	for type_element in root.iter("type"):
		category = type_element.get("category")
		if category == "bitmask":
			if type_element.get("alias") or not _api_allows(type_element, api):
				continue
			flags_name = type_element.findtext("name")
			bits_name = type_element.get("requires") or type_element.get("bitvalues")
			if flags_name and bits_name:
				bitmask_to_flagbits[flags_name] = bits_name
			continue
		if category != "struct":
			continue
		if type_element.get("alias") or not _api_allows(type_element, api):
			continue
		name = type_element.get("name")
		if not name:
			continue
		members = [
			_parse_decl(member)
			for member in type_element.findall("member")
			if _api_allows(member, api)
		]
		stype = next((member.values for member in members if member.name == "sType"), None)
		extends = type_element.get("structextends")
		structs[name] = Struct(
			name=name,
			members=members,
			structextends=extends.split(",") if extends else [],
			allow_duplicate=type_element.get("allowduplicate") == "true",
			returnedonly=type_element.get("returnedonly") == "true",
			stype=stype,
			guards=_guards_for(requirements, name),
		)

	# Keep only structs that some feature/extension actually pulls in.
	structs = {name: struct for name, struct in structs.items() if name in requirements}

	flagbits = _collect_flagbits(root, api, requirements)
	enums = _collect_enums(root, api, requirements)
	# A FlagBits type can appear as a member in its own right; give it an enum
	# form too. Only the ones actually used as members get generated.
	enums.update(
		(name, spec)
		for name, spec in _flag_bits_as_enums(flagbits, requirements).items()
		if name not in enums
	)
	roots, root_commands = _info_roots(root, structs, api)
	producers, _unwrappable = _producers(root, structs, api, requirements)

	# Closure over two relations at once: pNext (structextends) and the pointer
	# members that name another sType-carrying structure.
	closure = list(roots)
	in_closure = set(closure)
	changed = True
	while changed:
		changed = False
		for name, struct in structs.items():
			if name in in_closure:
				continue
			if any(parent in in_closure for parent in struct.structextends):
				closure.append(name)
				in_closure.add(name)
				changed = True
		for name in list(closure):
			for member in structs[name].members:
				target = structs.get(member.type)
				if not (is_reference(member, target) or is_element(member, target)):
					continue
				if member.type in in_closure:
					continue
				closure.append(member.type)
				in_closure.add(member.type)
				changed = True

	elements = sorted(
		(name, member.name, member.type)
		for name in closure
		for member in structs[name].members
		if is_element(member, structs.get(member.type))
	)

	references = sorted(
		(name, member.name, member.type)
		for name in closure
		for member in structs[name].members
		if is_reference(member, structs.get(member.type))
	)

	edges: list[tuple[str, str]] = []
	for name in closure:
		for parent in structs[name].structextends:
			if parent in in_closure:
				edges.append((parent, name))

	branches = sorted({parent for parent, _ in edges})

	return Registry(
		header_version=header_version,
		author_tags=sorted(tag.get("name") for tag in root.iter("tag") if tag.get("name")),
		structs=structs,
		bitmask_to_flagbits=bitmask_to_flagbits,
		flagbits=flagbits,
		enums=enums,
		roots=sorted(roots),
		root_commands={name: sorted(set(cmds)) for name, cmds in sorted(root_commands.items())},
		producers=sorted(producers, key=lambda command: command.name),
		closure=sorted(closure),
		branches=branches,
		references=references,
		elements=elements,
		edges=sorted(edges),
	)
