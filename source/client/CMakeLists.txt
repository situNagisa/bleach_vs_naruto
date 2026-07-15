find_package(glslang CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(EnTT CONFIG REQUIRED)

function(bvn_client_compile_shader OUT_VAR SHADER_SOURCE)
	get_filename_component(BVN_SHADER_NAME "${SHADER_SOURCE}" NAME)
	set(BVN_SHADER_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/shaders/${BVN_SHADER_NAME}.spv")

	add_custom_command(
		OUTPUT "${BVN_SHADER_OUTPUT}"
		COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
		COMMAND glslang::glslang-standalone -V "${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_SOURCE}" -o "${BVN_SHADER_OUTPUT}"
		DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${SHADER_SOURCE}"
		VERBATIM
	)

	set(${OUT_VAR} "${BVN_SHADER_OUTPUT}" PARENT_SCOPE)
endfunction()

bvn_client_compile_shader(BVN_GRID_VERT_SPV "shaders/grid.vert")
bvn_client_compile_shader(BVN_GRID_FRAG_SPV "shaders/grid.frag")
bvn_client_compile_shader(BVN_SPRITE_VERT_SPV "shaders/sprite.vert")
bvn_client_compile_shader(BVN_SPRITE_FRAG_SPV "shaders/sprite.frag")

add_custom_target(bvn_client_shaders DEPENDS
	"${BVN_GRID_VERT_SPV}"
	"${BVN_GRID_FRAG_SPV}"
	"${BVN_SPRITE_VERT_SPV}"
	"${BVN_SPRITE_FRAG_SPV}"
)

bvn_add_executable_app(bvn_client client)
add_dependencies(bvn_client bvn_client_shaders)
target_link_libraries(bvn_client
	PRIVATE
		bvn_platform
		bvn_graphics
		bvn_renderer
		bvn_assets
		bvn_sim
		bvn_gameplay
		EnTT::EnTT
		glm::glm
		imgui::imgui
)
target_compile_definitions(bvn_client
	PRIVATE
		BVN_ASSET_ROOT="${PROJECT_SOURCE_DIR}/assets"
		BVN_GRID_VERT_SPV="${BVN_GRID_VERT_SPV}"
		BVN_GRID_FRAG_SPV="${BVN_GRID_FRAG_SPV}"
		BVN_SPRITE_VERT_SPV="${BVN_SPRITE_VERT_SPV}"
		BVN_SPRITE_FRAG_SPV="${BVN_SPRITE_FRAG_SPV}"
)

if(WIN32)
	add_custom_command(TARGET bvn_client POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			$<TARGET_RUNTIME_DLLS:bvn_client>
			$<TARGET_FILE_DIR:bvn_client>
		COMMAND_EXPAND_LISTS
	)
endif()
