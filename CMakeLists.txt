cmake_minimum_required(VERSION 3.28)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

project(bvn VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

option(BVN_WARNINGS_AS_ERRORS "Treat compiler warnings as errors." OFF)

add_library(bvn_build_settings INTERFACE)
target_compile_features(bvn_build_settings INTERFACE cxx_std_23)

set(BVN_WARNING_AS_ERROR_FLAG)

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
	if(MSVC)
		target_compile_options(bvn_build_settings INTERFACE
			/W4
			/permissive-
			/Zc:__cplusplus
			/utf-8
			/wd4100
			-clang:-std=c++2c
		)
		set(BVN_WARNING_AS_ERROR_FLAG /WX)
	else()
		target_compile_options(bvn_build_settings INTERFACE
			-Wall
			-Wextra
			-Wpedantic
			-std=c++2c
		)
		set(BVN_WARNING_AS_ERROR_FLAG -Werror)
	endif()
elseif(MSVC)
	target_compile_options(bvn_build_settings INTERFACE
		/W4
		/permissive-
		/Zc:__cplusplus
		/Zc:preprocessor
		/utf-8
		/wd4100
		/std:c++latest
	)
	set(BVN_WARNING_AS_ERROR_FLAG /WX)
else()
	target_compile_options(bvn_build_settings INTERFACE
		-Wall
		-Wextra
		-Wpedantic
	)
	set(BVN_WARNING_AS_ERROR_FLAG -Werror)
endif()

if(BVN_WARNINGS_AS_ERRORS)
	target_compile_options(bvn_build_settings INTERFACE ${BVN_WARNING_AS_ERROR_FLAG})
endif()

file(TO_CMAKE_PATH "$ENV{NAGISA_LIBRARY_ROOT}/stdexec" BVN_STDEXEC_ROOT_DEFAULT)
set(BVN_STDEXEC_ROOT "${BVN_STDEXEC_ROOT_DEFAULT}" CACHE PATH "stdexec source root")
add_library(bvn_stdexec_headers INTERFACE)
target_include_directories(bvn_stdexec_headers SYSTEM INTERFACE
	"$<BUILD_INTERFACE:${BVN_STDEXEC_ROOT}/include>"
)
add_library(STDEXEC::stdexec ALIAS bvn_stdexec_headers)
# if(NOT EXISTS "${BVN_STDEXEC_ROOT}/CMakeLists.txt")
# 	message(FATAL_ERROR "BVN_STDEXEC_ROOT must point to the stdexec source root: ${BVN_STDEXEC_ROOT}")
# endif()
# 
# set(STDEXEC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
# set(STDEXEC_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
# set(STDEXEC_INSTALL OFF CACHE BOOL "" FORCE)
# set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
# set(BVN_STDEXEC_BINARY_DIR "${PROJECT_BINARY_DIR}/_deps/stdexec")
# file(MAKE_DIRECTORY "${BVN_STDEXEC_BINARY_DIR}")
# file(WRITE "${BVN_STDEXEC_BINARY_DIR}/RAPIDS.cmake"
# 	"list(PREPEND CMAKE_MODULE_PATH \"${PROJECT_SOURCE_DIR}/cmake/stdexec-rapids-shim\")\n"
# )
# file(WRITE "${BVN_STDEXEC_BINARY_DIR}/execution.bs" "Revision: 0\n")
# add_subdirectory("${BVN_STDEXEC_ROOT}" "${BVN_STDEXEC_BINARY_DIR}" EXCLUDE_FROM_ALL)

file(TO_CMAKE_PATH "$ENV{NAGISA_ROOT}" BVN_NAGISA_ROOT_DEFAULT)
set(BVN_NAGISA_ROOT "${BVN_NAGISA_ROOT_DEFAULT}" CACHE PATH "Nagisa source root")

if(NOT EXISTS "${BVN_NAGISA_ROOT}/include/nagisa")
	message(FATAL_ERROR "BVN_NAGISA_ROOT must point to a Nagisa root with include/nagisa: ${BVN_NAGISA_ROOT}")
endif()

add_library(bvn_nagisa_headers INTERFACE)
target_include_directories(bvn_nagisa_headers SYSTEM INTERFACE
	"$<BUILD_INTERFACE:${BVN_NAGISA_ROOT}/include>"
)

include("./cmake/module.cmake")

add_subdirectory(source)
