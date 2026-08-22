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
		"rasterization",
		"reconstruction",
		"reconvergence",
		"reinterpretation",
		"representable",
		"representation",
		"representative",
		"simultaneous",
		"maintenance",
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

	def struct_name(self, struct: str) -> str | None:
		return self.structs.get(struct)

	def member_name(self, struct: str, member: str) -> str | None:
		return self.members.get(struct, {}).get(member)

	def bit_name(self, flag_bits: str, bit: str) -> str | None:
		return self.bits.get(flag_bits, {}).get(bit)


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
	return Table(structs=structs, members=members, bits=bits)


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

_FEATURE_FAMILY = re.compile(r"PhysicalDevice(.*?)Features(2?)")
_INFO_SUFFIX = re.compile(r"(?:Create)?Info(2?)$")


def _split_author_tag(core: str, author_tags: frozenset[str]) -> tuple[str, str]:
	for tag in sorted(author_tags, key=len, reverse=True):
		if core.endswith(tag) and len(core) > len(tag):
			return core[: -len(tag)], tag
	return core, ""


def _bucket(struct: str, roots: frozenset[str], author_tags: frozenset[str]) -> tuple[str, str, str]:
	"""(namespace, leaf stem, author tag) for one object."""
	core, tag = _split_author_tag(struct.removeprefix("Vk"), author_tags)
	family = _FEATURE_FAMILY.fullmatch(core)
	if family:
		return FEATURE_NAMESPACE, family.group(1) + family.group(2), tag
	leaf = _INFO_SUFFIX.sub(r"\1", core)
	return ("" if struct in roots else OPTION_NAMESPACE), leaf, tag


def suggest_object_names(
	closure: list[str],
	roots: frozenset[str],
	author_tags: frozenset[str],
) -> dict[str, Suggestion]:
	"""Name every object at once, because collisions are only visible in bulk.

	A leaf keeps its vendor tag only when it has to: two objects landing on the
	same name both get qualified, and an untagged core object keeps the bare
	name because it has no tag to add.
	"""
	buckets = {struct: _bucket(struct, roots, author_tags) for struct in closure}
	claims: dict[tuple[str, str], list[str]] = {}
	for struct, (namespace, leaf, _) in buckets.items():
		claims.setdefault((namespace, "_".join(_tokens(leaf))), []).append(struct)

	suggestions: dict[str, Suggestion] = {}
	for struct, (namespace, leaf, tag) in buckets.items():
		words = _tokens(leaf)
		contested = len(claims[(namespace, "_".join(words))]) > 1
		if contested and tag:
			words = words + _tokens(tag)
		value = _escape_keyword("_".join(words))
		reviews = _reviews(leaf, value, author_tags)
		if contested and not tag:
			reviews.append("collision")
		suggestions[struct] = Suggestion(
			value=f"{namespace}::{value}" if namespace else value,
			reviews=reviews,
		)
	return suggestions


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
