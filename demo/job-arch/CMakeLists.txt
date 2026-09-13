cmake_minimum_required(VERSION 3.28)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

	project(bvn_job_arch_demo LANGUAGES CXX)

	set(CMAKE_CXX_STANDARD 23)
	set(CMAKE_CXX_STANDARD_REQUIRED ON)
	set(CMAKE_CXX_EXTENSIONS OFF)

	add_library(job_arch_build_settings INTERFACE)
	target_compile_features(job_arch_build_settings INTERFACE cxx_std_23)

	if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND MSVC)
		target_compile_options(job_arch_build_settings INTERFACE
			/W4
			/permissive-
			/Zc:__cplusplus
			/utf-8
			/wd4100
			-clang:-std=c++2c
		)
	elseif(MSVC)
		target_compile_options(job_arch_build_settings INTERFACE
			/W4
			/permissive-
			/Zc:__cplusplus
			/Zc:preprocessor
			/utf-8
			/wd4100
			/std:c++latest
		)
	else()
		target_compile_options(job_arch_build_settings INTERFACE
			-Wall
			-Wextra
			-Wpedantic
		)
	endif()

	file(TO_CMAKE_PATH "$ENV{NAGISA_LIBRARY_ROOT}/stdexec" JOB_ARCH_STDEXEC_ROOT_DEFAULT)
	set(JOB_ARCH_STDEXEC_ROOT "${JOB_ARCH_STDEXEC_ROOT_DEFAULT}" CACHE PATH "stdexec source root")
	if(NOT EXISTS "${JOB_ARCH_STDEXEC_ROOT}/include/stdexec/execution.hpp")
		message(FATAL_ERROR
			"JOB_ARCH_STDEXEC_ROOT must contain include/stdexec/execution.hpp: "
			"${JOB_ARCH_STDEXEC_ROOT}"
		)
	endif()

	add_library(job_arch_stdexec_headers INTERFACE)
	target_include_directories(job_arch_stdexec_headers SYSTEM INTERFACE
		"${JOB_ARCH_STDEXEC_ROOT}/include"
	)

	set(JOB_ARCH_PROJECT_LIBRARIES
		job_arch_build_settings
		job_arch_stdexec_headers
	)
else()
	set(JOB_ARCH_PROJECT_LIBRARIES
		bvn_build_settings
		STDEXEC::stdexec
	)
endif()

find_package(Threads REQUIRED)
find_package(EnTT CONFIG REQUIRED)

add_executable(bvn_job_arch_demo
	main.cpp
	dynamic_when_all.h
	manual_lifetime.h
)
target_link_libraries(bvn_job_arch_demo PRIVATE
	${JOB_ARCH_PROJECT_LIBRARIES}
	EnTT::EnTT
	Threads::Threads
)

set_target_properties(bvn_job_arch_demo PROPERTIES
	FOLDER demo
	OUTPUT_NAME job-arch
	CXX_SCAN_FOR_MODULES OFF
)
