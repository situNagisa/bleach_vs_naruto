cmake_minimum_required(VERSION 3.24)

project(vkfu VERSION 0.1.0 LANGUAGES CXX)

option(VKFU_BUILD_EXAMPLES "Build vkfu examples" ON)
option(VKFU_BUILD_TESTS "Build vkfu tests" ON)

find_package(Vulkan REQUIRED)

add_library(vkfu INTERFACE)
add_library(vkfu::vkfu ALIAS vkfu)
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
	add_executable(vkfu_tests tests/vkfu_tests.cpp)
	target_link_libraries(vkfu_tests PRIVATE vkfu::vkfu)
	add_test(NAME vkfu_tests COMMAND vkfu_tests)
endif()
