"""Stage 2 against a golden file.

The golden is the whole point: a diff against it is the review. Anything that
changes generated code shows up as a reviewable text change rather than as a
compile error three steps later.

To accept a new golden, read the diff first, then:

	python -m vkfu_gen gen --xml tests/fixture.xml --table tests/fixture.naming.toml \\
		--scope closure --include-prefix ../../../include/vkfu/ --out tests/golden/fixture.h
"""

from __future__ import annotations

import pathlib
import unittest

from vkfu_gen import check, emit, ir, naming

HERE = pathlib.Path(__file__).parent
FIXTURE = str(HERE / "fixture.xml")
TABLE = str(HERE / "fixture.naming.toml")
GOLDEN = HERE / "golden" / "fixture.h"
PREFIX = "../../../include/vkfu/"


def generate() -> str:
	registry = ir.load(FIXTURE)
	table = naming.load_table(TABLE)
	text, _warnings = emit.generate(registry, table, "closure", PREFIX)
	return text


class GoldenTest(unittest.TestCase):
	def test_matches_the_checked_in_header(self) -> None:
		produced = generate()
		expected = GOLDEN.read_text(encoding="utf-8")
		if produced != expected:
			produced_lines = produced.splitlines()
			expected_lines = expected.splitlines()
			for index, (left, right) in enumerate(zip(produced_lines, expected_lines), start=1):
				if left != right:
					self.fail(
						f"golden differs at line {index}:\n"
						f"  generated: {left}\n"
						f"  golden:    {right}\n"
						"read the diff, then regenerate as described in this module's docstring"
					)
			self.fail(
				f"golden has {len(expected_lines)} lines, generated has {len(produced_lines)}"
			)

	def test_generation_is_deterministic(self) -> None:
		self.assertEqual(generate(), generate())


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
		self.contains("static_assert(::std::bit_cast<VkWidgetFlags>(widget<>::flags_type{.alpha = 1}) == VK_WIDGET_ALPHA_BIT);")

	def test_a_value_defined_entry_is_not_a_bit(self) -> None:
		self.assertNotIn("VK_WIDGET_NONE", self.text)

	def test_a_count_and_pointer_pair_becomes_one_span(self) -> None:
		self.contains("::std::span<VkWidgetPart const> parts{};")
		self.contains(".partCount = static_cast<::std::uint32_t>(parts.size()),")
		self.assertNotIn("part_count", self.text)

	def test_a_lone_const_pointer_becomes_a_slot(self) -> None:
		self.contains("template<::vkfu::reference_expression_for<obj::widget_tuning> tuning_expression = ::vkfu::absent_expression>")
		self.contains("::vkfu::reference_slot<&VkWidgetCreateInfo::pTuning,")

	def test_a_fixed_array_is_copied_even_alongside_a_slot(self) -> None:
		# A designator cannot fill a C array, so the head needs a name first. The
		# slot path used to skip this and drop the field silently.
		self.contains("auto value = VkWidgetCreateInfo{")
		self.contains("::std::ranges::copy(weights, value.weights);")

	def test_allow_duplicate_reaches_the_trait(self) -> None:
		self.contains(
			"struct vulkan_object_trait<obj::option::khr::widget_label>\n"
			"{\n"
			"\tconstexpr static auto root = false;\n"
			"\tconstexpr static auto branch = false;\n"
			"\tconstexpr static auto allow_duplicate = true;\n"
			"};"
		)

	def test_structextends_becomes_a_compat_edge(self) -> None:
		self.contains(
			"inline constexpr auto is_vulkan_object_compatible_with_v<obj::widget, obj::option::ext::widget_colour> = true;"
		)

	def test_platform_guards_wrap_everything_they_reach(self) -> None:
		# The win32 structure reaches the glue, the tag, the trait, the tag/native
		# maps, the extension list, the compat edge, the param and the command.
		self.assertGreaterEqual(self.text.count("#if defined(VK_USE_PLATFORM_WIN32_KHR)"), 7)
		opens = sum(1 for line in self.text.splitlines() if line.startswith("#if"))
		closes = sum(1 for line in self.text.splitlines() if line.startswith("#endif"))
		self.assertEqual(opens, closes)

	def test_query_objects_get_a_tag_but_no_param(self) -> None:
		self.contains("struct vulkan_object_native<obj::result::widget_properties>")
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
		# Same structure, one namespace deeper, in both obj and param.
		self.contains("using widget_limits = ::vkfu::obj::option::widget_limits;")
		self.contains("using widget_limits = ::vkfu::param::option::widget_limits;")

	def test_chain_query_takes_the_extras_as_a_pack(self) -> None:
		self.contains("template<::vkfu::query_extension_of<::vkfu::obj::result::widget_properties>... Extras>")
		self.contains("::std::addressof(chain.head())")

	def test_the_producer_takes_an_expression(self) -> None:
		self.contains("::vkfu::expression_for<::vkfu::obj::widget>")


class SplitTest(unittest.TestCase):
	@classmethod
	def setUpClass(cls) -> None:
		registry = ir.load(FIXTURE)
		table = naming.load_table(TABLE)
		cls.files, _ = emit.generate_split(registry, table, "closure", "fixture", PREFIX)

	def test_one_file_per_section_plus_an_umbrella(self) -> None:
		self.assertEqual(
			sorted(self.files),
			[
				"fixture.commands.h",
				"fixture.enums.h",
				"fixture.h",
				"fixture.objects.h",
				"fixture.param.h",
			],
		)

	def test_every_part_stands_on_its_own(self) -> None:
		for name, body in self.files.items():
			code = "\n".join(
				line for line in body.splitlines() if not line.lstrip().startswith(("#", "//"))
			)
			self.assertEqual(code.count("{"), code.count("}"), name)

	def test_the_umbrella_includes_every_part_in_dependency_order(self) -> None:
		umbrella = self.files["fixture.h"]
		positions = [umbrella.index(f"fixture.{name}.h") for name in emit.SECTION_ORDER]
		self.assertEqual(positions, sorted(positions))

	def test_a_part_declares_what_it_depends_on(self) -> None:
		self.assertIn('#include "fixture.objects.h"', self.files["fixture.param.h"])
		self.assertIn('#include "fixture.enums.h"', self.files["fixture.param.h"])
		self.assertIn('#include "fixture.objects.h"', self.files["fixture.commands.h"])
		# Nothing depends on the params, which is what makes the split worth doing.
		self.assertNotIn('#include "fixture.param.h"', self.files["fixture.commands.h"])

	def test_the_split_and_the_single_file_agree(self) -> None:
		joined = "".join(
			self.files[f"fixture.{name}.h"] for name in emit.SECTION_ORDER
		)
		for marker in ("struct widget", "namespace vkfu::enums", "namespace vkfu::obj"):
			self.assertIn(marker, joined)


class FailureTest(unittest.TestCase):
	def test_a_missing_name_stops_generation(self) -> None:
		registry = ir.load(FIXTURE)
		table = naming.load_table(TABLE)
		del table.structs["VkWidgetTuning"]
		with self.assertRaises(emit.GenerationError) as raised:
			emit.generate(registry, table, "closure", PREFIX)
		self.assertIn("struct.VkWidgetTuning", raised.exception.missing)

	def test_gen_and_check_agree_on_a_sound_table(self) -> None:
		registry = ir.load(FIXTURE)
		table = naming.load_table(TABLE)
		self.assertEqual(
			[p for p in check.check(registry, table) if p.severity == "error"], []
		)
		emit.generate(registry, table, "closure", PREFIX)


if __name__ == "__main__":
	unittest.main()
