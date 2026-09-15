cmake_minimum_required(VERSION 3.28)

set(CONSUMER_ARCH_BVN_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
	set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
	if(NOT VCPKG_MANIFEST_DIR)
		set(VCPKG_MANIFEST_DIR "${CONSUMER_ARCH_BVN_ROOT}" CACHE PATH "vcpkg manifest directory" FORCE)
	endif()

	project(bvn_consumer_arch_demo LANGUAGES CXX)

	set(CMAKE_CXX_STANDARD 23)
	set(CMAKE_CXX_STANDARD_REQUIRED ON)
	set(CMAKE_CXX_EXTENSIONS OFF)

	add_library(consumer_arch_build_settings INTERFACE)
	target_compile_features(consumer_arch_build_settings INTERFACE cxx_std_23)

	if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND MSVC)
		target_compile_options(consumer_arch_build_settings INTERFACE
			/W4
			/permissive-
			/Zc:__cplusplus
			/utf-8
			/wd4100
			-clang:-std=c++2c
		)
	elseif(MSVC)
		target_compile_options(consumer_arch_build_settings INTERFACE
			/W4
			/permissive-
			/Zc:__cplusplus
			/Zc:preprocessor
			/utf-8
			/wd4100
			/std:c++latest
		)
	else()
		target_compile_options(consumer_arch_build_settings INTERFACE
			-Wall
			-Wextra
			-Wpedantic
		)
	endif()

	file(TO_CMAKE_PATH "$ENV{NAGISA_ROOT}" CONSUMER_ARCH_NAGISA_ROOT_DEFAULT)
	set(CONSUMER_ARCH_NAGISA_ROOT "${CONSUMER_ARCH_NAGISA_ROOT_DEFAULT}" CACHE PATH "Nagisa source root")
	if(NOT EXISTS "${CONSUMER_ARCH_NAGISA_ROOT}/include/nagisa/concurrency")
		message(FATAL_ERROR
			"CONSUMER_ARCH_NAGISA_ROOT must contain include/nagisa/concurrency: "
			"${CONSUMER_ARCH_NAGISA_ROOT}"
		)
	endif()

	file(TO_CMAKE_PATH "$ENV{NAGISA_LIBRARY_ROOT}/stdexec" CONSUMER_ARCH_STDEXEC_ROOT_DEFAULT)
	set(CONSUMER_ARCH_STDEXEC_ROOT "${CONSUMER_ARCH_STDEXEC_ROOT_DEFAULT}" CACHE PATH "stdexec source root")
	if(NOT EXISTS "${CONSUMER_ARCH_STDEXEC_ROOT}/include/stdexec/execution.hpp")
		message(FATAL_ERROR
			"CONSUMER_ARCH_STDEXEC_ROOT must contain include/stdexec/execution.hpp: "
			"${CONSUMER_ARCH_STDEXEC_ROOT}"
		)
	endif()

	add_library(consumer_arch_nagisa_headers INTERFACE)
	target_include_directories(consumer_arch_nagisa_headers SYSTEM INTERFACE
		"${CONSUMER_ARCH_NAGISA_ROOT}/include"
	)

	add_library(consumer_arch_stdexec_headers INTERFACE)
	target_include_directories(consumer_arch_stdexec_headers SYSTEM INTERFACE
		"${CONSUMER_ARCH_STDEXEC_ROOT}/include"
	)

	find_package(SDL3 CONFIG REQUIRED)
	set(CONSUMER_ARCH_PLATFORM_SOURCES
		"${CONSUMER_ARCH_BVN_ROOT}/source/platform/event.cpp"
		"${CONSUMER_ARCH_BVN_ROOT}/source/platform/sdl_context.cpp"
		"${CONSUMER_ARCH_BVN_ROOT}/source/platform/window.cpp"
	)
	set(CONSUMER_ARCH_PROJECT_LIBRARIES
		consumer_arch_build_settings
		consumer_arch_nagisa_headers
		consumer_arch_stdexec_headers
		SDL3::SDL3
	)
else()
	set(CONSUMER_ARCH_PLATFORM_SOURCES)
	set(CONSUMER_ARCH_PROJECT_LIBRARIES
		bvn_build_settings
		bvn_nagisa_headers
		STDEXEC::stdexec
		bvn_platform
	)
endif()

find_package(Threads REQUIRED)
find_package(Vulkan REQUIRED)
find_package(glslang CONFIG REQUIRED)

