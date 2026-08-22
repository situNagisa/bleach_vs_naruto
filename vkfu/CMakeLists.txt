cmake_minimum_required(VERSION 3.28)

project(vkfu VERSION 0.2.0 LANGUAGES CXX)

option(VKFU_BUILD_EXAMPLES "Build vkfu examples" ON)
option(VKFU_BUILD_TESTS "Build vkfu tests" ON)

find_package(Vulkan REQUIRED)

add_library(vkfu INTERFACE)
add_library(vkfu::vkfu ALIAS vkfu)
# std::bit_cast in a constant expression, plus default member initialisers on
# bit-fields, so C++20 is the floor.
target_compile_features(vkfu INTERFACE cxx_std_20)
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
endif()

if(VKFU_BUILD_TESTS)
	include(CTest)
	add_executable(vkfu_typical_structures tests/typical_structures.cpp)
	target_link_libraries(vkfu_typical_structures PRIVATE vkfu::vkfu)
	add_test(NAME vkfu_typical_structures COMMAND vkfu_typical_structures)
endif()
