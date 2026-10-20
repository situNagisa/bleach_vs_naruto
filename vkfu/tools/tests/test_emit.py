"""Stage 2 against a golden tree.

The golden is the whole point: a diff against it is the review. Anything that
changes generated code shows up as a reviewable text change rather than as a
compile error three steps later.

To accept a new golden, read the diff first, then:

	python -m vkfu_gen gen --xml tests/fixture.xml --table tests/fixture.naming.toml \\
		--scope closure --out tests/golden
"""

from __future__ import annotations

import pathlib
import unittest

from vkfu_gen import check, emit, ir, naming

HERE = pathlib.Path(__file__).parent
FIXTURE = str(HERE / "fixture.xml")
TABLE = str(HERE / "fixture.naming.toml")
GOLDEN = HERE / "golden"


def generate_files() -> dict[str, str]:
	registry = ir.load(FIXTURE)
	table = naming.load_table(TABLE)
	files, _warnings = emit.generate(registry, table, "closure")
	return files


def generate() -> str:
	"""Every file, umbrellas first then units, for tests that look for a snippet."""
	files = generate_files()
	return "\n".join(files[name] for name in sorted(files, key=lambda name: (name.count("/"), name)))


class GoldenTest(unittest.TestCase):
	def test_matches_the_checked_in_tree(self) -> None:
		produced = generate_files()
		expected = {
			path.relative_to(GOLDEN).as_posix(): path.read_text(encoding="utf-8")
			for path in GOLDEN.rglob("*.h")
		}
		self.assertEqual(sorted(produced), sorted(expected), "the golden tree has different files")
		for name in sorted(produced):
			if produced[name] == expected[name]:
				continue
			for index, (left, right) in enumerate(
				zip(produced[name].splitlines(), expected[name].splitlines()), start=1
			):
				if left != right:
					self.fail(
						f"{name} differs at line {index}:\n"
						f"  generated: {left}\n"
						f"  golden:    {right}\n"
						"read the diff, then regenerate as described in this module's docstring"
					)
			self.fail(f"{name} differs in length")

	def test_generation_is_deterministic(self) -> None:
		self.assertEqual(generate_files(), generate_files())


