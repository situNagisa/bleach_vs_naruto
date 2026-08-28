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
class Argument:
	kind: str  # passthrough | info | span | out | out_span | allocator
	member: Member
	count: Member | None = None  # the length parameter a span consumes


@dataclasses.dataclass
class Wrapper:
	"""A command vkfu can wrap: expressions where Vulkan takes a structure, spans
	where it takes a pointer and a count."""

	name: str
	returns: str
	arguments: list[Argument]  # what the wrapper takes, counts folded away
	params: list[Member]  # what the command takes, in its own order
	spans: dict[str, str]  # count parameter name -> the span parameter that drives it
	guards: list[str]


@dataclasses.dataclass
class Provenance:
	"""What a caller has to enable before a type exists.

	vk.xml puts a promoted type's core name in <feature> and its old name in
	<extension>, so the two are read together: `core` is the earliest core
	version that has it, `extensions` the extensions that provide it (its own or
	its aliases'). Both empty is impossible; both set means either will do.
	"""

	extensions: list[str]
	core: str | None  # VK_VERSION_1_2, or None for extension-only types
	instance: bool  # the extensions are instance extensions, not device ones


@dataclasses.dataclass
class Query:
	"""A two-call enumerate: the command writes the count as well as the array.

	Distinct from Wrapper because the count is an out parameter, so the caller
	cannot supply a span -- the wrapper has to size a container itself.
	"""

	name: str
	returns: str  # VkResult (can answer VK_INCOMPLETE) or void
	leading: list[Member]  # the handles the caller already has
	params: list[Member]  # the command's own parameters, in order
	count: Member
	out: Member
	guards: list[str]


@dataclasses.dataclass
class ChainQuery:
	"""A command that fills in one sType structure, i.e. a query chain head.

	vkGetPhysicalDeviceProperties2 and friends. The caller says which extra
	structures to hang off pNext; the wrapper owns them, links them, stamps every
	sType and hands back the lot.
	"""

	name: str
	returns: str
	arguments: list[Argument]  # what the caller supplies: passthrough or info
	params: list[Member]  # the command's own parameters, in order
	out: Member
	guards: list[str]


@dataclasses.dataclass
class Alias:
	"""An old spelling of a type core later promoted.

	Same structure, same sType, different name -- vk.xml records the extension
	against this name rather than against the promoted one. Code written before
	the promotion says `...KHR`, so the generated header offers both.
	"""

	name: str  # VkPhysicalDeviceFeatures2KHR
	target: str  # VkPhysicalDeviceFeatures2
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
	wrappers: list[Wrapper]  # every other command worth wrapping
	queries: list[Query]  # two-call enumerates
	chain_queries: list[ChainQuery]  # commands that fill in one sType structure
	provenance: dict[str, Provenance]  # type name -> what enables it
	aliases: list[Alias]  # old spellings whose target this header generates
	query_roots: list[str]  # sType structures a command writes into
	query_closure: list[str]  # those plus their pNext children, minus `closure`
	closure: list[str]
	plain: list[str]  # sType-less member structures: a param, but not a chain node
	branches: list[str]
	references: list[tuple[str, str, str]]  # (owner, member, target) pointer slots
	elements: list[tuple[str, str, str]]  # (owner, member, target) array element types
	edges: list[tuple[str, str]]  # (parent, child) from structextends


# ---------------------------------------------------------------- parsing


def _api_allows(element: ET.Element, api: str) -> bool:
	value = element.get("api")
	return value is None or api in value.split(",")


@dataclasses.dataclass(frozen=True)
class Profile:
	"""Which of vk.xml's features and extensions to generate for.

	Everything downstream is reachability from these, so narrowing the profile
	narrows the closure, the enums and the commands together -- there is no
	separate list to keep in step.
	"""

	core: int | None = None  # highest VK_VERSION_1_x minor to include, None = all
	extensions: frozenset[str] | None = None  # None = all supported ones

	def allows_feature(self, name: str) -> bool:
		if self.core is None:
			return True
		match = re.fullmatch(r"VK_VERSION_1_(\d+)", name or "")
		return match is not None and int(match.group(1)) <= self.core

	def allows_extension(self, name: str) -> bool:
		return self.extensions is None or name in self.extensions


EVERYTHING = Profile()


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


