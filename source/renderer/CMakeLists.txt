find_package(glm CONFIG REQUIRED)

bvn_add_header_only_module(bvn_renderer)
target_link_libraries(bvn_renderer
	INTERFACE
		glm::glm
)
