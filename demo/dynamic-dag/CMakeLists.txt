cmake_minimum_required(VERSION 3.28)

set(DYNAMIC_DAG_BVN_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

	project(bvn_dynamic_dag_demo LANGUAGES CXX)

	set(CMAKE_CXX_STANDARD 23)
	set(CMAKE_CXX_STANDARD_REQUIRED ON)
	set(CMAKE_CXX_EXTENSIONS OFF)

	add_library(dynamic_dag_build_settings INTERFACE)
	target_compile_features(dynamic_dag_build_settings INTERFACE cxx_std_23)

	if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND MSVC)
		target_compile_options(dynamic_dag_build_settings INTERFACE
			/W4
			/permissive-
			/Zc:__cplusplus
			/utf-8
			/wd4100
			-clang:-std=c++2c
		)
	elseif(MSVC)
		target_compile_options(dynamic_dag_build_settings INTERFACE
			/W4
			/permissive-
			/Zc:__cplusplus
			/Zc:preprocessor
			/utf-8
			/wd4100
			/std:c++latest
		)
	else()
		target_compile_options(dynamic_dag_build_settings INTERFACE
			-Wall
			-Wextra
			-Wpedantic
		)
	endif()

	file(TO_CMAKE_PATH "$ENV{NAGISA_LIBRARY_ROOT}/stdexec" DYNAMIC_DAG_STDEXEC_ROOT_DEFAULT)
	set(DYNAMIC_DAG_STDEXEC_ROOT "${DYNAMIC_DAG_STDEXEC_ROOT_DEFAULT}" CACHE PATH "stdexec source root")
	if(NOT EXISTS "${DYNAMIC_DAG_STDEXEC_ROOT}/include/stdexec/execution.hpp")
		message(FATAL_ERROR
			"DYNAMIC_DAG_STDEXEC_ROOT must contain include/stdexec/execution.hpp: "
			"${DYNAMIC_DAG_STDEXEC_ROOT}"
		)
	endif()

	add_library(dynamic_dag_stdexec_headers INTERFACE)
	target_include_directories(dynamic_dag_stdexec_headers SYSTEM INTERFACE
		"${DYNAMIC_DAG_STDEXEC_ROOT}/include"
	)

	set(DYNAMIC_DAG_PROJECT_LIBRARIES
		dynamic_dag_build_settings
		dynamic_dag_stdexec_headers
	)
else()
	set(DYNAMIC_DAG_PROJECT_LIBRARIES
		bvn_build_settings
		STDEXEC::stdexec
	)
endif()

find_package(Threads REQUIRED)

add_executable(bvn_dynamic_dag_demo
	main.cpp
	dag.h
)
target_link_libraries(bvn_dynamic_dag_demo PRIVATE
	${DYNAMIC_DAG_PROJECT_LIBRARIES}
	Threads::Threads
)

set_target_properties(bvn_dynamic_dag_demo PROPERTIES
	FOLDER demo
	OUTPUT_NAME dynamic-dag
	# 这个 demo 不用 C++20 模块。让 CMake 对 C++23 目标默认开启的模块依赖扫描
	# （-fmodules-ts）介入的话，GCC 16 会在扫描 stdexec 的头时 ICE。
	CXX_SCAN_FOR_MODULES OFF
)