def _collect_requirements(root: ET.Element, api: str, profile: Profile = EVERYTHING) -> dict[str, list[str | None]]:
	"""type name -> list of guards it is reachable through (None == unguarded)."""
	platforms = _platform_guards(root)
	requirements: dict[str, list[str | None]] = {}

	def record(name: str | None, guard: str | None) -> None:
		if name:
			requirements.setdefault(name, []).append(guard)

	for feature in root.iter("feature"):
		if not _api_allows(feature, api) or not profile.allows_feature(feature.get("name")):
			continue
		for require in feature.findall("require"):
			if not _api_allows(require, api):
				continue
			for type_element in require.findall("type"):
				record(type_element.get("name"), None)
				for command_element in require.findall("command"):
					record(command_element.get("name"), None)

	for extension in root.iter("extension"):
		supported = extension.get("supported")
		if supported and api not in supported.split(","):
			continue
		if not profile.allows_extension(extension.get("name")):
			continue
		guard = _extension_guard(extension, platforms)
		for require in extension.findall("require"):
			if not _api_allows(require, api):
				continue
			for type_element in require.findall("type"):
				record(type_element.get("name"), guard)
				for command_element in require.findall("command"):
					record(command_element.get("name"), guard)

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


def _provenance(root: ET.Element, api: str, profile: Profile = EVERYTHING) -> dict[str, Provenance]:
	"""type name -> the core version and/or extensions that provide it.

	An alias counts as the type itself: VkAttachmentDescription2 is core 1.2 and
	vk.xml only mentions VK_KHR_create_renderpass2 against the KHR-suffixed
	alias, so without following aliases every promoted type would look as if it
	needed no extension at all.
	"""
	canonical: dict[str, str] = {}
	for element in root.iter("type"):
		name, target = element.get("name"), element.get("alias")
		if name and target:
			canonical[name] = target

	def resolve(name: str) -> str:
		seen: set[str] = set()
		while name in canonical and name not in seen:
			seen.add(name)
			name = canonical[name]
		return name

	cores: dict[str, set[str]] = {}
	extensions: dict[str, set[str]] = {}
	instance: dict[str, set[bool]] = {}

	for feature in root.iter("feature"):
		if not _api_allows(feature, api) or not profile.allows_feature(feature.get("name")):
			continue
		version = feature.get("name")
		for require in feature.findall("require"):
			if not _api_allows(require, api):
				continue
			for element in require.findall("type"):
				if element.get("name") and version:
					cores.setdefault(resolve(element.get("name")), set()).add(version)

	for extension in root.iter("extension"):
		supported = extension.get("supported")
		if supported and api not in supported.split(","):
			continue
		label = extension.get("name")
		if not profile.allows_extension(label):
			continue
		kind = extension.get("type") == "instance"
		for require in extension.findall("require"):
			if not _api_allows(require, api):
				continue
			for element in require.findall("type"):
				if not element.get("name") or not label:
					continue
				target = resolve(element.get("name"))
				extensions.setdefault(target, set()).add(label)
				instance.setdefault(target, set()).add(kind)

	names = set(cores) | set(extensions)
	return {
		name: Provenance(
			extensions=sorted(extensions.get(name, ())),
			core=min(cores[name]) if cores.get(name) else None,
			instance=instance.get(name) == {True},
		)
		for name in sorted(names)
	}


def _queries(
	root: ET.Element,
	api: str,
	requirements: dict[str, list[str | None]],
) -> tuple[list[Query], list[str]]:
	"""The two-call enumerate pattern, which no span can express.

	Identified by shape, not by name: an out array whose length parameter is
	itself an out pointer. That is the whole family -- vkEnumerate*, most of the
	plural vkGet*, and vkGetPhysicalDeviceQueueFamilyProperties, which returns
	void rather than VkResult.
	"""
	queries: list[Query] = []
	skipped: list[str] = []
	for element in root.iter("command"):
		if not _api_allows(element, api):
			continue
		proto = element.find("proto")
		if proto is None:
			continue
		name = proto.findtext("name") or ""
		if name not in requirements:
			continue

		params = [_parse_decl(p) for p in element.findall("param") if _api_allows(p, api)]
		by_name = {p.name: p for p in params}
		outs = [p for p in params if p.ptr_depth >= 1 and not p.elem_const]
		pairs = [
			(by_name[out.length], out)
			for out in outs
			if out.length and by_name.get(out.length) is not None and by_name[out.length].ptr_depth >= 1
		]
		if not pairs:
			continue
		if len(pairs) != 1 or len(outs) != 2:
			# Several arrays sharing one count (vkGetPipelineCacheData's paired
			# forms) need a decision about who owns the count; not guessed here.
			skipped.append(name)
			continue

		count, out = pairs[0]
		if out.ptr_depth != 1 or count.ptr_depth != 1:
			skipped.append(name)
			continue
		queries.append(
			Query(
				name=name,
				returns=proto.findtext("type") or "void",
				leading=[p for p in params if p is not count and p is not out],
				params=params,
				count=count,
				out=out,
				guards=_guards_for(requirements, name),
			)
		)
	return queries, skipped


