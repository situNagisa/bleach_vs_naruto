find_package(glm CONFIG REQUIRED)

bvn_add_static_module(bvn_sim sim)
target_link_libraries(bvn_sim PUBLIC glm::glm)
target_compile_definitions(bvn_sim PRIVATE BVN_SIM_BUILD)
