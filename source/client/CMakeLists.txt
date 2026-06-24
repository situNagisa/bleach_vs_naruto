bvn_add_executable_app(bvn_client client)
target_link_libraries(bvn_client
	PRIVATE
		bvn_platform
		bvn_render
		bvn_sim
		bvn_compute
)

if(WIN32)
	add_custom_command(TARGET bvn_client POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			$<TARGET_RUNTIME_DLLS:bvn_client>
			$<TARGET_FILE_DIR:bvn_client>
		COMMAND_EXPAND_LISTS
	)
endif()
