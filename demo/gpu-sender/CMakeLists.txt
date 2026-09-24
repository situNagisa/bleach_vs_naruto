cmake_minimum_required(VERSION 3.28)

set(GPU_SENDER_BVN_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

	project(bvn_gpu_sender_demo LANGUAGES CXX)

	set(CMAKE_CXX_STANDARD 23)
	set(CMAKE_CXX_STANDARD_REQUIRED ON)
	set(CMAKE_CXX_EXTENSIONS OFF)

	add_library(gpu_sender_build_settings INTERFACE)
	target_compile_features(gpu_sender_build_settings INTERFACE cxx_std_23)

	if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND MSVC)
		target_compile_options(gpu_sender_build_settings INTERFACE
			/W4
			/permissive-
			/Zc:__cplusplus
			/utf-8
			/wd4100
			-clang:-std=c++2c
		)
	elseif(MSVC)
		target_compile_options(gpu_sender_build_settings INTERFACE
			/W4
			/permissive-
			/Zc:__cplusplus
			/Zc:preprocessor
			/utf-8
			/wd4100
			/std:c++latest
		)
	else()
		target_compile_options(gpu_sender_build_settings INTERFACE
			-Wall
			-Wextra
			-Wpedantic
			-std=c++2c
		)
	endif()

	file(TO_CMAKE_PATH "$ENV{NAGISA_LIBRARY_ROOT}/stdexec" GPU_SENDER_STDEXEC_ROOT_DEFAULT)
	set(GPU_SENDER_STDEXEC_ROOT "${GPU_SENDER_STDEXEC_ROOT_DEFAULT}" CACHE PATH "stdexec source root")
	if(NOT EXISTS "${GPU_SENDER_STDEXEC_ROOT}/include/stdexec/execution.hpp")
		message(FATAL_ERROR
			"GPU_SENDER_STDEXEC_ROOT must contain include/stdexec/execution.hpp: "
			"${GPU_SENDER_STDEXEC_ROOT}"
		)
	endif()

	add_library(gpu_sender_stdexec_headers INTERFACE)
	target_include_directories(gpu_sender_stdexec_headers SYSTEM INTERFACE
		"${GPU_SENDER_STDEXEC_ROOT}/include"
	)

	set(GPU_SENDER_PROJECT_LIBRARIES
		gpu_sender_build_settings
		gpu_sender_stdexec_headers
	)
else()
	set(GPU_SENDER_PROJECT_LIBRARIES
		bvn_build_settings
		STDEXEC::stdexec
	)
endif()

find_package(Threads REQUIRED)
find_package(Vulkan REQUIRED)

if(NOT TARGET vkfu::vkfu)
	set(VKFU_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
	set(VKFU_BUILD_TESTS OFF CACHE BOOL "" FORCE)
	add_subdirectory("${GPU_SENDER_BVN_ROOT}/vkfu" "${CMAKE_CURRENT_BINARY_DIR}/vkfu" EXCLUDE_FROM_ALL)
endif()

# 无窗口：只要 stdexec + vkfu + vkkl，不碰 SDL / nagisa。
add_executable(bvn_gpu_sender_demo
	main.cpp
	context.h
	sender.h
)
target_include_directories(bvn_gpu_sender_demo PRIVATE
	"${GPU_SENDER_BVN_ROOT}/include"
)
target_link_libraries(bvn_gpu_sender_demo PRIVATE
	${GPU_SENDER_PROJECT_LIBRARIES}
	Threads::Threads
	Vulkan::Vulkan
	vkfu::vkfu
)

set_target_properties(bvn_gpu_sender_demo PROPERTIES
	FOLDER demo
	OUTPUT_NAME gpu-sender
	CXX_SCAN_FOR_MODULES OFF
)
