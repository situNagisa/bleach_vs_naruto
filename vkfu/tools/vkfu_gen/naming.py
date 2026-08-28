"""The naming table, plus a suggester that only ever writes review material.

The table is the sole authority for every name that appears in generated code.
There is deliberately no fallback: if an identifier is missing from the table,
generation fails. `suggest` exists to make filling the table cheap, and its
output is meant to be read by a human before it is moved into the table -- it
is never consulted by the generator.
"""

from __future__ import annotations

import dataclasses
import re
import tomllib


CPP_KEYWORDS = frozenset(
	"""
	alignas alignof and and_eq asm auto bitand bitor bool break case catch char char8_t char16_t
	char32_t class compl concept const consteval constexpr constinit const_cast continue co_await
	co_return co_yield decltype default delete do double dynamic_cast else enum explicit export
	extern false float for friend goto if inline int long mutable namespace new noexcept not not_eq
	nullptr operator or or_eq private protected public register reinterpret_cast requires return
	short signed sizeof static static_assert static_cast struct switch template this thread_local
	throw true try typedef typeid typename union unsigned using virtual void volatile wchar_t while
	xor xor_eq
	""".split()
)

_ATOM = re.compile(r"[A-Z]+(?=[A-Z][a-z])|[A-Z]?[a-z]+|[A-Z]+|[0-9]+")
_HUNGARIAN = re.compile(r"^(pfn|pp|p)(?=[A-Z])")

# Vulkan's own casing is not self-consistent, so the suggester needs a
# dictionary. Every entry below was added because a real v1.4.328 identifier
# needed it -- see the table in tools/README.md for what each one fixes.
#
# A word is matched case-insensitively against a run of consecutive atoms
# (joining what camelCase split apart) or against a single atom (splitting a run
# that carries no boundary at all). Matches are exact, so `3` + `Depth` never
# collapses into `3d`.
LEXICON = frozenset(
	{
		# joins: camelCase splits these, but they read as one word
		"1d",
		"2d",
		"3d",
		"macos",  # MacOSSurfaceCreateInfoMVK -> Mac + OS
		"ycbcr",  # WithoutYCbCrSampler -> Y + Cb + Cr
		"rgba10x6",  # RGBA10X6Formats -> RGBA + 10 + X + 6
		"bfloat16",  # shaderBFloat16Type -> B + Float + 16
		"a4b4g4r4",  # formatA4B4G4R4 -> A + 4 + B + 4 + ...
		"a4r4g4b4",
		"directfb",  # VK_EXT_directfb_surface; FB is also a real vendor tag, so
		"imagepipe",  # VK_FUCHSIA_imagepipe_surface
		# splits: the identifier carries no boundary where a word ends
		"astc",  # TextureCompressionASTCHDR -> ASTCHDR
		"hdr",
		"cluster",  # clustercullingShader
		"culling",
		# domain vocabulary: real acronyms, so the `acronym` tag stays meaningful
		"av1",
		"fps",
		"id",
		"io",
		"ios",
		"pps",
		"rdma",
		"sm",
		"sps",
		"vp9",
		"vps",
		# long single words, listed so the `wordrun` review tag stays meaningful
		"acceleration",
		"availability",
		"conservative",
		"decompression",
		"dependencies",
		"displacement",
		"interpolation",
		"multisampled",
		"overallocation",
		"overestimation",
		"quantization",
		"requirements",
		"rasterization",
		"reconstruction",
		"reconvergence",
		"reinterpretation",
		"representable",
		"representation",
		"representative",
		"simultaneous",
		"constant",  # pushconstantPipelineLayout -> pushconstant
		"description",
		"descriptions",
		"intersection",
		"maintenance",
		"premultiplied",
		"presentation",
		"push",
		"specialization",
		"synchronization",
		"tessellation",
		"unnormalized",
	}
)

_MAX_MERGE = 9  # a4b4g4r4 spans nine atoms


def _decompose(text: str) -> list[str] | None:
	"""Cover `text` entirely with lexicon words, longest first, or give up."""
	if not text:
		return []
	for length in range(len(text), 0, -1):
		head = text[:length]
		if head in LEXICON:
			tail = _decompose(text[length:])
			if tail is not None:
				return [head] + tail
	return None


# ---------------------------------------------------------------- table


