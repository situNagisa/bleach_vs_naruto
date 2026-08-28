"""The suggester, which is review material -- but its splitting rules are not.

Every case below is a real v1.4.328 identifier that was once split wrongly. The
suggestions themselves are a human's to accept or reject; the tokenisation is
not, so it is pinned here.
"""

from __future__ import annotations

import unittest

from vkfu_gen import naming

TAGS = frozenset({"KHR", "EXT", "NV", "AMD", "ARM", "QCOM", "FB", "MVK"})


class SplitTest(unittest.TestCase):
	def assert_tokens(self, identifier: str, expected: list[str]) -> None:
		self.assertEqual(naming._tokens(identifier), expected, identifier)

	def test_plain_camel_case(self) -> None:
		self.assert_tokens("maxImageDimension", ["max", "image", "dimension"])

	def test_trailing_version_digit_stays_on_the_word(self) -> None:
		self.assert_tokens("features2", ["features2"])
		self.assert_tokens("maintenance4", ["maintenance4"])

	def test_lexicon_joins_what_camel_case_split(self) -> None:
		# Dimension2D would otherwise become dimension2_d.
		self.assert_tokens("maxImageDimension2D", ["max", "image", "dimension", "2d"])
		self.assert_tokens("withoutYCbCrSampler", ["without", "ycbcr", "sampler"])
		self.assert_tokens("shaderBFloat16Type", ["shader", "bfloat16", "type"])

	def test_lexicon_splits_a_run_with_no_boundary(self) -> None:
		self.assert_tokens("textureCompressionASTCHDR", ["texture", "compression", "astc", "hdr"])
		self.assert_tokens("clustercullingShader", ["cluster", "culling", "shader"])

	def test_acronym_plural_is_a_marker_not_a_word(self) -> None:
		# pStdSPSs is "the SPS entries", not sp + ss.
		self.assert_tokens("pStdSPSs", ["p", "std", "sps"])

	def test_hungarian_prefix_is_not_part_of_the_name(self) -> None:
		self.assertEqual(naming.suggest_member("pNext").value, "next")
		self.assertEqual(naming.suggest_member("ppGeometries").value, "geometries")
		self.assertEqual(naming.suggest_member("pfnCallback").value, "callback")


class MemberTest(unittest.TestCase):
	def test_a_lone_flag_restating_the_object_becomes_enable(self) -> None:
		suggestion = naming.suggest_member(
			"timelineSemaphore", TAGS, leaf="timeline_semaphore", kind="bool"
		)
		self.assertEqual(suggestion.value, "enable")

	def test_a_non_flag_restating_the_object_needs_a_human(self) -> None:
		suggestion = naming.suggest_member("extent", TAGS, leaf="extent", kind="scalar")
		self.assertIn("restates", suggestion.reviews)

	def test_the_object_name_prefix_is_dropped(self) -> None:
		suggestion = naming.suggest_member(
			"tileShadingPerTileDraw", TAGS, leaf="tile_shading", kind="bool"
		)
		self.assertEqual(suggestion.value, "per_tile_draw")

	def test_a_bare_quantity_keeps_its_noun(self) -> None:
		# viewportCount must not become `count` just because the object is called
		# `viewport` -- the field would read as nothing at all.
		suggestion = naming.suggest_member("viewportCount", TAGS, leaf="viewport")
		self.assertEqual(suggestion.value, "viewport_count")

	def test_keywords_are_escaped(self) -> None:
		self.assertEqual(naming.suggest_member("operator").value, "operator_")
		self.assertIn("keyword", naming.suggest_member("operator").reviews)


class BitTest(unittest.TestCase):
	def test_prefix_from_the_constants_beats_the_type_name(self) -> None:
		# VkBufferUsageFlagBits2 carries its 2 in the middle of the constants, so
		# the type name is not a usable guide.
		prefix = naming.bit_prefix(
			"VkBufferUsageFlagBits2",
			[
				"VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT",
				"VK_BUFFER_USAGE_2_TRANSFER_DST_BIT",
				"VK_BUFFER_USAGE_2_INDEX_BUFFER_BIT",
			],
			TAGS,
		)
		self.assertEqual(prefix, "VK_BUFFER_USAGE_2_")

	def test_few_constants_share_more_than_the_prefix(self) -> None:
		# Documented rather than desired: the longest usable candidate wins, so an
		# enum with two closely-named bits strips further than the type name would.
		# The table is where a human shortens or lengthens that.
		prefix = naming.bit_prefix(
			"VkBufferUsageFlagBits2",
			["VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT", "VK_BUFFER_USAGE_2_TRANSFER_DST_BIT"],
			TAGS,
		)
		self.assertEqual(prefix, "VK_BUFFER_USAGE_2_TRANSFER_")

	def test_single_bit_falls_back_to_the_type_name(self) -> None:
		prefix = naming.bit_prefix("VkWidgetFlagBits", ["VK_WIDGET_ALPHA_BIT"], TAGS)
		self.assertEqual(prefix, "VK_WIDGET_")

	def test_bit_suffix_is_dropped(self) -> None:
		self.assertEqual(
			naming.suggest_bit("VkWidgetFlagBits", "VK_WIDGET_ALPHA_BIT", "VK_WIDGET_").value,
			"alpha",
		)

	def test_a_constant_outside_the_prefix_is_flagged(self) -> None:
		suggestion = naming.suggest_bit("VkWidgetFlagBits", "VK_OTHER_THING_BIT", "VK_WIDGET_")
		self.assertIn("prefix", suggestion.reviews)


