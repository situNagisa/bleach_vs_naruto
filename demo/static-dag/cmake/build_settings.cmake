# 公共编译设置目标 sdag_build_settings：C++26 + 静态反射 + 警告级别。
function(sdag_add_build_settings TARGET)
    add_library(${TARGET} INTERFACE)
    target_compile_features(${TARGET} INTERFACE cxx_std_26)
    target_compile_options(${TARGET} INTERFACE
        -freflection
        -Wall
        -Wextra
        -Wpedantic
    )
endfunction()