@dataclasses.dataclass
class Table:
	structs: dict[str, str]  # VkXxx -> obj/param name
	members: dict[str, dict[str, str]]  # VkXxx -> {memberName: field name}
	bits: dict[str, dict[str, str]]  # VkXxxFlagBits -> {VK_..._BIT: field name}
	commands: dict[str, str]  # vkCreateBuffer -> qualified function name
	singulars: dict[str, str]  # vkCreateGraphicsPipelines -> create_graphics_pipeline
	counts: dict[str, str]  # vkEnumeratePhysicalDevices -> count_physical_devices
	aliases: dict[str, str]  # VkPhysicalDeviceFeatures2KHR -> feature::khr::core
	enums: dict[str, str]  # VkFormat -> enum class name
	values: dict[str, dict[str, str]]  # VkFormat -> {VK_FORMAT_...: enumerator}

	def struct_name(self, struct: str) -> str | None:
		return self.structs.get(struct)

	def member_name(self, struct: str, member: str) -> str | None:
		return self.members.get(struct, {}).get(member)

	def bit_name(self, flag_bits: str, bit: str) -> str | None:
		return self.bits.get(flag_bits, {}).get(bit)

	def command_name(self, command: str) -> str | None:
		return self.commands.get(command)

	def command_singular(self, command: str) -> str | None:
		return self.singulars.get(command)

	def command_count(self, command: str) -> str | None:
		return self.counts.get(command)

	def alias_name(self, struct: str) -> str | None:
		return self.aliases.get(struct)

	def enum_name(self, enum_type: str) -> str | None:
		return self.enums.get(enum_type)

	def value_name(self, enum_type: str, value: str) -> str | None:
		return self.values.get(enum_type, {}).get(value)


def load_table(path: str) -> Table:
	try:
		with open(path, "rb") as stream:
			document = tomllib.load(stream)
	except FileNotFoundError:
		document = {}

	structs: dict[str, str] = {}
	members: dict[str, dict[str, str]] = {}
	for struct, entry in (document.get("struct") or {}).items():
		name = entry.get("name")
		if name:
			structs[struct] = name
		member_map = entry.get("members") or {}
		if member_map:
			members[struct] = dict(member_map)

	bits = {flag_bits: dict(entry) for flag_bits, entry in (document.get("bits") or {}).items()}

	enums: dict[str, str] = {}
	values: dict[str, dict[str, str]] = {}
	for enum_type, entry in (document.get("enum") or {}).items():
		name = entry.get("name")
		if name:
			enums[enum_type] = name
		value_map = entry.get("values") or {}
		if value_map:
			values[enum_type] = dict(value_map)

	commands: dict[str, str] = {}
	singulars: dict[str, str] = {}
	counts: dict[str, str] = {}
	for command, entry in (document.get("command") or {}).items():
		if entry.get("name"):
			commands[command] = entry["name"]
		if entry.get("singular"):
			singulars[command] = entry["singular"]
		if entry.get("count"):
			counts[command] = entry["count"]

	aliases: dict[str, str] = {}
	for struct, entry in (document.get("alias") or {}).items():
		if entry.get("name"):
			aliases[struct] = entry["name"]

	return Table(
		structs=structs,
		members=members,
		bits=bits,
		commands=commands,
		singulars=singulars,
		counts=counts,
		aliases=aliases,
		enums=enums,
		values=values,
	)


# ---------------------------------------------------------------- suggester


@dataclasses.dataclass
class Suggestion:
	value: str
	reviews: list[str]  # non-empty means "a human must look at this"


def _split(identifier: str) -> tuple[list[str], list[str]]:
	"""camelCase identifier -> (lower-case words, atoms the lexicon did not touch).

	Atoms first (digits stand alone), then the lexicon joins or splits, then a
	leftover bare digit run rejoins the word before it so `Features2` stays
	`features2` while `Of3D` becomes `of` + `3d`.
	"""
	# A lowercase `s` right after an all-caps run is a plural marker, not a word:
	# pStdSPSs is "the SPS entries", not "sp" + "ss".
	identifier = re.sub(r"([A-Z]{2,})s(?![a-z])", r"\g<1>", identifier)
	atoms = _ATOM.findall(identifier)
	words: list[str] = []
	untouched: list[str] = []
	index = 0
	while index < len(atoms):
		joined = None
		for end in range(min(len(atoms), index + _MAX_MERGE), index + 1, -1):
			candidate = "".join(atoms[index:end]).lower()
			if candidate in LEXICON:
				joined = (candidate, end)
				break
		if joined:
			words.append(joined[0])
			index = joined[1]
			continue

		parts = _decompose(atoms[index].lower())
		if parts:
			words.extend(parts)
		else:
			words.append(atoms[index].lower())
			untouched.append(atoms[index])
		index += 1

	merged: list[str] = []
	for word in words:
		if word.isdigit() and merged:
			merged[-1] += word
		else:
			merged.append(word)
	return merged, untouched


