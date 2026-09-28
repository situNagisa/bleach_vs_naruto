cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

	project(bvn_gpu_sender_concepts_demo LANGUAGES CXX)

	set(CMAKE_CXX_STANDARD 23)
	set(CMAKE_CXX_STANDARD_REQUIRED ON)
	set(CMAKE_CXX_EXTENSIONS OFF)

	add_library(gpu_sender_concepts_build_settings INTERFACE)
	target_compile_features(gpu_sender_concepts_build_settings INTERFACE cxx_std_23)

	if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND MSVC)
		target_compile_options(gpu_sender_concepts_build_settings INTERFACE
			/W4
			/permissive-
			/Zc:__cplusplus
			/utf-8
			/wd4100
			-clang:-std=c++2c
		)
	elseif(MSVC)
		target_compile_options(gpu_sender_concepts_build_settings INTERFACE
			/W4
			/permissive-
			/Zc:__cplusplus
			/Zc:preprocessor
			/utf-8
			/wd4100
			/std:c++latest
		)
	else()
		target_compile_options(gpu_sender_concepts_build_settings INTERFACE
			-Wall
			-Wextra
			-Wpedantic
			-std=c++2c
		)
	endif()

	file(TO_CMAKE_PATH "$ENV{NAGISA_LIBRARY_ROOT}/stdexec" GPU_SENDER_CONCEPTS_STDEXEC_ROOT_DEFAULT)
	set(GPU_SENDER_CONCEPTS_STDEXEC_ROOT "${GPU_SENDER_CONCEPTS_STDEXEC_ROOT_DEFAULT}" CACHE PATH "stdexec source root")
	if(NOT EXISTS "${GPU_SENDER_CONCEPTS_STDEXEC_ROOT}/include/stdexec/execution.hpp")
		message(FATAL_ERROR
			"GPU_SENDER_CONCEPTS_STDEXEC_ROOT must contain include/stdexec/execution.hpp: "
			"${GPU_SENDER_CONCEPTS_STDEXEC_ROOT}"
		)
	endif()

	add_library(gpu_sender_concepts_stdexec_headers INTERFACE)
	target_include_directories(gpu_sender_concepts_stdexec_headers SYSTEM INTERFACE
		"${GPU_SENDER_CONCEPTS_STDEXEC_ROOT}/include"
	)

	set(GPU_SENDER_CONCEPTS_PROJECT_LIBRARIES
		gpu_sender_concepts_build_settings
		gpu_sender_concepts_stdexec_headers
	)
else()
	set(GPU_SENDER_CONCEPTS_PROJECT_LIBRARIES
		bvn_build_settings
		STDEXEC::stdexec
	)
endif()

find_package(Threads REQUIRED)

# 整个 demo 里的 "vulkan 调用" 都是 mock_vulkan.h 里的替身：不需要 Vulkan SDK、
# loader 或者显卡就能构建、运行、验证——这份原型验证的是 CPO/concept/算法的类型
# 流转和调用序列，不是真实的 GPU 时序。
add_executable(bvn_gpu_sender_concepts_demo
	main.cpp
	algorithms.h
	concepts.h
	cpo.h
	image_state.h
	mock_vulkan.h
	swapchain.h
	tracked_value.h
)
target_link_libraries(bvn_gpu_sender_concepts_demo PRIVATE
	${GPU_SENDER_CONCEPTS_PROJECT_LIBRARIES}
	Threads::Threads
)

set_target_properties(bvn_gpu_sender_concepts_demo PROPERTIES
	FOLDER demo
	OUTPUT_NAME gpu-sender-concepts
	CXX_SCAN_FOR_MODULES OFF
)

# swapchain_frame.cpp / texture_upload.cpp：同一套 CPO/concept/算法接上真实的
# vkfu/vkkl/VkImage 类型，逐行对照 demo/new-arch-pro 里 render.h、main_menu.cpp
# 手写的 barrier/submit/present 序列。两个文件都没有 main()——只用来在真实类型下
# 编译期验证，不打算被真正链接执行（这台开发机没有 Vulkan loader/驱动）。用一个
# OBJECT 库把它们编译一遍，让 CI/构建流程能捕捉到这两个文件的真实破坏，同时不
# 强求下游链接、不要求真的存在可用的 Vulkan 设备。
find_package(Vulkan QUIET)
if(TARGET Vulkan::Vulkan)
	set(GPU_SENDER_CONCEPTS_BVN_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
	if(NOT TARGET vkfu::vkfu)
		set(VKFU_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
		set(VKFU_BUILD_TESTS OFF CACHE BOOL "" FORCE)
		add_subdirectory("${GPU_SENDER_CONCEPTS_BVN_ROOT}/vkfu" "${CMAKE_CURRENT_BINARY_DIR}/vkfu" EXCLUDE_FROM_ALL)
	endif()

	add_library(bvn_gpu_sender_concepts_vulkan_scenes OBJECT EXCLUDE_FROM_ALL
		swapchain_frame.cpp
		texture_upload.cpp
		algorithms.h
		concepts.h
		cpo.h
		vulkan_image_state.h
		vulkan_tracked_value.h
	)
	target_include_directories(bvn_gpu_sender_concepts_vulkan_scenes PRIVATE
		"${GPU_SENDER_CONCEPTS_BVN_ROOT}/include"
	)
	target_link_libraries(bvn_gpu_sender_concepts_vulkan_scenes PRIVATE
		${GPU_SENDER_CONCEPTS_PROJECT_LIBRARIES}
		Vulkan::Vulkan
		vkfu::vkfu
	)
	set_target_properties(bvn_gpu_sender_concepts_vulkan_scenes PROPERTIES
		FOLDER demo
		CXX_SCAN_FOR_MODULES OFF
	)
else()
	message(STATUS
		"gpu-sender-concepts: 找不到 Vulkan SDK，跳过 swapchain_frame.cpp/"
		"texture_upload.cpp 的真实 Vulkan 编译期验证（mock 版本 demo 不受影响）"
	)
endif()
