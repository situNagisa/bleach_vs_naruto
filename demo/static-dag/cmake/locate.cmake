# sdag_locate(<OUT_VAR> ENV <环境变量> [SUBDIR <子目录>] PROBE <相对路径> DOC <说明>)
#
# 以"环境变量 -> 缓存变量 -> 探测文件"三步定位一份放在仓库外的依赖：
#   1. 默认值取 $ENV{<环境变量>}[/<子目录>]；
#   2. 写入缓存变量 <OUT_VAR>，用户可用 -D<OUT_VAR>=... 覆盖；
#   3. 若 <OUT_VAR>/<PROBE> 存在则 <OUT_VAR>_FOUND 为 TRUE，否则为 FALSE，由调用方决定报错还是退路。
function(sdag_locate OUT_VAR)
    cmake_parse_arguments(ARG "" "ENV;SUBDIR;PROBE;DOC" "" ${ARGN})
    if(NOT ARG_ENV OR NOT ARG_PROBE)
        message(FATAL_ERROR "sdag_locate(${OUT_VAR}) 需要 ENV 与 PROBE")
    endif()

    set(_default "")
    if(DEFINED ENV{${ARG_ENV}})
        file(TO_CMAKE_PATH "$ENV{${ARG_ENV}}" _default)
        if(ARG_SUBDIR)
            string(APPEND _default "/${ARG_SUBDIR}")
        endif()
    endif()
    set(${OUT_VAR} "${_default}" CACHE PATH "${ARG_DOC}")

    if(${OUT_VAR} AND EXISTS "${${OUT_VAR}}/${ARG_PROBE}")
        set(${OUT_VAR}_FOUND TRUE PARENT_SCOPE)
    else()
        set(${OUT_VAR}_FOUND FALSE PARENT_SCOPE)
    endif()
endfunction()
