# 公共编译设置目标 exec0_build_settings。
# 所有参与模块编译的目标（包括 std 模块本身）都必须链接它：BMI 要求导出方与导入方的
# 语言选项一致（标准、标准库、异常模型……）。
#
# 产出变量：BVN_EXEC0_STDLIB_FLAGS —— 查询 libc++ 模块清单时也要带上的标准库选项（clang-cl 为空）。
function(bvn_exec0_add_build_settings TARGET)
    add_library(${TARGET} INTERFACE)
    target_compile_features(${TARGET} INTERFACE cxx_std_26)

    if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        # clang-cl：/W4 /permissive- 等与仓库根 CMake 一致；/EHsc 必须显式给（clang-cl 默认关异常）。
        target_compile_options(${TARGET} INTERFACE
            /W4
            /permissive-
            /Zc:__cplusplus
            /utf-8
            /EHsc
            /bigobj
            /wd4100
        )
        # Release 也要 PDB；Debug / RelWithDebInfo 的 /DEBUG CMake 已经会加，重复无害。
        target_link_options(${TARGET} INTERFACE /DEBUG)
        set(BVN_EXEC0_STDLIB_FLAGS "" PARENT_SCOPE)
    else()
        # clang++ + libc++
        set(_stdlib_flags -stdlib=libc++)
        target_compile_options(${TARGET} INTERFACE
            -Wall
            -Wextra
            -Wpedantic
            ${_stdlib_flags}
        )
        target_link_options(${TARGET} INTERFACE ${_stdlib_flags})
        set(BVN_EXEC0_STDLIB_FLAGS ${_stdlib_flags} PARENT_SCOPE)
    endif()
endfunction()
