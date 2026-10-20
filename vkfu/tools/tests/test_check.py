"""One deliberately broken table per check.

These are the errors the user should never have to hear about from a compiler:
an empty name, a duplicate, a keyword, a name that is both a class and a
namespace. Each test breaks exactly one thing and asserts the checker names it.
"""

from __future__ import annotations

import pathlib
import unittest

from vkfu_gen import check, ir, naming

FIXTURE = str(pathlib.Path(__file__).with_name("fixture.xml"))


def sound_table(registry: ir.Registry) -> naming.Table:
	"""A table that passes, built the way `rebuild` builds one."""
	objects = {
		"VkWidgetCreateInfo": "widget",
		"VkWidgetPart": "widget_part",
		"VkWidgetTuning": "widget_tuning",
		"VkWidgetColourInfoEXT": "option::ext::widget_colour",
		"VkWidgetLabelInfoKHR": "option::khr::widget_label",
		"VkWidgetLimits": "option::widget_limits",
		"VkWidgetWin32InfoKHR": "option::khr::widget_win32",
		"VkExtent": "extent",
		"VkBounds": "bounds",
		"VkZone": "zone",
		"VkWidgetProperties": "property::widget",
		"VkWidgetDriverPropertiesEXT": "property::ext::widget_driver",
	}
	members = {
		"VkWidgetCreateInfo": {
			"flags": "flags",
			"mode": "mode",
			"enableTracing": "enable_tracing",
			"pParts": "parts",
			"pTuning": "tuning",
			"extent": "extent",
			"bounds": "bounds",
			"weights": "weights",
		},
		"VkWidgetPart": {"index": "index"},
		"VkWidgetTuning": {"bias": "bias"},
		"VkWidgetColourInfoEXT": {"fallbackMode": "fallback_mode"},
		"VkWidgetLabelInfoKHR": {"tag": "tag"},
		"VkWidgetLimits": {"maxParts": "max_parts"},
		"VkWidgetWin32InfoKHR": {"handle": "handle"},
		"VkExtent": {"width": "width", "height": "height"},
		"VkBounds": {"zone": "zone", "size": "size"},
		"VkZone": {"mode": "mode", "depth": "depth"},
	}
	return naming.Table(
		structs=dict(objects),
		members={owner: dict(fields) for owner, fields in members.items()},
		bits={"VkWidgetFlagBits": {"VK_WIDGET_ALPHA_BIT": "alpha", "VK_WIDGET_GAMMA_BIT": "gamma"}},
		commands={
			"vkCreateWidget": "create_widget",
			"vkCmdSubmitParts": "cmd_submit_parts",
			"vkCmdEndWidget": "cmd_end_widget",
			"vkCmdSetWin32Widget": "khr::cmd_set_win32_widget",
			"vkEnumerateWidgetReports": "enumerate_widget_reports",
			"vkGetWidgetProperties": "get_widget_properties",
		},
		singulars={},
		counts={"vkEnumerateWidgetReports": "count_widget_reports"},
		aliases={"VkWidgetLimitsKHR": "option::khr::widget_limits"},
		enums={"VkWidgetMode": "widget_mode"},
		flags={"VkWidgetFlags": "widget"},
		values={"VkWidgetMode": {"VK_WIDGET_MODE_PLAIN": "plain", "VK_WIDGET_MODE_FANCY": "fancy"}},
		strings={
			"VK_EXT_WIDGET_COLOUR_EXTENSION_NAME": "ext::widget_colour_extension_name",
			"VK_KHR_WIDGET_BARE_EXTENSION_NAME": "khr::widget_bare_extension_name",
			"VK_KHR_WIDGET_WIN32_EXTENSION_NAME": "khr::widget_win32_extension_name",
		},
		layers={"VK_LAYER_FIXTURE_widget": "layer::fixture_widget"},
	)


