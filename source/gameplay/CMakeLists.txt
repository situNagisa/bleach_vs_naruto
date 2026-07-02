bvn_add_header_only_module(bvn_gameplay)
target_link_libraries(bvn_gameplay
	INTERFACE
		bvn_nagisa_headers
		STDEXEC::stdexec
)
