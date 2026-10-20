"""Where every generated declaration goes: which file, and what that file needs.

A file is a *unit*. Each extension is one unit, filed under its vendor:
VK_KHR_swapchain -> `khr/swapchain.h`. A core version is too big for one file,
so it is cut along the <require> blocks vk.xml already groups it into --
VK_VERSION_1_3's "Promoted from VK_KHR_synchronization2" block becomes
`version_1_3/synchronization2.h`, VK_VERSION_1_0's "Buffer commands" becomes
`version_1_0/buffer.h`. Every path is relative to the `v<version>/`
directory.

Platform and beta macros never appear inside a unit. A unit has a condition --
the macros it needs, its own plus everything it includes -- and the umbrella
that includes it is what tests them.
"""

from __future__ import annotations

import dataclasses
import re

from . import ir

# vk.xml's 1.0 <require> comments describe an area of the API in a sentence;
# these are the ones a sentence does not shorten into a usable file name.
# Everything else follows the rules in _section_name.
_SECTION_NAMES = {
	"Fundamental types used by many commands and structures": "types",
	"These types are part of the API, though not directly used in API commands or data structures": "types",
	"API constants": "constants",
	"Memory management API commands": "memory",
	"Sparse resource memory management API commands": "sparse_memory",
	"Queue semaphore commands": "semaphore",
	"Pass commands": "render_pass",
}

_EXTENSION = re.compile(r"VK_[A-Z0-9]+_(\w+)")


def section_name(comment: str) -> str:
	"""A <require> comment, as a file name.

	A block promoted from an extension is named after that extension; a block
	with no comment holds what the version itself added, such as
	VkPhysicalDeviceVulkan12Features, and is `core`.
	"""
	if not comment:
		return "core"
	if comment in _SECTION_NAMES:
		return _SECTION_NAMES[comment]
	match = _EXTENSION.search(comment)
	if match:
		return match.group(1).lower()
	words = re.sub(r" commands$", "", comment.strip()).lower()
	return re.sub(r"[^a-z0-9]+", "_", words).strip("_")


def vendor_of(module: str) -> str:
	"""VK_KHR_swapchain -> khr. Core versions have no vendor."""
	return module.split("_")[1].lower()


def is_core(module: str) -> bool:
	return module.startswith("VK_VERSION_")


def unit_path(module: str, section: str | None) -> str:
	if is_core(module):
		return f"{module.lower()[3:]}/{section}.h"  # version_1_3/synchronization2.h
	_, vendor, name = module.split("_", 2)
	return f"{vendor.lower()}/{name.lower()}.h"  # khr/swapchain.h


def umbrella_of(path: str) -> str:
	"""khr/swapchain.h -> khr.h; version_1_0/buffer.h -> version_1_0.h."""
	return path.split("/", 1)[0] + ".h"


def relative(source: str, target: str) -> str:
	"""How `source` spells an #include of `target`, both under one directory."""
	source_dir = source.rpartition("/")[0]
	target_dir, _, target_file = target.rpartition("/")
	if source_dir == target_dir:
		return target_file
	if not source_dir:
		return target
	return f"../{target}"


@dataclasses.dataclass
class Placement:
	"""Name -> unit, and the order units are listed in."""

	units: dict[str, str]  # type or command name -> unit path
	modules: dict[str, str]  # unit path -> VK_VERSION_1_0 / VK_KHR_swapchain
	guards: dict[str, str]  # unit path -> its own macro, for platform and beta extensions
	rank: dict[str, tuple[int, int]]  # unit path -> registry order


# Layer names are not in vk.xml, so no <extension> owns them: they get a unit of
# their own, after every extension, under an umbrella of its own.
LAYER_UNIT = "layer/names.h"
LAYER_MODULE = "layers"


def place(origins: ir.Origins) -> Placement:
	module_rank = {module: index for index, module in enumerate(origins.order)}
	placement = Placement(units={}, modules={}, guards={}, rank={})
	sections: dict[str, int] = {}
	for name, module in origins.modules.items():
		section = section_name(origins.sections.get(name, "")) if is_core(module) else None
		path = unit_path(module, section)
		placement.units[name] = path
		if path not in placement.modules:
			placement.modules[path] = module
			sections.setdefault(module, 0)
			placement.rank[path] = (module_rank[module], sections[module])
			sections[module] += 1
			if module in origins.guards:
				placement.guards[path] = origins.guards[module]

	# An extension with no type or command of its own still has a name, so it
	# still has somewhere to put it. Only units something is put into are written.
	for module in origins.order:
		path = unit_path(module, None) if not is_core(module) else None
		if path is not None and path not in placement.modules:
			placement.modules[path] = module
			placement.rank[path] = (module_rank[module], 0)
			if module in origins.guards:
				placement.guards[path] = origins.guards[module]

	placement.modules[LAYER_UNIT] = LAYER_MODULE
	placement.rank[LAYER_UNIT] = (len(origins.order), 0)
	return placement