class CheckTest(unittest.TestCase):
	@classmethod
	def setUpClass(cls) -> None:
		cls.registry = ir.load(FIXTURE)

	def problems(self, mutate=None) -> list[check.Problem]:
		table = sound_table(self.registry)
		if mutate is not None:
			mutate(table)
		return check.check(self.registry, table)

	def errors(self, mutate=None) -> list[check.Problem]:
		return [p for p in self.problems(mutate) if p.severity == "error"]

	def assert_error(self, kind: str, mutate) -> check.Problem:
		found = [p for p in self.errors(mutate) if p.kind == kind]
		self.assertTrue(found, f"expected a {kind} error, got {self.errors(mutate)}")
		return found[0]

	# ------------------------------------------------------------ baseline

	def test_a_sound_table_is_clean(self) -> None:
		self.assertEqual(self.errors(), [])

	# ------------------------------------------------------------ per-name

	def test_empty_object_name(self) -> None:
		self.assert_error("empty", lambda t: t.structs.update({"VkWidgetCreateInfo": ""}))

	def test_empty_leaf_after_a_namespace(self) -> None:
		# The real bug this came from: a family whose stem stripped to nothing
		# produced `feature::`, i.e. an anonymous struct.
		problem = self.assert_error(
			"empty", lambda t: t.structs.update({"VkWidgetCreateInfo": "feature::"})
		)
		self.assertIn("no leaf name", problem.detail)

	def test_leading_digit_is_not_an_identifier(self) -> None:
		self.assert_error("invalid", lambda t: t.structs.update({"VkWidgetCreateInfo": "property::2"}))

	def test_cpp_keyword(self) -> None:
		self.assert_error("keyword", lambda t: t.members["VkWidgetPart"].update({"index": "operator"}))

	def test_reserved_identifier(self) -> None:
		# Double underscore is reserved to the implementation, so the program is
		# ill-formed even though compilers accept it.
		self.assert_error("reserved", lambda t: t.members["VkWidgetPart"].update({"index": "my__index"}))
		self.assert_error("reserved", lambda t: t.members["VkWidgetPart"].update({"index": "_Index"}))

	# ------------------------------------------------------------ duplicates

	def test_two_objects_claiming_one_name(self) -> None:
		problem = self.assert_error(
			"duplicate", lambda t: t.structs.update({"VkWidgetPart": "widget"})
		)
		self.assertIn("widget", problem.detail)

	def test_two_fields_claiming_one_name(self) -> None:
		self.assert_error(
			"duplicate", lambda t: t.members["VkWidgetCreateInfo"].update({"mode": "flags"})
		)

	def test_two_bits_claiming_one_name(self) -> None:
		self.assert_error(
			"duplicate", lambda t: t.bits["VkWidgetFlagBits"].update({"VK_WIDGET_GAMMA_BIT": "alpha"})
		)

	def test_two_enumerators_claiming_one_name(self) -> None:
		self.assert_error(
			"duplicate", lambda t: t.values["VkWidgetMode"].update({"VK_WIDGET_MODE_FANCY": "plain"})
		)

	def test_two_commands_claiming_one_name(self) -> None:
		self.assert_error(
			"duplicate", lambda t: t.commands.update({"vkCmdSubmitParts": "create_widget"})
		)

	def test_a_count_twin_claiming_its_own_command_name(self) -> None:
		self.assert_error(
			"duplicate",
			lambda t: t.counts.update({"vkEnumerateWidgetReports": "enumerate_widget_reports"}),
		)

	def test_an_alias_claiming_a_real_object_name(self) -> None:
		# The alias is a `using` in the same namespace, so this is a redefinition.
		problem = self.assert_error(
			"duplicate", lambda t: t.aliases.update({"VkWidgetLimitsKHR": "option::widget_limits"})
		)
		self.assertIn("option::widget_limits", problem.detail)

	def test_a_missing_count_name(self) -> None:
		def drop(table: naming.Table) -> None:
			del table.counts["vkEnumerateWidgetReports"]

		self.assertEqual(
			self.assert_error("missing", drop).where, "command.vkEnumerateWidgetReports.count"
		)

	def test_an_alias_without_a_name_is_simply_not_generated(self) -> None:
		table = sound_table(self.registry)
		table.aliases.clear()
		self.assertEqual([p for p in check.check(self.registry, table) if p.severity == "error"], [])

	# ------------------------------------------------------------ structural

	def test_a_flags_name_is_checked_like_any_other(self) -> None:
		self.assert_error("keyword", lambda t: t.flags.update({"VkWidgetFlags": "class"}))

	def test_a_flags_type_needs_a_name(self) -> None:
		self.assert_error("missing", lambda t: t.flags.clear())

	def test_a_field_colliding_with_a_slot_template_parameter(self) -> None:
		self.assert_error(
			"scope", lambda t: t.members["VkWidgetCreateInfo"].update({"mode": "tuning_expression"})
		)

	def test_a_field_colliding_with_a_fixed_alias(self) -> None:
		self.assert_error(
			"scope", lambda t: t.members["VkWidgetCreateInfo"].update({"mode": "vulkan_tag_type"})
		)
		self.assert_error(
			"scope", lambda t: t.members["VkWidgetCreateInfo"].update({"mode": "storage_type"})
		)

	def test_a_name_that_is_both_a_class_and_a_namespace(self) -> None:
		# `property::widget` and `property::widget::driver` cannot coexist: the
		# second redefines `widget` as a different kind of symbol.
		problem = self.assert_error(
			"namespace",
			lambda t: t.structs.update({"VkWidgetDriverPropertiesEXT": "property::widget::driver"}),
		)
		self.assertIn("property::widget", problem.detail)

	def test_a_command_hiding_a_vkfu_cpo(self) -> None:
		problem = self.assert_error(
			"shadow", lambda t: t.commands.update({"vkCmdSubmitParts": "evaluate"})
		)
		self.assertIn("namespace vkfu", problem.detail)

	# ------------------------------------------------------------ coverage

	def test_a_missing_entry_is_an_error_in_closure_scope(self) -> None:
		def drop(table: naming.Table) -> None:
			del table.structs["VkWidgetTuning"]

		problem = self.assert_error("missing", drop)
		self.assertEqual(problem.where, "struct.VkWidgetTuning")

	def test_an_extension_name_without_an_entry_is_missing(self) -> None:
		problem = self.assert_error("missing", lambda t: t.strings.pop("VK_KHR_WIDGET_BARE_EXTENSION_NAME"))
		self.assertEqual(problem.where, "string.VK_KHR_WIDGET_BARE_EXTENSION_NAME")

	def test_a_string_constant_must_be_an_identifier(self) -> None:
		# What a macro that starts with a digit would produce unless a human names it.
		self.assert_error(
			"invalid", lambda t: t.strings.update({"VK_KHR_WIDGET_BARE_EXTENSION_NAME": "khr::16bit_extension_name"})
		)
		self.assert_error("invalid", lambda t: t.layers.update({"VK_LAYER_FIXTURE_widget": "layer::fixture-widget"}))

	def test_a_string_constant_cannot_share_a_name_with_a_command(self) -> None:
		def clash(table: naming.Table) -> None:
			table.strings["VK_KHR_WIDGET_BARE_EXTENSION_NAME"] = "khr::cmd_set_win32_widget"

		self.assert_error("duplicate", clash)

	def test_a_string_entry_for_no_extension_is_an_orphan(self) -> None:
		table = sound_table(self.registry)
		table.strings["VK_KHR_NOT_AN_EXTENSION_NAME"] = "khr::not_an_extension_name"
		orphans = [p for p in check.check(self.registry, table) if p.kind == "orphan"]
		self.assertEqual([p.where for p in orphans], ["string.VK_KHR_NOT_AN_EXTENSION_NAME"])

	def test_a_missing_entry_is_allowed_in_table_scope(self) -> None:
		table = sound_table(self.registry)
		del table.structs["VkWidgetTuning"]
		problems = check.check(self.registry, table, "table")
		self.assertEqual([p for p in problems if p.kind == "missing"], [])

	def test_an_orphan_entry_is_a_warning_not_an_error(self) -> None:
		table = sound_table(self.registry)
		table.structs["VkSomethingElse"] = "elsewhere"
		problems = check.check(self.registry, table)
		orphans = [p for p in problems if p.kind == "orphan"]
		self.assertEqual(len(orphans), 1)
		self.assertEqual(orphans[0].severity, "warning")

	def test_query_objects_need_a_name_but_no_fields(self) -> None:
		def drop(table: naming.Table) -> None:
			del table.structs["VkWidgetDriverPropertiesEXT"]

		self.assertEqual(
			self.assert_error("missing", drop).where, "struct.VkWidgetDriverPropertiesEXT"
		)
		# ...and no field of a query object is ever asked for.
		self.assertEqual(
			[p for p in self.errors() if "VkWidgetProperties.revision" in p.where], []
		)


if __name__ == "__main__":
	unittest.main()