class ContentTest(unittest.TestCase):
	@classmethod
	def setUpClass(cls) -> None:
		cls.text = generate()

	def contains(self, needle: str) -> None:
		self.assertIn(needle, self.text, needle)

	def test_flags_become_a_bit_field_with_the_gaps_spelled_out(self) -> None:
		self.contains("VkWidgetFlags alpha : 1 = 0;")
		self.contains("VkWidgetFlags _reserved_1 : 1 = 0;")
		self.contains("VkWidgetFlags gamma : 1 = 0;")
		self.contains("VkWidgetFlags _reserved_3 : 29 = 0;")

	def test_the_bit_layout_is_asserted_not_assumed(self) -> None:
		self.contains("static_assert(::std::bit_cast<VkWidgetFlags>(widget{.alpha = 1}) == VK_WIDGET_ALPHA_BIT);")

	def test_a_flags_type_is_one_shared_struct_not_a_nested_one(self) -> None:
		# The field names the shared struct, so a value moves between any two
		# fields of the same flags type without a cast.
		self.contains("namespace vkfu::flags\n{\nstruct widget\n{")
		self.contains("\t::vkfu::flags::widget flags{};")
		self.assertNotIn("flags_type", self.text)
		self.assertEqual(self.text.count("VkWidgetFlags alpha : 1 = 0;"), 1)

	def test_the_tag_namespace_is_called_tag(self) -> None:
		self.contains("namespace vkfu::tag\n{")
		self.assertNotIn("vkfu::obj", self.text)
		self.assertNotIn("obj::", self.text)

	def test_a_value_defined_entry_is_not_a_bit(self) -> None:
		self.assertNotIn("VK_WIDGET_NONE", self.text)

	def test_a_count_and_pointer_pair_becomes_one_span(self) -> None:
		self.contains("::std::span<VkWidgetPart const> parts{};")
		self.contains(".partCount = static_cast<::std::uint32_t>(parts.size()),")
		self.assertNotIn("part_count", self.text)

	def test_a_lone_const_pointer_becomes_a_slot(self) -> None:
		self.contains("template<::vkfu::reference_expression_for<tag::widget_tuning> tuning_expression = ::vkfu::absent_expression>")
		self.contains("::vkfu::reference_slot<&VkWidgetCreateInfo::pTuning,")

	def test_a_fixed_array_is_copied_even_alongside_a_slot(self) -> None:
		# A designator cannot fill a C array, so the head needs a name first. The
		# slot path used to skip this and drop the field silently.
		self.contains("auto value = VkWidgetCreateInfo{")
		self.contains("::std::ranges::copy(weights, value.weights);")

	def test_allow_duplicate_reaches_the_trait(self) -> None:
		self.contains(
			"struct vulkan_object_trait<tag::option::khr::widget_label>\n"
			"{\n"
			"\tconstexpr static auto root = false;\n"
			"\tconstexpr static auto branch = false;\n"
			"\tconstexpr static auto allow_duplicate = true;\n"
			"};"
		)

	def test_structextends_becomes_a_compat_edge(self) -> None:
		self.contains(
			"inline constexpr auto is_vulkan_object_compatible_with_v<tag::widget, tag::option::ext::widget_colour> = true;"
		)

	def test_a_platform_extension_is_guarded_once_as_a_whole(self) -> None:
		# One #if around the win32 unit in its vendor's umbrella, nothing inside it.
		self.assertEqual(self.text.count("#if defined(VK_USE_PLATFORM_WIN32_KHR)"), 1)
		opens = sum(1 for line in self.text.splitlines() if line.startswith("#if"))
		closes = sum(1 for line in self.text.splitlines() if line.startswith("#endif"))
		self.assertEqual(opens, closes)

	def test_query_objects_get_a_tag_but_no_param(self) -> None:
		self.contains("struct vulkan_object_native<tag::result::widget_properties>")
		self.contains("constexpr static auto structure_type = VK_STRUCTURE_TYPE_WIDGET_PROPERTIES;")
		# No param: nobody fills a query in.
		self.assertNotIn("struct widget_properties\n{", self.text)

	def test_extension_requirements_come_from_the_registry(self) -> None:
		self.contains(
			'constexpr static ::std::array<char const*, 1> names{"VK_EXT_widget_colour"};'
		)
		# Promoted: core has it at 1.1, the alias's extension below that.
		self.contains(
			'constexpr static ::std::array<char const*, 1> names{"VK_KHR_widget_limits"};'
		)
		self.contains("constexpr static ::std::uint32_t core = VK_API_VERSION_1_1;")

	def test_span_command(self) -> None:
		self.contains(
			"inline void cmd_submit_parts(VkRecorder recorder, ::std::span<VkWidgetPart const> parts)"
		)
		self.contains(
			"::vkCmdSubmitParts(recorder, static_cast<::std::uint32_t>(parts.size()), parts.data());"
		)

	def test_two_call_enumerate_splits_into_count_and_write(self) -> None:
		# Nothing is allocated, so the header needs no container.
		self.assertNotIn("::std::vector", self.text)
		self.contains("[[nodiscard]] inline auto count_widget_reports(VkDevice device) -> ::std::uint32_t")
		self.contains(
			"[[nodiscard]] inline auto enumerate_widget_reports(VkDevice device,"
			" ::std::span<VkWidgetReport> out) -> ::std::span<VkWidgetReport>"
		)
		# VK_INCOMPLETE is not a failure: it says the span was filled and there
		# was more. count_* is how you find out.
		self.contains("if (outcome != ::VK_SUCCESS && outcome != ::VK_INCOMPLETE)")
		self.contains("return out.first(count);")

	def test_the_iterator_overload_is_contiguous_not_merely_output(self) -> None:
		# The driver writes through a raw pointer, so there has to be one.
		self.contains("template<::std::contiguous_iterator Out>")
		self.contains("requires ::std::same_as<::std::iter_value_t<Out>, VkWidgetReport>")
		self.contains("::std::span<VkWidgetReport>{::std::to_address(out), count}")

	def test_an_alias_is_a_using_beside_the_real_name(self) -> None:
		# Same structure, one namespace deeper, in both tag and param.
		self.contains("using widget_limits = ::vkfu::tag::option::widget_limits;")
		self.contains("using widget_limits = ::vkfu::param::option::widget_limits;")

	def test_chain_query_takes_the_extras_as_a_pack(self) -> None:
		self.contains("template<::vkfu::query_extension_of<::vkfu::tag::result::widget_properties>... Extras>")
		self.contains("::std::addressof(chain.head())")

	def test_the_producer_takes_an_expression(self) -> None:
		self.contains("::vkfu::expression_for<::vkfu::tag::widget>")

	def test_an_embedded_plain_structure_takes_its_param_not_the_native_one(self) -> None:
		# The field is typed with the param, and the parent evaluates it. A native
		# VkBounds field would leave `.bounds = {.zone = {...}}` the only place in a
		# param that still speaks Vulkan's names.
		self.contains("\t::vkfu::param::bounds bounds{};")
		self.contains("\t\t\t.bounds = bounds.evaluate(),")
		self.assertNotIn("\tVkBounds bounds{};", self.text)
		self.contains("\t::vkfu::param::zone zone{};")
		self.contains("\t\t\t.zone = zone.evaluate(),")

	def test_a_plain_structure_that_is_the_native_one_renamed_keeps_the_native_field(self) -> None:
		# VkExtent is width and height either way. Its param exists, but a field of
		# the type stays native, so a VkExtent from Vulkan goes straight in.
		self.contains("\tVkExtent extent{};")
		self.contains("\t\t\t.extent = extent,")
		self.assertNotIn("::vkfu::param::extent extent", self.text)
		self.contains("\tVkExtent size{};")
		self.contains("struct extent\n{")  # the param is still generated

	def test_the_embedded_param_is_declared_before_the_param_that_embeds_it(self) -> None:
		# `bounds` sorts before `zone`, so name order alone would get this wrong.
		core = self.text
		self.assertLess(core.index("struct zone\n{"), core.index("struct bounds\n{"))
		# And plain params come before the chain objects that embed them.
		# (`struct widget` alone would find the flags struct of the same name first.)
		self.assertLess(core.index("struct bounds\n{"), core.index("using vulkan_tag_type = tag::widget;"))

	def test_a_command_with_nothing_to_substitute_is_still_wrapped(self) -> None:
		self.contains("inline void cmd_end_widget(VkRecorder recorder)")
		self.contains("::vkCmdEndWidget(recorder);")

	def test_a_wrapper_that_would_throw_on_a_success_code_is_not_generated(self) -> None:
		self.assertNotIn("get_widget_status", self.text)

	def test_an_extension_name_is_a_constant_in_its_vendor_namespace(self) -> None:
		self.contains('namespace vkfu::ext\n{\ninline constexpr auto widget_colour_extension_name = "VK_EXT_widget_colour";\n}')
		# The literal, not the macro: a platform extension's macro only exists there.
		self.assertNotIn("= VK_EXT_WIDGET_COLOUR_EXTENSION_NAME", self.text)

	def test_a_layer_name_is_a_constant_too(self) -> None:
		self.contains('namespace vkfu::layer\n{\ninline constexpr auto fixture_widget = "VK_LAYER_FIXTURE_widget";\n}')

	def test_only_the_extension_name_is_a_constant(self) -> None:
		# Not its SPEC_VERSION, and not the name of an extension for another api.
		self.assertNotIn("spec_version", self.text)
		self.assertNotIn("scsonly", self.text)