def _tokens(identifier: str) -> list[str]:
	return _split(identifier)[0]


def _reviews(identifier: str, value: str, author_tags: frozenset[str] = frozenset()) -> list[str]:
	"""Why a human should not trust this suggestion.

	Judged against the identifier's atoms and the resulting words, so anything
	the lexicon already resolved stops being flagged.
	"""
	reviews: list[str] = []
	words, untouched = _split(identifier)

	# An all-caps atom means the word split was a guess -- unless it is a vendor
	# tag (vk.xml declares those in <tags>) or the lexicon consumed it.
	if any(len(atom) >= 2 and atom.isupper() and atom not in author_tags for atom in untouched):
		reviews.append("acronym")
	# A lone single-letter word is the signature of a bad split: `Dimension2D`
	# becoming `dimension2_d`.
	if any(len(word) == 1 and word.isalpha() for word in words[1:]):
		reviews.append("lone")
	# A long word with no boundary in it, that the lexicon cannot account for.
	# A trailing version digit (`maintenance4`) is not part of the word.
	if any(len(word) >= 12 and word.rstrip("0123456789") not in LEXICON for word in words):
		reviews.append("wordrun")
	# A word appearing twice usually means a prefix that the namespace or the
	# parent already carries: VkDeviceDeviceMemoryReportCreateInfoEXT.
	if len(set(words)) != len(words):
		reviews.append("repeat")
	if value in CPP_KEYWORDS:
		reviews.append("keyword")
	if not value or value[0].isdigit():
		reviews.append("invalid")
	return reviews


def _escape_keyword(value: str) -> str:
	return value + "_" if value in CPP_KEYWORDS else value


# The shared prefix of a family goes into a namespace instead of into every
# name. Roots -- the create-infos you actually pass to vkCreate* -- stay at the
# top of `param`; everything else is either a device feature or an extra node
# hung off a create call.
FEATURE_NAMESPACE = "feature"
OPTION_NAMESPACE = "option"
STATE_NAMESPACE = "state"
# The read side. A structure a command writes into is never mixed in with the
# ones you fill in, so it gets its own namespace rather than a suffix.
PROPERTY_NAMESPACE = "property"
RESULT_NAMESPACE = "result"

_FEATURE_FAMILY = re.compile(r"PhysicalDevice(.*?)Features(2?)")
_PROPERTY_FAMILY = re.compile(r"PhysicalDevice(.*?)Properties(2?)")
# Two tiers: normally `CreateInfo` / `AllocateInfo` / `Info` all go, but when
# that makes two objects collide, only `Info` goes -- the verb is what tells
# VkAccelerationStructureCreateInfoNV from VkAccelerationStructureInfoNV.
_INFO_SUFFIX = re.compile(r"(?:Create|Allocate)?Info([0-9]*)$")
_INFO_SUFFIX_KEEP_VERB = re.compile(r"Info([0-9]*)$")
_STATE_FAMILY = re.compile(r"Pipeline(.+?)State$")


def _family_leaf(match: re.Match[str], noun: str) -> str:
	"""The part of a family member's name that is not the family itself.

	VkPhysicalDeviceMeshShaderFeaturesEXT keeps `MeshShader`, but the family's
	own base structure has nothing left over -- VkPhysicalDeviceFeatures2 would
	strip to `2`, which cannot start an identifier. Those fall back to the family
	noun, so the name is always usable; which of them reads best is a judgement
	call for the overrides file.
	"""
	stem = match.group(1) + match.group(2)
	if not stem or stem[0].isdigit():
		return noun + stem
	return stem


def _split_author_tag(core: str, author_tags: frozenset[str]) -> tuple[str, str]:
	for tag in sorted(author_tags, key=len, reverse=True):
		if core.endswith(tag) and len(core) > len(tag):
			return core[: -len(tag)], tag
	return core, ""