class EnumValueTest(unittest.TestCase):
	def test_a_tie_is_broken_by_the_vendor_tag(self) -> None:
		named = naming.suggest_enum_values(
			[
				"VK_QUERY_TYPE_PERFORMANCE_QUERY_KHR",
				"VK_QUERY_TYPE_PERFORMANCE_QUERY_INTEL",
			],
			"VK_QUERY_TYPE_",
			TAGS | {"INTEL"},
		)
		self.assertEqual(
			sorted(entry.value for entry in named.values()),
			["performance_query_intel", "performance_query_khr"],
		)

	def test_a_leading_digit_is_not_an_identifier(self) -> None:
		named = naming.suggest_enum_values(["VK_THING_2D"], "VK_THING_", TAGS)
		suggestion = named["VK_THING_2D"]
		self.assertTrue(suggestion.value.isidentifier())
		self.assertIn("invalid", suggestion.reviews)


class ObjectTest(unittest.TestCase):
	def name_of(self, structs: list[str], **kwargs) -> dict[str, str]:
		suggestions = naming.suggest_object_names(
			structs,
			kwargs.get("roots", frozenset(structs)),
			kwargs.get("states", frozenset()),
			kwargs.get("members", frozenset()),
			TAGS,
			kwargs.get("queries", frozenset()),
		)
		return {name: suggestion.value for name, suggestion in suggestions.items()}

	def test_the_vendor_tag_becomes_a_namespace(self) -> None:
		names = self.name_of(["VkWidgetCreateInfoEXT"])
		self.assertEqual(names["VkWidgetCreateInfoEXT"], "ext::widget")

	def test_features_get_their_own_namespace(self) -> None:
		names = self.name_of(["VkPhysicalDeviceMeshShaderFeaturesEXT"], roots=frozenset())
		self.assertEqual(names["VkPhysicalDeviceMeshShaderFeaturesEXT"], "feature::ext::mesh_shader")

	def test_a_family_base_never_strips_to_a_digit(self) -> None:
		# PhysicalDeviceFeatures2 strips to "2", which cannot start an identifier.
		names = self.name_of(["VkPhysicalDeviceFeatures2"], roots=frozenset())
		self.assertEqual(names["VkPhysicalDeviceFeatures2"], "feature::features2")
		self.assertTrue(names["VkPhysicalDeviceFeatures2"].rpartition("::")[2].isidentifier())

	def test_properties_land_in_the_read_side_namespace(self) -> None:
		structs = ["VkPhysicalDeviceDriverProperties", "VkMemoryRequirements2"]
		names = self.name_of(structs, roots=frozenset(), queries=frozenset(structs))
		self.assertEqual(names["VkPhysicalDeviceDriverProperties"], "property::driver")
		self.assertEqual(names["VkMemoryRequirements2"], "result::memory_requirements2")

	def test_a_collision_falls_back_to_keeping_the_verb(self) -> None:
		# VkAccelerationStructureCreateInfoNV and VkAccelerationStructureInfoNV
		# both strip to `acceleration_structure` unless the verb is kept.
		names = self.name_of(
			["VkAccelerationStructureCreateInfoNV", "VkAccelerationStructureInfoNV"]
		)
		self.assertNotEqual(
			names["VkAccelerationStructureCreateInfoNV"],
			names["VkAccelerationStructureInfoNV"],
		)

	def test_every_suggested_leaf_is_an_identifier(self) -> None:
		structs = [
			"VkPhysicalDeviceFeatures",
			"VkPhysicalDeviceProperties2",
			"VkSparseImageMemoryBind",
			"VkSparseImageMemoryBindInfo",
		]
		for value in self.name_of(structs, queries=frozenset({"VkPhysicalDeviceProperties2"})).values():
			for segment in value.split("::"):
				self.assertTrue(segment.isidentifier(), value)


class CommandTest(unittest.TestCase):
	def test_the_vendor_tag_becomes_a_namespace(self) -> None:
		name, singular = naming.suggest_command_name("vkCreateSwapchainKHR", TAGS)
		self.assertEqual(name, "khr::create_swapchain")
		self.assertEqual(singular, "khr::create_swapchain")

	def test_a_plural_producer_gets_a_singular_form(self) -> None:
		name, singular = naming.suggest_command_name("vkCreateGraphicsPipelines", TAGS)
		self.assertEqual(name, "create_graphics_pipelines")
		self.assertEqual(singular, "create_graphics_pipeline")


if __name__ == "__main__":
	unittest.main()