class SplitTest(unittest.TestCase):
	@classmethod
	def setUpClass(cls) -> None:
		registry = ir.load(FIXTURE)
		table = naming.load_table(TABLE)
		cls.files, _ = emit.generate(registry, table, "closure")

	def test_the_tree_is_named_after_the_registry_version(self) -> None:
		# vulkan/ under vkfu's headers, then v<major>.<minor>.<VK_HEADER_VERSION>.
		self.assertTrue(all(name.startswith("vulkan/v1.1.42") for name in self.files))
		self.assertIn("vulkan/v1.1.42.h", self.files)

	def test_core_is_cut_by_area_and_extensions_are_filed_by_vendor(self) -> None:
		self.assertEqual(
			sorted(self.files),
			[
				"vulkan/v1.1.42.h",
				"vulkan/v1.1.42/ext.h",
				"vulkan/v1.1.42/ext/widget_colour.h",
				"vulkan/v1.1.42/khr.h",
				"vulkan/v1.1.42/khr/widget_bare.h",
				"vulkan/v1.1.42/khr/widget_label.h",
				"vulkan/v1.1.42/khr/widget_limits.h",
				"vulkan/v1.1.42/khr/widget_win32.h",
				"vulkan/v1.1.42/layer.h",
				"vulkan/v1.1.42/layer/names.h",
				"vulkan/v1.1.42/version_1_0.h",
				"vulkan/v1.1.42/version_1_0/core.h",
				"vulkan/v1.1.42/version_1_1.h",
				"vulkan/v1.1.42/version_1_1/core.h",
			],
		)

	def test_a_unit_holds_every_kind_of_thing_it_introduces(self) -> None:
		core = self.files["vulkan/v1.1.42/version_1_0/core.h"]
		for marker in (
			"struct widget{};",  # tag
			"enum class widget_mode",  # enum
			"struct vulkan_object_trait<tag::widget>",  # trait
			"namespace vkfu::param",  # param
			"auto create_widget(",  # command
		):
			self.assertIn(marker, core)

	def test_nothing_lands_in_a_unit_that_does_not_introduce_it(self) -> None:
		self.assertNotIn("widget_colour", self.files["vulkan/v1.1.42/version_1_0/core.h"])
		self.assertNotIn("create_widget", self.files["vulkan/v1.1.42/ext/widget_colour.h"])
		self.assertIn("cmd_set_win32_widget", self.files["vulkan/v1.1.42/khr/widget_win32.h"])

	def test_an_extension_with_nothing_else_still_gets_a_file_for_its_name(self) -> None:
		bare = self.files["vulkan/v1.1.42/khr/widget_bare.h"]
		self.assertIn('inline constexpr auto widget_bare_extension_name = "VK_KHR_widget_bare";', bare)
		self.assertIn("1 name constant", bare)
		self.assertNotIn("namespace vkfu::param", bare)
		self.assertIn('#include "khr/widget_bare.h"', self.files["vulkan/v1.1.42/khr.h"])

	def test_a_platform_extensions_name_goes_in_the_unit_the_umbrella_guards(self) -> None:
		win32 = self.files["vulkan/v1.1.42/khr/widget_win32.h"]
		self.assertIn('widget_win32_extension_name = "VK_KHR_widget_win32";', win32)
		# It says which macro it needs, in a comment, but tests nothing itself.
		self.assertEqual([line for line in win32.splitlines() if line.startswith("#if")], [])

	def test_layer_names_have_their_own_umbrella(self) -> None:
		self.assertIn('#include "layer/names.h"', self.files["vulkan/v1.1.42/layer.h"])
		self.assertIn('#include "v1.1.42/layer.h"', self.files["vulkan/v1.1.42.h"])

	def test_no_macro_guard_inside_a_unit(self) -> None:
		for name, body in self.files.items():
			if name.count("/") < 3:
				continue  # an umbrella
			guards = [line for line in body.splitlines() if line.startswith("#if") and "__clang__" not in line]
			self.assertEqual(guards, [], name)

	def test_the_vendor_umbrella_is_what_tests_the_platform(self) -> None:
		umbrella = self.files["vulkan/v1.1.42/khr.h"]
		self.assertIn(
			'#if defined(VK_USE_PLATFORM_WIN32_KHR)\n#include "khr/widget_win32.h"\n#endif', umbrella
		)
		self.assertIn('#include "khr/widget_label.h"', umbrella)
		self.assertNotIn("VK_USE_PLATFORM", self.files["vulkan/v1.1.42.h"])

	def test_the_core_framework_is_included_by_its_install_path(self) -> None:
		for name, body in self.files.items():
			self.assertNotIn('"../../', body, name)
		self.assertIn("#include <vkfu/expression.h>", self.files["vulkan/v1.1.42/version_1_0/core.h"])

	def test_generated_files_include_each_other_relatively(self) -> None:
		colour = self.files["vulkan/v1.1.42/ext/widget_colour.h"]
		self.assertIn('#include "../version_1_0/core.h"', colour)
		self.assertIn('#include "version_1_1/core.h"', self.files["vulkan/v1.1.42/version_1_1.h"])
		self.assertIn('#include "v1.1.42/version_1_0.h"', self.files["vulkan/v1.1.42.h"])

	def test_every_part_stands_on_its_own(self) -> None:
		for name, body in self.files.items():
			code = "\n".join(
				line for line in body.splitlines() if not line.lstrip().startswith(("#", "//"))
			)
			self.assertEqual(code.count("{"), code.count("}"), name)

	def test_an_include_always_names_a_file_that_exists(self) -> None:
		for name, body in self.files.items():
			directory = name.rpartition("/")[0]
			for line in body.splitlines():
				if not line.startswith('#include "'):
					continue
				target = line.split('"')[1]
				resolved = []
				for part in (f"{directory}/{target}" if directory else target).split("/"):
					if part == "..":
						resolved.pop()
					else:
						resolved.append(part)
				self.assertIn("/".join(resolved), self.files, f"{name}: {line}")