def _chain_queries(
	root: ET.Element,
	structs: dict[str, Struct],
	api: str,
	requirements: dict[str, list[str | None]],
	branches: set[str],
) -> list[ChainQuery]:
	"""Commands whose single out parameter is an sType structure.

	That sType is the tell: the structure heads a pNext chain, so the caller is
	meant to hang further structures off it. Everything else about the command --
	the handles it needs, an info structure it reads -- is the same shape the
	other wrappers already handle.
	"""
	found: list[ChainQuery] = []
	for element in root.iter("command"):
		if not _api_allows(element, api):
			continue
		proto = element.find("proto")
		if proto is None:
			continue
		name = proto.findtext("name") or ""
		if name not in requirements:
			continue
		# vkCreate*/vkAllocate* belong to the producers, even when what they hand
		# back happens to be a structure rather than a handle.
		if name.startswith("vkCreate") or name.startswith("vkAllocate"):
			continue

		params = [_parse_decl(p) for p in element.findall("param") if _api_allows(p, api)]
		outs = [p for p in params if p.ptr_depth >= 1 and not p.elem_const]
		if len(outs) != 1:
			continue
		out = outs[0]
		target = structs.get(out.type)
		if out.ptr_depth != 1 or out.length or out.array_dims or target is None or target.stype is None:
			continue
		# Only worth a chain if something can actually hang off the head. When
		# nothing extends it, returning the structure by value says the same thing
		# with a plainer type, which is what the ordinary wrapper already does.
		if out.type not in branches:
			continue

		arguments: list[Argument] = []
		for param in params:
			if param is out:
				continue
			info = structs.get(param.type)
			if param.ptr_depth == 1 and param.elem_const and info is not None and info.stype is not None:
				arguments.append(Argument(kind="info", member=param))
			else:
				arguments.append(Argument(kind="passthrough", member=param))
		found.append(
			ChainQuery(
				name=name,
				returns=proto.findtext("type") or "void",
				arguments=arguments,
				params=params,
				out=out,
				guards=sorted(
					set(_guards_for(requirements, name))
					| {
						guard
						for argument in arguments
						if argument.kind == "info"
						for guard in structs[argument.member.type].guards
					}
					| set(target.guards)
				),
			)
		)
	return found


def _aliases(
	root: ET.Element,
	structs: dict[str, Struct],
	api: str,
	requirements: dict[str, list[str | None]],
) -> list[Alias]:
	"""Struct aliases whose target this api declares, and which it declares too."""
	found: list[Alias] = []
	for element in root.iter("type"):
		if element.get("category") != "struct" or not _api_allows(element, api):
			continue
		name, target = element.get("name"), element.get("alias")
		if not name or not target or name not in requirements:
			continue
		# Chains of aliases are legal; only the structure at the end is real.
		seen: set[str] = set()
		while target not in structs and target in seen.union({target}) and target not in seen:
			seen.add(target)
			nested = next(
				(x.get("alias") for x in root.iter("type") if x.get("name") == target and x.get("alias")),
				None,
			)
			if nested is None:
				break
			target = nested
		if target not in structs:
			continue
		found.append(Alias(name=name, target=target, guards=_guards_for(requirements, name)))
	return sorted(found, key=lambda alias: alias.name)


