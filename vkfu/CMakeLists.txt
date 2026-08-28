cmake_minimum_required(VERSION 3.28)

project(vkfu VERSION 0.2.0 LANGUAGES CXX)

option(VKFU_BUILD_EXAMPLES "Build vkfu examples" ON)
option(VKFU_BUILD_TESTS "Build vkfu tests" ON)

find_package(Vulkan REQUIRED)

add_library(vkfu INTERFACE)
add_library(vkfu::vkfu ALIAS vkfu)
# C++23 for std::expected, which the generated create_* wrappers return.
target_compile_features(vkfu INTERFACE cxx_std_23)
target_include_directories(vkfu INTERFACE
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(vkfu INTERFACE Vulkan::Vulkan)

if(MSVC)
	target_compile_options(vkfu INTERFACE /W4 /permissive- /Zc:__cplusplus /utf-8)
else()
	target_compile_options(vkfu INTERFACE -Wall -Wextra -Wpedantic)
endif()

if(VKFU_BUILD_EXAMPLES)
	add_executable(vkfu_device_example examples/device.cpp)
	target_link_libraries(vkfu_device_example PRIVATE vkfu::vkfu)

	# Instance / device / swapchain bring-up with no helper library. It calls into
	# the loader, so it links against Vulkan.
	add_executable(vkfu_bootstrap_example examples/bootstrap.cpp)
	target_link_libraries(vkfu_bootstrap_example PRIVATE vkfu::vkfu)
endif()

if(VKFU_BUILD_TESTS)
	include(CTest)

	# The generator's own tests: unit tests over ir/naming/check plus a golden
	# header built from tools/tests/fixture.xml. No GPU, no vk.xml.
	find_package(Python3 COMPONENTS Interpreter)
	if(Python3_FOUND)
		add_test(NAME vkfu_generator
			COMMAND ${Python3_EXECUTABLE} -m unittest discover -s tests -t .
			WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/tools)
	endif()
	foreach(case IN ITEMS typical_structures native_expression pipeline_slots triangle_pipeline chain frame_recording extensions)
		add_executable(vkfu_${case} tests/${case}.cpp)
		target_link_libraries(vkfu_${case} PRIVATE vkfu::vkfu)
		add_test(NAME vkfu_${case} COMMAND vkfu_${case})
	endforeach()

	# The create_* wrappers call into the loader, so this one is a compile-only
	# check: building it instantiates every template body under test.
	foreach(case IN ITEMS producers commands queries)
		add_library(vkfu_${case} OBJECT tests/${case}.cpp)
		target_link_libraries(vkfu_${case} PRIVATE vkfu::vkfu)
	endforeach()
endif()