class FailureTest(unittest.TestCase):
	def test_a_dependency_cycle_between_modules_stops_generation(self) -> None:
		registry = ir.load(FIXTURE)
		table = naming.load_table(TABLE)
		# Pretend core's create-info hangs off the colour extension's structure
		# and the extension's enum came from core: each module would need the other.
		registry.origins.modules["VkWidgetMode"] = "VK_EXT_widget_colour"
		with self.assertRaises(emit.GenerationError) as raised:
			emit.generate(registry, table, "closure")
		self.assertTrue(any("cycle" in problem for problem in raised.exception.problems))

	def test_a_missing_name_stops_generation(self) -> None:
		registry = ir.load(FIXTURE)
		table = naming.load_table(TABLE)
		del table.structs["VkWidgetTuning"]
		with self.assertRaises(emit.GenerationError) as raised:
			emit.generate(registry, table, "closure")
		self.assertIn("struct.VkWidgetTuning", raised.exception.missing)

	def test_an_extension_name_without_a_table_entry_stops_generation(self) -> None:
		registry = ir.load(FIXTURE)
		table = naming.load_table(TABLE)
		del table.strings["VK_EXT_WIDGET_COLOUR_EXTENSION_NAME"]
		with self.assertRaises(emit.GenerationError) as raised:
			emit.generate(registry, table, "closure")
		self.assertIn("string.VK_EXT_WIDGET_COLOUR_EXTENSION_NAME", raised.exception.missing)
		# In table scope an unnamed one is simply not generated.
		files, _ = emit.generate(registry, table, "table")
		self.assertNotIn("widget_colour_extension_name", "".join(files.values()))

	def test_a_constant_cannot_take_a_commands_name(self) -> None:
		registry = ir.load(FIXTURE)
		table = naming.load_table(TABLE)
		table.strings["VK_KHR_WIDGET_BARE_EXTENSION_NAME"] = "khr::cmd_set_win32_widget"
		table.commands["vkCmdSetWin32Widget"] = "khr::cmd_set_win32_widget"
		with self.assertRaises(emit.GenerationError) as raised:
			emit.generate(registry, table, "closure")
		self.assertTrue(any("name collision" in problem for problem in raised.exception.problems))

	def test_an_unnamed_embedded_structure_stops_generation_even_in_table_scope(self) -> None:
		registry = ir.load(FIXTURE)
		table = naming.load_table(TABLE)
		# `table` scope skips what has no name, but a field typed with that
		# structure's param cannot be left dangling.
		del table.structs["VkBounds"]
		with self.assertRaises(emit.GenerationError) as raised:
			emit.generate(registry, table, "table")
		self.assertIn("struct.VkBounds", raised.exception.missing)

	def test_an_embedded_param_that_lands_after_its_user_stops_generation(self) -> None:
		registry = ir.load(FIXTURE)
		table = naming.load_table(TABLE)
		# Put the two in different namespaces, so grouping by namespace would
		# declare `bounds` (root) ahead of `deep::zone` and break the nesting.
		table.structs["VkZone"] = "deep::zone"
		with self.assertRaises(emit.GenerationError) as raised:
			emit.generate(registry, table, "closure")
		self.assertTrue(any("VkBounds embeds VkZone" in problem for problem in raised.exception.problems))

	def test_gen_and_check_agree_on_a_sound_table(self) -> None:
		registry = ir.load(FIXTURE)
		table = naming.load_table(TABLE)
		self.assertEqual(
			[p for p in check.check(registry, table) if p.severity == "error"], []
		)
		emit.generate(registry, table, "closure")


if __name__ == "__main__":
	unittest.main()