def _query_roots(root: ET.Element, structs: dict[str, Struct], api: str) -> list[str]:
	"""Structures a command writes into: the mirror image of _info_roots.

	Same judgement-free rule with the const dropped. These carry an sType, so
	they form pNext chains exactly like create-infos do -- but the caller reads
	them instead of filling them in.
	"""
	found: list[str] = []
	for command in root.iter("command"):
		if not _api_allows(command, api):
			continue
		for param in command.findall("param"):
			if not _api_allows(param, api):
				continue
			declaration = _parse_decl(param)
			if declaration.ptr_depth != 1 or declaration.elem_const:
				continue
			struct = structs.get(declaration.type)
			if struct is None or struct.stype is None or struct.name in found:
				continue
			found.append(struct.name)
	return found


def _bit_position(enum: ET.Element) -> dict[str, object]:
	bitpos = enum.get("bitpos")
	return {
		"bitpos": int(bitpos) if bitpos is not None else None,
		"value": enum.get("value") if bitpos is None else None,
	}


def _collect_flagbits(root: ET.Element, api: str, requirements: dict[str, list[str | None]], profile: Profile = EVERYTHING) -> dict[str, FlagBits]:
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
		if _api_allows(feature, api) and profile.allows_feature(feature.get("name")):
			absorb(feature, None)
	for extension in root.iter("extension"):
		supported = extension.get("supported")
		if supported and api not in supported.split(","):
			continue
		if not profile.allows_extension(extension.get("name")):
			continue
		absorb(extension, _extension_guard(extension, platforms))

	# A bit is only usable if its own enum constant is reachable unguarded.
	for flag_bits in collected.values():
		for bit in flag_bits.bits:
			if not bit.guards:
				bit.guards = _guards_for(requirements, bit.name)

	return collected


def _collect_enums(root: ET.Element, api: str, requirements: dict[str, list[str | None]], profile: Profile = EVERYTHING) -> dict[str, EnumType]:
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
		if _api_allows(feature, api) and profile.allows_feature(feature.get("name")):
			absorb(feature, None)
	for extension in root.iter("extension"):
		supported = extension.get("supported")
		if supported and api not in supported.split(","):
			continue
		if not profile.allows_extension(extension.get("name")):
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


def _wrappers(
	root: ET.Element,
	structs: dict[str, Struct],
	api: str,
	requirements: dict[str, list[str | None]],
	chain_queried: set[str],
) -> tuple[list[Wrapper], list[str]]:
	"""Commands other than vkCreate*/vkAllocate* that a wrapper would improve.

	Worth wrapping when the command takes a structure the caller fills in, or a
	pointer paired with its own count. The two-call enumerate pattern is left
	alone: its count is an out parameter, which is a different shape entirely.
	"""
	wrappers: list[Wrapper] = []
	skipped: list[str] = []
	for element in root.iter("command"):
		if not _api_allows(element, api):
			continue
		proto = element.find("proto")
		if proto is None:
			continue
		name = proto.findtext("name") or ""
		if name.startswith("vkCreate") or name.startswith("vkAllocate"):
			continue
		if name in chain_queried:
			continue  # the query chain form supersedes a plain out parameter

		params = [_parse_decl(p) for p in element.findall("param") if _api_allows(p, api)]
		by_name = {p.name: p for p in params}

		def length_of(param: Member) -> Member | None:
			if not param.length:
				return None
			token = param.length.split(",")[0]
			target = by_name.get(token)
			return target if target is not None and target.ptr_depth == 0 else None

		# `void* pData` is a buffer the caller supplies, not something produced.
		outs = [
			p
			for p in params
			if p.ptr_depth >= 1 and not p.elem_const and not (p.type == "void" and p.ptr_depth == 1)
		]
		# Enumerate pattern: the out array's count is itself written by the call.
		if any(
			out.length and by_name.get(out.length) is not None and by_name[out.length].ptr_depth >= 1
			for out in outs
		):
			continue
		if len(outs) > 1:
			skipped.append(name)
			continue
		# Only wrap what this api actually declares; a platform command we cannot
		# see would not compile.
		if name not in requirements:
			continue

		consumed = {
			length_of(p).name
			for p in params
			if p.ptr_depth >= 1 and p.type != "void" and length_of(p) is not None
		}

		arguments: list[Argument] = []
		interesting = False
		for param in params:
			if param.name in consumed:
				continue
			if param.type == "VkAllocationCallbacks":
				arguments.append(Argument(kind="allocator", member=param))
				continue
			if param in outs:
				count = length_of(param)
				arguments.append(Argument(kind="out_span" if param.length else "out", member=param, count=count))
				continue
			count = length_of(param)
			if param.ptr_depth >= 1 and param.elem_const and count is not None and param.type != "void":
				arguments.append(Argument(kind="span", member=param, count=count))
				interesting = True
				continue
			target = structs.get(param.type)
			if param.ptr_depth == 1 and param.elem_const and target is not None and target.stype is not None:
				arguments.append(Argument(kind="info", member=param))
				interesting = True
				continue
			arguments.append(Argument(kind="passthrough", member=param))

		if not interesting:
			continue
		wrappers.append(
			Wrapper(
				name=name,
				returns=proto.findtext("type") or "void",
				arguments=arguments,
				params=params,
				spans={
					argument.count.name: argument.member.name
					for argument in arguments
					if argument.count is not None
				},
				guards=sorted(
					set(_guards_for(requirements, name))
					| {
						guard
						for argument in arguments
						if argument.kind == "info"
						for guard in structs[argument.member.type].guards
					}
				),
			)
		)
	return wrappers, skipped


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