def _bucket(
	struct: str,
	roots: frozenset[str],
	states: frozenset[str],
	members: frozenset[str],
	author_tags: frozenset[str],
	keep_verb: bool = False,
	queries: frozenset[str] = frozenset(),
) -> tuple[str, str, str]:
	"""(namespace, leaf stem, author tag) for one object."""
	core, tag = _split_author_tag(struct.removeprefix("Vk"), author_tags)
	family = _FEATURE_FAMILY.fullmatch(core)
	if family:
		return FEATURE_NAMESPACE, _family_leaf(family, "Features"), tag
	if struct in queries:
		# Read-side objects. Properties are the bulk of them and read well as a
		# family; the rest are one-off results like VkMemoryRequirements2.
		properties = _PROPERTY_FAMILY.fullmatch(core)
		if properties:
			return PROPERTY_NAMESPACE, _family_leaf(properties, "Properties"), tag
		return RESULT_NAMESPACE, _INFO_SUFFIX.sub(lambda match: match.group(1), core), tag
	leaf = (_INFO_SUFFIX_KEEP_VERB if keep_verb else _INFO_SUFFIX).sub(
		lambda match: match.group(1), core
	)
	# Pipeline state: the namespace carries both the `Pipeline` prefix and the
	# `State` suffix that all of them share.
	if struct in states:
		state = _STATE_FAMILY.fullmatch(leaf) or re.fullmatch(r"Pipeline(.+)", leaf)
		if state and state.group(1):
			return STATE_NAMESPACE, state.group(1), tag
	# A structure that another structure names -- through a pointer slot or an
	# array -- is something the caller builds and passes, exactly like a root.
	# Only pNext extensions belong in `option`.
	if struct in roots or struct in members:
		return "", leaf, tag
	return OPTION_NAMESPACE, leaf, tag


def _vendor_namespace(family: str, tag: str) -> str:
	"""Family first, then the vendor, so `feature::nv::mesh_shader` reads in order.

	Every author tag vk.xml declares gets its own namespace -- no list of "real"
	vendors to keep, and no tag suffixes to disambiguate with.
	"""
	return "::".join(part for part in (family, tag.lower()) if part)


def suggest_object_names(
	closure: list[str],
	roots: frozenset[str],
	states: frozenset[str],
	members: frozenset[str],
	author_tags: frozenset[str],
	queries: frozenset[str] = frozenset(),
) -> dict[str, Suggestion]:
	"""Name every object at once, because collisions are only visible in bulk.

	The vendor tag becomes a namespace segment rather than a suffix, so two
	vendors' takes on the same idea never collide in the first place. Within one
	namespace, a tie falls back to keeping the verb, which is what tells
	VkAccelerationStructureCreateInfoNV from VkAccelerationStructureInfoNV.
	"""
	plain = {
		struct: _bucket(struct, roots, states, members, author_tags, queries=queries)
		for struct in closure
	}
	verbose = {
		struct: _bucket(struct, roots, states, members, author_tags, keep_verb=True, queries=queries)
		for struct in closure
	}
	# Last resort: the core name with no suffix stripped at all.
	whole = {
		struct: (
			plain[struct][0],
			_split_author_tag(struct.removeprefix("Vk"), author_tags)[0],
			plain[struct][2],
		)
		for struct in closure
	}

	def qualified(bucket: tuple[str, str, str]) -> str:
		family, leaf, tag = bucket
		return f"{_vendor_namespace(family, tag)}::{'_'.join(_tokens(leaf))}"

	def group(keys: dict[str, str]) -> dict[str, list[str]]:
		out: dict[str, list[str]] = {}
		for struct, key in keys.items():
			out.setdefault(key, []).append(struct)
		return out

	chosen = {struct: qualified(bucket) for struct, bucket in plain.items()}
	# Tier 2 already happened inside _bucket (the vendor namespace). Tier 3 keeps
	# the verb, tier 4 keeps the whole core name -- and anything that stripped
	# down to nothing goes straight to tier 4.
	for contenders in group(chosen).values():
		if len(contenders) > 1:
			for struct in contenders:
				chosen[struct] = qualified(verbose[struct])
	for contenders in list(group(chosen).values()):
		for struct in contenders:
			if len(contenders) > 1 or not chosen[struct].rpartition("::")[2]:
				chosen[struct] = qualified(whole[struct])

	final = group(chosen)
	suggestions: dict[str, Suggestion] = {}
	for struct, key in chosen.items():
		namespace, _, value = key.rpartition("::")
		value = _escape_keyword(value)
		reviews = _reviews(plain[struct][1], value, author_tags)
		if len(final[key]) > 1:
			reviews.append("collision")
		suggestions[struct] = Suggestion(
			value=f"{namespace}::{value}" if namespace else value,
			reviews=reviews,
		)
	return suggestions


