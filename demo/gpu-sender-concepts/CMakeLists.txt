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
