bvn_add_header_only_module(bvn_compute)
target_link_libraries(bvn_compute
	INTERFACE
		bvn_nagisa_headers
		STDEXEC::stdexec
)
