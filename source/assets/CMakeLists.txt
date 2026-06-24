find_package(Stb REQUIRED)

bvn_add_static_module(bvn_assets assets)
target_include_directories(bvn_assets
	PRIVATE
		${Stb_INCLUDE_DIR}
)
target_compile_definitions(bvn_assets PRIVATE BVN_ASSETS_BUILD)
