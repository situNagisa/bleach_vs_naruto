# 把标准库自带的 std 模块源文件（libc++ 的 std.cppm、MSVC STL 的 std.ixx）编译成一个
# 普通的具名模块库目标，供 `import std;` 使用。
#
# 为什么不用 CMake 自带的 CMAKE_CXX_MODULE_STD：
#   1. 它仍是实验特性，开关 UUID（CMAKE_EXPERIMENTAL_CXX_IMPORT_STD）每个 CMake 版本都变；
#   2. 它只认 clang + libc++/libstdc++ 与 MSVC cl，不支持 clang-cl + MSVC STL。
# 这里把 std 当成一个普通模块来编译，对 CMake 而言没有任何特殊之处，所以跨 CMake 版本稳定。
#
# 覆盖方式：-DBVN_EXEC0_STD_MODULE_SOURCE=<std.cppm 或 std.ixx 的绝对路径>

function(bvn_exec0_add_std_module TARGET)
    set(_source "${BVN_EXEC0_STD_MODULE_SOURCE}")
    set(_system_includes)

    if(NOT _source)
        if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
            # clang-cl 配 MSVC STL。VS 开发者环境（vcvars64 / VS“打开文件夹”）会导出 VCToolsInstallDir。
            if(DEFINED ENV{VCToolsInstallDir})
                file(TO_CMAKE_PATH "$ENV{VCToolsInstallDir}" _vc_tools)
                set(_source "${_vc_tools}/modules/std.ixx")
            elseif(DEFINED ENV{INCLUDE})
                # 退路：从 INCLUDE 里找 <VC>/Tools/MSVC/<ver>/include，旁边就是 modules/std.ixx。
                file(TO_CMAKE_PATH "$ENV{INCLUDE}" _include_dirs)
                foreach(_dir IN LISTS _include_dirs)
                    if(EXISTS "${_dir}/../modules/std.ixx")
                        get_filename_component(_source "${_dir}/../modules/std.ixx" ABSOLUTE)
                        break()
                    endif()
                endforeach()
            endif()
        else()
            # clang + libc++：问编译器要 libc++.modules.json，再按清单解析 std 的源文件。
            execute_process(
                COMMAND "${CMAKE_CXX_COMPILER}" ${BVN_EXEC0_STDLIB_FLAGS} -print-file-name=libc++.modules.json
                OUTPUT_VARIABLE _manifest
                OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE _result
            )
            if(_result EQUAL 0 AND IS_ABSOLUTE "${_manifest}" AND EXISTS "${_manifest}")
                file(READ "${_manifest}" _content)
                get_filename_component(_manifest_dir "${_manifest}" DIRECTORY)
                string(JSON _count LENGTH "${_content}" modules)
                math(EXPR _last "${_count} - 1")
                foreach(_index RANGE ${_last})
                    string(JSON _name GET "${_content}" modules ${_index} logical-name)
                    if(NOT _name STREQUAL "std")
                        continue()
                    endif()
                    string(JSON _relative GET "${_content}" modules ${_index} source-path)
                    get_filename_component(_source "${_manifest_dir}/${_relative}" ABSOLUTE)
                    string(JSON _include_count ERROR_VARIABLE _error
                        LENGTH "${_content}" modules ${_index} local-arguments system-include-directories)
                    if(NOT _error AND _include_count GREATER 0)
                        math(EXPR _include_last "${_include_count} - 1")
                        foreach(_include_index RANGE ${_include_last})
                            string(JSON _include GET "${_content}" modules ${_index} local-arguments system-include-directories ${_include_index})
                            get_filename_component(_include "${_manifest_dir}/${_include}" ABSOLUTE)
                            list(APPEND _system_includes "${_include}")
                        endforeach()
                    endif()
                    break()
                endforeach()
            endif()
        endif()
    endif()

    if(NOT _source OR NOT EXISTS "${_source}")
        message(FATAL_ERROR
            "找不到标准库的 std 模块源文件（std.cppm / std.ixx）。\n"
            "  clang-cl：请在 VS 开发者命令提示符（或 VS“打开文件夹”）里配置，确保 VCToolsInstallDir 已设置；\n"
            "  clang   ：请安装带模块清单的 libc++（`clang++ -print-file-name=libc++.modules.json` 应返回绝对路径）；\n"
            "  或者直接传 -DBVN_EXEC0_STD_MODULE_SOURCE=<路径>。\n"
            "  当前探测结果：'${_source}'"
        )
    endif()

    set(BVN_EXEC0_STD_MODULE_SOURCE "${_source}" CACHE FILEPATH "std 模块的源文件（std.cppm / std.ixx）" FORCE)
    message(STATUS "exec-0: std 模块源文件 = ${_source}")

    get_filename_component(_source_dir "${_source}" DIRECTORY)
    add_library(${TARGET} STATIC)
    target_sources(${TARGET}
        PUBLIC FILE_SET CXX_MODULES
            BASE_DIRS "${_source_dir}"
            FILES "${_source}"
    )
    if(_system_includes)
        target_include_directories(${TARGET} SYSTEM PRIVATE ${_system_includes})
    endif()
    # std 是保留的模块名；MSVC STL 的 std.ixx 还会在模块 purview 里 #include <...>。
    target_compile_options(${TARGET} PRIVATE
        -Wno-reserved-module-identifier
        -Wno-include-angled-in-module-purview
    )
endfunction()
