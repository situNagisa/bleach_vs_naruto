function(bvn_collect_module_sources OUT_VAR MODULE)
	file(GLOB_RECURSE BVN_COLLECTED_SOURCES CONFIGURE_DEPENDS
		"${PROJECT_SOURCE_DIR}/source/${MODULE}/*.cc"
		"${PROJECT_SOURCE_DIR}/source/${MODULE}/*.cpp"
		"${PROJECT_SOURCE_DIR}/source/${MODULE}/*.cxx"
	)
	file(GLOB_RECURSE BVN_COLLECTED_HEADERS CONFIGURE_DEPENDS
		"${PROJECT_SOURCE_DIR}/include/bvn/${MODULE}/*.h"
		"${PROJECT_SOURCE_DIR}/include/bvn/${MODULE}/*.hpp"
	)

	set(${OUT_VAR} ${BVN_COLLECTED_SOURCES} ${BVN_COLLECTED_HEADERS} PARENT_SCOPE)
	set(${OUT_VAR}_COMPILE ${BVN_COLLECTED_SOURCES} PARENT_SCOPE)
	set(${OUT_VAR}_HEADERS ${BVN_COLLECTED_HEADERS} PARENT_SCOPE)
endfunction()

function(bvn_add_static_module TARGET MODULE)
	bvn_collect_module_sources(BVN_TARGET "${MODULE}")

	if(NOT BVN_TARGET_COMPILE)
		message(FATAL_ERROR "${TARGET} has no source files. Use bvn_add_header_only_module for header-only modules.")
	endif()

	add_library(${TARGET} STATIC ${BVN_TARGET})
	target_link_libraries(${TARGET} PUBLIC bvn_build_settings)
	target_include_directories(${TARGET}
		PUBLIC
			"$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
	)
	set_target_properties(${TARGET} PROPERTIES FOLDER "engine")

	if(BVN_TARGET_COMPILE)
		source_group(TREE "${PROJECT_SOURCE_DIR}/source/${MODULE}" PREFIX "source" FILES ${BVN_TARGET_COMPILE})
	endif()

	if(BVN_TARGET_HEADERS)
		source_group(TREE "${PROJECT_SOURCE_DIR}/include/bvn/${MODULE}" PREFIX "include" FILES ${BVN_TARGET_HEADERS})
	endif()
endfunction()

function(bvn_add_header_only_module TARGET)
	add_library(${TARGET} INTERFACE)
	target_link_libraries(${TARGET} INTERFACE bvn_build_settings)
	target_include_directories(${TARGET}
		INTERFACE
			"$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
	)
	set_target_properties(${TARGET} PROPERTIES FOLDER "engine")
endfunction()

function(bvn_add_executable_app TARGET MODULE)
	bvn_collect_module_sources(BVN_TARGET "${MODULE}")

	if(NOT BVN_TARGET_COMPILE)
		message(FATAL_ERROR "${TARGET} has no source files.")
	endif()

	add_executable(${TARGET} ${BVN_TARGET})
	target_link_libraries(${TARGET} PRIVATE bvn_build_settings)
	target_include_directories(${TARGET}
		PRIVATE
			"$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
	)
	set_target_properties(${TARGET} PROPERTIES FOLDER "apps")

	if(BVN_TARGET_COMPILE)
		source_group(TREE "${PROJECT_SOURCE_DIR}/source/${MODULE}" PREFIX "source" FILES ${BVN_TARGET_COMPILE})
	endif()

	if(BVN_TARGET_HEADERS)
		source_group(TREE "${PROJECT_SOURCE_DIR}/include/bvn/${MODULE}" PREFIX "include" FILES ${BVN_TARGET_HEADERS})
	endif()
endfunction()