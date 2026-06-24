find_package(imgui CONFIG REQUIRED)
find_package(glslang CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

function(bvn_compile_shader OUT_VAR SHADER_SOURCE)
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

bvn_compile_shader(BVN_GRID_VERT_SPV "shaders/grid.vert")
bvn_compile_shader(BVN_GRID_FRAG_SPV "shaders/grid.frag")
bvn_compile_shader(BVN_SPRITE_VERT_SPV "shaders/sprite.vert")
bvn_compile_shader(BVN_SPRITE_FRAG_SPV "shaders/sprite.frag")

add_custom_target(bvn_render_shaders DEPENDS
	"${BVN_GRID_VERT_SPV}"
	"${BVN_GRID_FRAG_SPV}"
	"${BVN_SPRITE_VERT_SPV}"
	"${BVN_SPRITE_FRAG_SPV}"
)

bvn_add_static_module(bvn_render render)
add_dependencies(bvn_render bvn_render_shaders)
target_link_libraries(bvn_render
	PUBLIC
		bvn_platform
		bvn_rhi
		bvn_assets
		imgui::imgui
		glm::glm
)
target_compile_definitions(bvn_render
	PRIVATE
		BVN_RENDER_BUILD
		BVN_GRID_VERT_SPV="${BVN_GRID_VERT_SPV}"
		BVN_GRID_FRAG_SPV="${BVN_GRID_FRAG_SPV}"
		BVN_SPRITE_VERT_SPV="${BVN_SPRITE_VERT_SPV}"
		BVN_SPRITE_FRAG_SPV="${BVN_SPRITE_FRAG_SPV}"
		BVN_ASSET_ROOT="${PROJECT_SOURCE_DIR}/assets"
)
