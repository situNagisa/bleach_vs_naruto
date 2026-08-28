"""Stage 1 against a fixture registry.

The point of these is regression: when vk.xml gains a construct, or a predicate
is retuned, the shape of the IR should not change silently.
"""

from __future__ import annotations

import pathlib
import unittest

from vkfu_gen import ir

FIXTURE = str(pathlib.Path(__file__).with_name("fixture.xml"))


class LoadTest(unittest.TestCase):
	@classmethod
	def setUpClass(cls) -> None:
		cls.registry = ir.load(FIXTURE)

	def test_header_version_and_tags(self) -> None:
		self.assertEqual(self.registry.header_version, "42")
		self.assertEqual(self.registry.author_tags, ["EXT", "KHR"])

	def test_roots_are_const_pointer_stype_parameters(self) -> None:
		# VkWidgetPart is a root because vkCmdSubmitParts takes it directly, and
		# VkExtent is not, because it has no sType.
		self.assertEqual(
			self.registry.roots,
			["VkWidgetCreateInfo", "VkWidgetPart", "VkWidgetWin32InfoKHR"],
		)

	def test_closure_follows_structextends_and_pointers(self) -> None:
		self.assertEqual(
			self.registry.closure,
			[
				"VkWidgetColourInfoEXT",
				"VkWidgetCreateInfo",
				"VkWidgetLabelInfoKHR",
				"VkWidgetLimits",
				"VkWidgetPart",
				"VkWidgetTuning",
				"VkWidgetWin32InfoKHR",
			],
		)

	def test_plain_is_the_stypeless_members(self) -> None:
		self.assertEqual(self.registry.plain, ["VkExtent"])

	def test_reference_is_a_lone_const_pointer(self) -> None:
		self.assertEqual(
			self.registry.references, [("VkWidgetCreateInfo", "pTuning", "VkWidgetTuning")]
		)

	def test_element_is_a_counted_array(self) -> None:
		self.assertEqual(
			self.registry.elements, [("VkWidgetCreateInfo", "pParts", "VkWidgetPart")]
		)

	def test_allow_duplicate_is_read(self) -> None:
		self.assertTrue(self.registry.structs["VkWidgetLabelInfoKHR"].allow_duplicate)
		self.assertFalse(self.registry.structs["VkWidgetColourInfoEXT"].allow_duplicate)

	def test_platform_guard_reaches_struct_and_command(self) -> None:
		self.assertEqual(
			self.registry.structs["VkWidgetWin32InfoKHR"].guards,
			["VK_USE_PLATFORM_WIN32_KHR"],
		)
		wrapper = next(c for c in self.registry.wrappers if c.name == "vkCmdSetWin32Widget")
		self.assertEqual(wrapper.guards, ["VK_USE_PLATFORM_WIN32_KHR"])

	def test_query_side_is_separate_from_the_closure(self) -> None:
		self.assertEqual(self.registry.query_roots, ["VkWidgetProperties"])
		self.assertEqual(
			self.registry.query_closure,
			["VkWidgetDriverPropertiesEXT", "VkWidgetProperties"],
		)
		for name in self.registry.query_closure:
			self.assertNotIn(name, self.registry.closure)

	def test_command_families_do_not_overlap(self) -> None:
		names = (
			[c.name for c in self.registry.producers]
			+ [c.name for c in self.registry.wrappers]
			+ [c.name for c in self.registry.queries]
			+ [c.name for c in self.registry.chain_queries]
		)
		self.assertEqual(len(names), len(set(names)))
		self.assertEqual([c.name for c in self.registry.producers], ["vkCreateWidget"])
		self.assertEqual([c.name for c in self.registry.queries], ["vkEnumerateWidgetReports"])
		self.assertEqual([c.name for c in self.registry.chain_queries], ["vkGetWidgetProperties"])

	def test_span_folding_records_the_count(self) -> None:
		wrapper = next(c for c in self.registry.wrappers if c.name == "vkCmdSubmitParts")
		self.assertEqual(wrapper.spans, {"partCount": "pParts"})
		self.assertEqual(
			[(a.kind, a.member.name) for a in wrapper.arguments],
			[("passthrough", "recorder"), ("span", "pParts")],
		)

	def test_bitmask_maps_to_its_flag_bits(self) -> None:
		self.assertEqual(
			self.registry.bitmask_to_flagbits["VkWidgetFlags"], "VkWidgetFlagBits"
		)
		bits = self.registry.flagbits["VkWidgetFlagBits"]
		self.assertEqual([(b.name, b.bitpos) for b in bits.bits if b.bitpos is not None],
			[("VK_WIDGET_ALPHA_BIT", 0), ("VK_WIDGET_GAMMA_BIT", 2)])
		# A value-defined entry is not a bit-field position.
		self.assertIn("VK_WIDGET_NONE", [b.name for b in bits.bits])
		self.assertIsNone(next(b for b in bits.bits if b.name == "VK_WIDGET_NONE").bitpos)


class ProvenanceTest(unittest.TestCase):
	@classmethod
	def setUpClass(cls) -> None:
		cls.registry = ir.load(FIXTURE)

	def test_extension_only(self) -> None:
		entry = self.registry.provenance["VkWidgetColourInfoEXT"]
		self.assertIsNone(entry.core)
		self.assertEqual(entry.extensions, ["VK_EXT_widget_colour"])

	def test_promoted_type_finds_its_alias_extension(self) -> None:
		# vk.xml names VK_KHR_widget_limits against VkWidgetLimitsKHR, not against
		# the core name, so this only works if aliases are followed.
		entry = self.registry.provenance["VkWidgetLimits"]
		self.assertEqual(entry.core, "VK_VERSION_1_1")
		self.assertEqual(entry.extensions, ["VK_KHR_widget_limits"])

	def test_core_only(self) -> None:
		entry = self.registry.provenance["VkWidgetCreateInfo"]
		self.assertEqual(entry.core, "VK_VERSION_1_0")
		self.assertEqual(entry.extensions, [])

	def test_instance_extension_is_flagged(self) -> None:
		self.assertTrue(self.registry.provenance["VkWidgetWin32InfoKHR"].instance)
		self.assertFalse(self.registry.provenance["VkWidgetColourInfoEXT"].instance)


class ProfileTest(unittest.TestCase):
	def test_core_ceiling_drops_later_versions(self) -> None:
		registry = ir.load(FIXTURE, profile=ir.Profile(core=0, extensions=frozenset()))
		self.assertNotIn("VkWidgetLimits", registry.closure)
		self.assertNotIn("VkWidgetColourInfoEXT", registry.closure)
		self.assertIn("VkWidgetCreateInfo", registry.closure)

	def test_named_extensions_come_back(self) -> None:
		registry = ir.load(
			FIXTURE,
			profile=ir.Profile(core=0, extensions=frozenset({"VK_EXT_widget_colour"})),
		)
		self.assertIn("VkWidgetColourInfoEXT", registry.closure)
		self.assertNotIn("VkWidgetLabelInfoKHR", registry.closure)

	def test_default_profile_takes_everything(self) -> None:
		self.assertEqual(len(ir.load(FIXTURE).closure), 7)


if __name__ == "__main__":
	unittest.main()