def suggest_enum_names(enum_types: list[str], author_tags: frozenset[str]) -> dict[str, Suggestion]:
	"""Name every plain enum at once; the vendor tag only stays to break a tie."""
	stems = {}
	for enum_type in enum_types:
		core, tag = _split_author_tag(enum_type.removeprefix("Vk"), author_tags)
		# A FlagBits type used as a single choice is just an enum; the suffix is
		# about the mask form, which is not what this name is for.
		core = re.sub(r"FlagBits([0-9]*)$", r"", core)
		stems[enum_type] = (core, tag)

	claims: dict[str, list[str]] = {}
	for enum_type, (core, _) in stems.items():
		claims.setdefault("_".join(_tokens(core)), []).append(enum_type)

	suggestions: dict[str, Suggestion] = {}
	for enum_type, (core, tag) in stems.items():
		words = _tokens(core)
		contested = len(claims["_".join(words)]) > 1
		if contested and tag:
			words = words + _tokens(tag)
		value = _escape_keyword("_".join(words))
		reviews = _reviews(core, value, author_tags)
		if contested and not tag:
			reviews.append("collision")
		suggestions[enum_type] = Suggestion(value=value, reviews=reviews)
	return suggestions


def suggest_command_name(command: str, author_tags: frozenset[str]) -> tuple[str, str]:
	"""(qualified function name, singular form) for one producer command.

	The vendor tag becomes a namespace, exactly as it does for objects, so
	vkCreateSwapchainKHR is vkfu::khr::create_swapchain.
	"""
	core, tag = _split_author_tag(command.removeprefix("vk"), author_tags)
	words = _tokens(core)
	namespace = tag.lower()
	name = "_".join(words)
	singular_words = words[:-1] + [re.sub(r"s$", "", words[-1])] if words else words
	singular = "_".join(singular_words)
	return (
		f"{namespace}::{name}" if namespace else name,
		f"{namespace}::{singular}" if namespace else singular,
	)


def suggest_count_name(command_name: str) -> str:
	"""The `count_*` twin of an enumerate's name.

	Same words, different verb: the two functions ask about the same thing, so
	only the leading verb changes. Review material like everything else here.
	"""
	namespace, _, leaf = command_name.rpartition("::")
	words = leaf.split("_")
	if words and words[0] in ("enumerate", "get"):
		words = words[1:]
	value = "_".join(["count"] + words)
	return f"{namespace}::{value}" if namespace else value


def suggest_alias_name(alias: str, target_name: str, author_tags: frozenset[str]) -> str | None:
	"""Where an alias lives: the target's own name, one namespace deeper.

	VkPhysicalDeviceTimelineSemaphoreFeaturesKHR is the same structure as the
	core name, so it keeps the leaf and gains the vendor namespace that says
	which spelling this is. None when the alias carries no tag to put there.
	"""
	_core, tag = _split_author_tag(alias.removeprefix("Vk"), author_tags)
	if not tag:
		return None
	namespace, _, leaf = target_name.rpartition("::")
	segments = [segment for segment in (namespace, tag.lower()) if segment]
	return "::".join(segments + [leaf])


def suggest_member(
	member: str,
	author_tags: frozenset[str] = frozenset(),
	*,
	leaf: str | None = None,
	kind: str | None = None,
) -> Suggestion:
	"""`leaf` is the object's own name, which the field should not restate.

	`feature::timeline_semaphore{.timeline_semaphore = true}` says the same thing
	twice, and the field only carries on-or-off, so it becomes `.enable`. Sibling
	flags that merely wear the object's name as a prefix lose it, turning
	`.tile_shading_per_tile_draw` into `.per_tile_draw`. A non-flag field that
	restates the object needs a human, because "enable" would be a lie.
	"""
	stem = _HUNGARIAN.sub("", member)
	value = "_".join(_tokens(stem))

	if leaf:
		owner = "_".join(_tokens(leaf))
		if value == owner:
			if kind == "bool":
				return Suggestion(value="enable", reviews=[])
			return Suggestion(value=value, reviews=["restates"])
		prefix = f"{owner}_"
		if value.startswith(prefix) and not value[len(prefix)].isdigit():
			trimmed = value[len(prefix) :]
			# A bare quantity needs its noun: viewportCount must not become
			# `count` just because the object is called `viewport`.
			if trimmed not in ("count", "size"):
				return Suggestion(value=_escape_keyword(trimmed), reviews=_reviews(stem, trimmed, author_tags))

	return Suggestion(value=_escape_keyword(value), reviews=_reviews(stem, value, author_tags))


