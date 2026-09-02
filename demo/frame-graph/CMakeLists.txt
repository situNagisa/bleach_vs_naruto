cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

	project(bvn_frame_graph_demo LANGUAGES CXX)

	set(CMAKE_CXX_STANDARD 23)
	set(CMAKE_CXX_STANDARD_REQUIRED ON)
	set(CMAKE_CXX_EXTENSIONS OFF)

	add_library(frame_graph_build_settings INTERFACE)
	target_compile_features(frame_graph_build_settings INTERFACE cxx_std_23)

	if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND MSVC)
		target_compile_options(frame_graph_build_settings INTERFACE
			/W4
			/permissive-
			/Zc:__cplusplus
			/utf-8
			/wd4100
			-clang:-std=c++2c
		)
	elseif(MSVC)
		target_compile_options(frame_graph_build_settings INTERFACE
			/W4
			/permissive-
			/Zc:__cplusplus
			/Zc:preprocessor
			/utf-8
			/wd4100
			/std:c++latest
		)
	else()
		target_compile_options(frame_graph_build_settings INTERFACE
			-Wall
			-Wextra
			-Wpedantic
		)
	endif()

	file(TO_CMAKE_PATH "$ENV{NAGISA_LIBRARY_ROOT}/stdexec" FRAME_GRAPH_STDEXEC_ROOT_DEFAULT)
	set(FRAME_GRAPH_STDEXEC_ROOT "${FRAME_GRAPH_STDEXEC_ROOT_DEFAULT}" CACHE PATH "stdexec source root")
	if(NOT EXISTS "${FRAME_GRAPH_STDEXEC_ROOT}/include/stdexec/execution.hpp")
		message(FATAL_ERROR
			"FRAME_GRAPH_STDEXEC_ROOT must contain include/stdexec/execution.hpp: "
			"${FRAME_GRAPH_STDEXEC_ROOT}"
		)
	endif()

	add_library(frame_graph_stdexec_headers INTERFACE)
	target_include_directories(frame_graph_stdexec_headers SYSTEM INTERFACE
		"${FRAME_GRAPH_STDEXEC_ROOT}/include"
	)

	set(FRAME_GRAPH_PROJECT_LIBRARIES
		frame_graph_build_settings
		frame_graph_stdexec_headers
	)
else()
	set(FRAME_GRAPH_PROJECT_LIBRARIES
		bvn_build_settings
		STDEXEC::stdexec
	)
endif()

find_package(Threads REQUIRED)

add_executable(bvn_frame_graph_demo
	main.cpp
	frame_graph.h
	renderer.h
	entities.h
)
target_link_libraries(bvn_frame_graph_demo PRIVATE
	${FRAME_GRAPH_PROJECT_LIBRARIES}
	Threads::Threads
)

set_target_properties(bvn_frame_graph_demo PROPERTIES
	FOLDER demo
	OUTPUT_NAME frame-graph
	# 这个 demo 不用 C++20 模块。让 CMake 对 C++23 目标默认开启的模块依赖扫描
	# 介入的话，GCC 16 会在扫描 stdexec 的头时 ICE。
	CXX_SCAN_FOR_MODULES OFF
)