def load(path: str, api: str = "vulkan", profile: Profile = EVERYTHING) -> Registry:
	root = ET.parse(path).getroot()
	requirements = _collect_requirements(root, api, profile)

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

	flagbits = _collect_flagbits(root, api, requirements, profile)
	enums = _collect_enums(root, api, requirements, profile)
	# A FlagBits type can appear as a member in its own right; give it an enum
	# form too. Only the ones actually used as members get generated.
	enums.update(
		(name, spec)
		for name, spec in _flag_bits_as_enums(flagbits, requirements).items()
		if name not in enums
	)
	roots, root_commands = _info_roots(root, structs, api)
	producers, _unwrappable = _producers(root, structs, api, requirements)
	queries, _unwrappable_queries = _queries(root, api, requirements)
	# Anything named as a structextends target can carry a pNext chain, which is
	# what makes a query chain worth having. Independent of any closure, so it is
	# available before the wrapper families are split.
	extended = {parent for struct in structs.values() for parent in struct.structextends}
	chain_queries = _chain_queries(root, structs, api, requirements, extended)
	wrappers, _unwrappable_commands = _wrappers(
		root, structs, api, requirements, {query.name for query in chain_queries}
	)

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

	# Structures a closure member names that carry no sType. They have no pNext to
	# manage, so they get a param and nothing else -- but the param is still worth
	# having, for snake_case fields, scoped enums and bit-field flags.
	plain: set[str] = set()
	frontier = set(closure)
	while frontier:
		discovered: set[str] = set()
		for name in frontier:
			for member in structs[name].members:
				target = structs.get(member.type)
				if target is None or target.stype is not None:
					continue
				if member.type in in_closure or member.type in plain:
					continue
				plain.add(member.type)
				discovered.add(member.type)
		frontier = discovered

	references = sorted(
		(name, member.name, member.type)
		for name in closure
		for member in structs[name].members
		if is_reference(member, structs.get(member.type))
	)

	# Query chains: the read-side mirror of `closure`. A caller links these to
	# say which extra properties a vkGet* should fill in, so they need a tag and
	# the pNext hooks -- but not a param, because nobody fills the fields in.
	query_roots = [name for name in _query_roots(root, structs, api) if name not in in_closure]
	query_closure = set(query_roots)
	changed = True
	while changed:
		changed = False
		for name, struct in structs.items():
			if name in query_closure or name in in_closure:
				continue
			if any(parent in query_closure for parent in struct.structextends):
				query_closure.add(name)
				changed = True

	chained = in_closure | query_closure
	edges: list[tuple[str, str]] = []
	for name in sorted(chained):
		for parent in structs[name].structextends:
			if parent in chained:
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
		wrappers=sorted(wrappers, key=lambda command: command.name),
		queries=sorted(queries, key=lambda query: query.name),
		chain_queries=sorted(chain_queries, key=lambda query: query.name),
		provenance=_provenance(root, api, profile),
		aliases=[
			alias
			for alias in _aliases(root, structs, api, requirements)
			if alias.target in in_closure or alias.target in plain or alias.target in query_closure
		],
		query_roots=sorted(query_roots),
		query_closure=sorted(query_closure),
		closure=sorted(closure),
		plain=sorted(plain),
		branches=branches,
		references=references,
		elements=elements,
		edges=sorted(edges),
	)
