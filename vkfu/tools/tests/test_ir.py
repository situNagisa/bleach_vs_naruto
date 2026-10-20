"""Stage 1 against a fixture registry.

The point of these is regression: when vk.xml gains a construct, or a predicate
is retuned, the shape of the IR should not change silently.
"""

from __future__ import annotations

import pathlib
import unittest

from vkfu_gen import ir, plan

FIXTURE = str(pathlib.Path(__file__).with_name("fixture.xml"))


class LoadTest(unittest.TestCase):
	@classmethod
	def setUpClass(cls) -> None:
		cls.registry = ir.load(FIXTURE)

	def test_header_version_and_tags(self) -> None:
		self.assertEqual(self.registry.header_version, "42")
		# No VK_HEADER_VERSION_COMPLETE in the fixture, so the newest <feature> is used.
		self.assertEqual(self.registry.api_version, "1.1.42")
		self.assertEqual(self.registry.author_tags, ["EXT", "KHR"])

	def test_every_type_and_command_has_the_module_that_introduces_it(self) -> None:
		modules = self.registry.origins.modules
		self.assertEqual(modules["VkWidgetCreateInfo"], "VK_VERSION_1_0")
		self.assertEqual(modules["VkWidgetLimits"], "VK_VERSION_1_1")
		# The alias is introduced by the extension, not by the core version.
		self.assertEqual(modules["VkWidgetLimitsKHR"], "VK_KHR_widget_limits")
		# Commands too, including one in a require block with no types.
		self.assertEqual(modules["vkCreateWidget"], "VK_VERSION_1_0")
		self.assertEqual(modules["vkCmdSetWin32Widget"], "VK_KHR_widget_win32")

	def test_core_names_remember_their_require_block(self) -> None:
		self.assertEqual(self.registry.origins.sections["VkWidgetCreateInfo"], "")
		self.assertNotIn("VkWidgetLimitsKHR", self.registry.origins.sections)

	def test_a_platform_extension_records_its_macro(self) -> None:
		self.assertEqual(self.registry.origins.guards, {"VK_KHR_widget_win32": "VK_USE_PLATFORM_WIN32_KHR"})

	def test_modules_are_core_versions_then_extensions_by_number(self) -> None:
		self.assertEqual(
			self.registry.origins.order,
			[
				"VK_VERSION_1_0",
				"VK_VERSION_1_1",
				"VK_EXT_widget_colour",
				"VK_KHR_widget_label",
				"VK_KHR_widget_limits",
				"VK_KHR_widget_win32",
				"VK_KHR_widget_bare",
			],
		)

	def test_an_extension_enumerant_knows_its_value(self) -> None:
		# Offsets count from the extension's own block, the way vulkan_core.h does.
		from vkfu_gen.ir import _enum_literal
		import xml.etree.ElementTree as ET

		enum = ET.fromstring('<enum offset="4" extends="VkResult" dir="-" name="X"/>')
		self.assertEqual(_enum_literal(enum, "2"), "-1000001004")
		self.assertEqual(_enum_literal(ET.fromstring('<enum bitpos="3" name="Y"/>'), None), "0x8")

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
		# VkBounds is reached from the create-info, VkExtent from it and from VkBounds,
		# VkZone from VkBounds.
		self.assertEqual(self.registry.plain, ["VkBounds", "VkExtent", "VkZone"])

	def test_an_embedded_plain_structure_is_a_value_field(self) -> None:
		fields = {
			field.member.name: field
			for field in plan.plan_struct(self.registry.structs["VkWidgetCreateInfo"], self.registry).fields
		}
		self.assertEqual((fields["bounds"].kind, fields["bounds"].target), ("value", "VkBounds"))
		self.assertEqual(fields["weights"].kind, "array")
		nested = plan.plan_struct(self.registry.structs["VkBounds"], self.registry).fields
		self.assertEqual([(f.member.name, f.kind) for f in nested], [("zone", "value"), ("size", "scalar")])

	def test_a_plain_structure_that_is_the_native_one_renamed_stays_native(self) -> None:
		# VkExtent is width and height; its param would be exactly that, so a field
		# of the type keeps the native structure rather than demanding a copy.
		self.assertTrue(plan.is_transparent(self.registry.structs["VkExtent"], self.registry))
		fields = {
			field.member.name: field
			for field in plan.plan_struct(self.registry.structs["VkWidgetCreateInfo"], self.registry).fields
		}
		self.assertEqual((fields["extent"].kind, fields["extent"].cpp_type), ("scalar", "VkExtent"))
		# An enum member is something the param changes, so VkZone is not transparent,
		# and neither is what embeds it.
		self.assertFalse(plan.is_transparent(self.registry.structs["VkZone"], self.registry))
		self.assertFalse(plan.is_transparent(self.registry.structs["VkBounds"], self.registry))

	def test_an_extensions_name_macro_is_read_and_nothing_else_quoted(self) -> None:
		self.assertEqual(
			[(n.macro, n.value, n.module) for n in self.registry.extension_names],
			[
				("VK_EXT_WIDGET_COLOUR_EXTENSION_NAME", "VK_EXT_widget_colour", "VK_EXT_widget_colour"),
				("VK_KHR_WIDGET_BARE_EXTENSION_NAME", "VK_KHR_widget_bare", "VK_KHR_widget_bare"),
				("VK_KHR_WIDGET_WIN32_EXTENSION_NAME", "VK_KHR_widget_win32", "VK_KHR_widget_win32"),
			],
		)  # no SPEC_VERSION (not a string), no vulkansc-only extension

	def test_a_command_with_nothing_to_substitute_is_still_wrapped(self) -> None:
		wrapper = next(c for c in self.registry.wrappers if c.name == "vkCmdEndWidget")
		self.assertEqual([(a.kind, a.member.name) for a in wrapper.arguments], [("passthrough", "recorder")])

	def test_extra_success_codes_keep_a_bare_command_native(self) -> None:
		# Its wrapper would throw on VK_NOT_READY, which is not an error.
		self.assertNotIn("vkGetWidgetStatus", [c.name for c in self.registry.wrappers])

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