def bit_prefix(flag_bits: str, bit_names: list[str], author_tags: frozenset[str] = frozenset()) -> str:
	"""The shared prefix of an enum's bit constants.

	Derived from the constants themselves where possible, because the type name
	is not a reliable guide (VkBufferUsageFlagBits2 carries its `2` in the
	middle of VK_BUFFER_USAGE_2_*). Falls back to the type name for enums with
	a single bit, where a common prefix would swallow the whole constant.
	"""
	stem = flag_bits[2:] if flag_bits.startswith("Vk") else flag_bits
	lowered = {tag.lower() for tag in author_tags}
	tokens = _tokens(stem)
	# The vendor tag and the Flag/Bits noise sit at the end of the type name but
	# not in the middle of the constants, so strip them from the tail only.
	while tokens and (tokens[-1] in lowered or re.fullmatch(r"flags?|bits?[0-9]*", tokens[-1])):
		tokens.pop()
	from_type = "VK_" + "_".join(token.upper() for token in tokens) + "_"

	candidates = [from_type]
	if len(bit_names) > 1:
		shared = bit_names[0]
		for name in bit_names[1:]:
			while not name.startswith(shared):
				shared = shared[:-1]
		shared = shared[: shared.rfind("_") + 1]
		if shared:
			candidates.append(shared)

	usable = [
		candidate
		for candidate in candidates
		if candidate and all(name.startswith(candidate) for name in bit_names)
	]
	return max(usable, key=len) if usable else ""


def _strip_value_prefix(value: str, prefix: str, author_tags: frozenset[str]) -> tuple[str, str, list[str]]:
	reviews: list[str] = []
	stem = value
	if prefix and value.startswith(prefix):
		stem = value[len(prefix) :]
	else:
		reviews.append("prefix")
	found = ""
	for tag in sorted(author_tags, key=len, reverse=True):
		if stem.endswith(f"_{tag}"):
			stem, found = stem[: -len(tag) - 1], tag
			break
	# A FlagBits type used as a single choice keeps the _BIT suffix in its
	# constants, and it says nothing once the type is an enum.
	return re.sub(r"_BIT$", "", stem), found, reviews


def suggest_enum_values(
	values: list[str],
	prefix: str,
	author_tags: frozenset[str] = frozenset(),
) -> dict[str, Suggestion]:
	"""Name one enum's constants together, so a tie can be broken by vendor tag.

	VK_QUERY_TYPE_PERFORMANCE_QUERY_KHR and ..._INTEL are different queries that
	strip to the same word, so both keep their tag.
	"""
	stripped = {value: _strip_value_prefix(value, prefix, author_tags) for value in values}
	claims: dict[str, list[str]] = {}
	for value, (stem, _, _) in stripped.items():
		claims.setdefault(stem.lower(), []).append(value)

	suggestions: dict[str, Suggestion] = {}
	for value, (stem, tag, reviews) in stripped.items():
		reviews = list(reviews)
		contested = len(claims[stem.lower()]) > 1
		text = f"{stem}_{tag}" if contested and tag else stem
		text = re.sub(r"[^0-9A-Za-z_]", "_", text).lower()
		if not text or text[0].isdigit():
			text = f"value_{text}"
			reviews.append("invalid")
		if contested and not tag:
			reviews.append("collision")
		if text in CPP_KEYWORDS:
			reviews.append("keyword")
		suggestions[value] = Suggestion(value=_escape_keyword(text), reviews=reviews)
	return suggestions


def suggest_bit(flag_bits: str, bit: str, prefix: str) -> Suggestion:
	reviews: list[str] = []
	stem = bit
	if prefix and bit.startswith(prefix):
		stem = bit[len(prefix) :]
	else:
		reviews.append("prefix")
	stem = re.sub(r"_BIT(_[A-Z]+)?$", "", stem)
	value = stem.lower()
	value = re.sub(r"[^0-9a-z_]", "_", value)
	if not value or value[0].isdigit():
		value = "bit_" + value
		reviews.append("invalid")
	if value in CPP_KEYWORDS:
		reviews.append("keyword")
	return Suggestion(value=_escape_keyword(value), reviews=reviews)