if(NOT TARGET vkfu::vkfu)
	set(VKFU_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
	set(VKFU_BUILD_TESTS OFF CACHE BOOL "" FORCE)
	add_subdirectory("${CONSUMER_ARCH_BVN_ROOT}/vkfu" "${CMAKE_CURRENT_BINARY_DIR}/vkfu" EXCLUDE_FROM_ALL)
endif()

function(consumer_arch_compile_shader OUT_VAR SHADER_SOURCE)
	get_filename_component(CONSUMER_ARCH_SHADER_NAME "${SHADER_SOURCE}" NAME)
	set(CONSUMER_ARCH_SHADER_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/${CONSUMER_ARCH_SHADER_NAME}.spv")

	add_custom_command(
		OUTPUT "${CONSUMER_ARCH_SHADER_OUTPUT}"
		COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
		COMMAND glslang::glslang-standalone -V "${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_SOURCE}" -o "${CONSUMER_ARCH_SHADER_OUTPUT}"
		DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_SOURCE}"
		VERBATIM
	)

	set(${OUT_VAR} "${CONSUMER_ARCH_SHADER_OUTPUT}" PARENT_SCOPE)
endfunction()

consumer_arch_compile_shader(CONSUMER_ARCH_VERT_SPV "shaders/triangle.vert")
consumer_arch_compile_shader(CONSUMER_ARCH_FRAG_SPV "shaders/triangle.frag")

add_custom_target(bvn_consumer_arch_shaders DEPENDS
	"${CONSUMER_ARCH_VERT_SPV}"
	"${CONSUMER_ARCH_FRAG_SPV}"
)

add_executable(bvn_consumer_arch_demo
	demo.cpp
	consumer_task.h
	job.h
	../task-graph/node_sender.h
	../task-graph/dynamic_when_all.h
	${CONSUMER_ARCH_PLATFORM_SOURCES}
)
add_dependencies(bvn_consumer_arch_demo bvn_consumer_arch_shaders)
target_include_directories(bvn_consumer_arch_demo PRIVATE
	"${CONSUMER_ARCH_BVN_ROOT}/include"
)
target_link_libraries(bvn_consumer_arch_demo PRIVATE
	${CONSUMER_ARCH_PROJECT_LIBRARIES}
	Threads::Threads
	Vulkan::Vulkan
	vkfu::vkfu
)
target_compile_definitions(bvn_consumer_arch_demo PRIVATE
	BVN_CONSUMER_ARCH_VERT_SPV="${CONSUMER_ARCH_VERT_SPV}"
	BVN_CONSUMER_ARCH_FRAG_SPV="${CONSUMER_ARCH_FRAG_SPV}"
)

if(WIN32)
	add_custom_command(TARGET bvn_consumer_arch_demo POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			$<TARGET_RUNTIME_DLLS:bvn_consumer_arch_demo>
			$<TARGET_FILE_DIR:bvn_consumer_arch_demo>
		COMMAND_EXPAND_LISTS
	)
endif()

set_target_properties(bvn_consumer_arch_demo PROPERTIES
	FOLDER demo
	OUTPUT_NAME consumer-arch
)

# ---------------------------------------------------------------------------
# entity_arch.cpp：同一个帧流程，改写在 job-arch 的 entity / task 两层结构上，
# Vulkan 那层换成了内置的 mock。只要 stdexec + EnTT，不碰 Vulkan / SDL / vkfu / nagisa。
#
# EnTT 找不到就**跳过**这个目标，不影响上面那个真正的 Vulkan demo。
find_package(EnTT CONFIG QUIET)
if(TARGET EnTT::EnTT)
	set(CONSUMER_ARCH_ENTT_LIBRARY EnTT::EnTT)
else()
	file(TO_CMAKE_PATH "$ENV{NAGISA_LIBRARY_ROOT}/entt" CONSUMER_ARCH_ENTT_ROOT_DEFAULT)
	set(CONSUMER_ARCH_ENTT_ROOT "${CONSUMER_ARCH_ENTT_ROOT_DEFAULT}" CACHE PATH "EnTT source root")
	if(EXISTS "${CONSUMER_ARCH_ENTT_ROOT}/src/entt/entt.hpp")
		add_library(consumer_arch_entt_headers INTERFACE)
		target_include_directories(consumer_arch_entt_headers SYSTEM INTERFACE
			"${CONSUMER_ARCH_ENTT_ROOT}/src"
		)
		set(CONSUMER_ARCH_ENTT_LIBRARY consumer_arch_entt_headers)
	endif()
endif()

if(CONSUMER_ARCH_ENTT_LIBRARY)
	add_executable(bvn_consumer_arch_entity_demo
		entity_arch.cpp
		../job-arch/dynamic_when_all.h
		../job-arch/entities.h
		../job-arch/manual_lifetime.h
		../job-arch/node_sender.h
	)
	target_link_libraries(bvn_consumer_arch_entity_demo PRIVATE
		${CONSUMER_ARCH_PROJECT_LIBRARIES}
		${CONSUMER_ARCH_ENTT_LIBRARY}
		Threads::Threads
	)
	set_target_properties(bvn_consumer_arch_entity_demo PROPERTIES
		FOLDER demo
		OUTPUT_NAME consumer-arch-entity
		CXX_SCAN_FOR_MODULES OFF
	)
else()
	message(STATUS
		"consumer-arch: 找不到 EnTT，跳过 bvn_consumer_arch_entity_demo"
		"（设 CONSUMER_ARCH_ENTT_ROOT 指向 EnTT 源码根即可启用）"
	)
endif()
