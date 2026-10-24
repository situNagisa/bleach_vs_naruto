# exec0_demo 的源文件清单与两种构建方式（README §3）。
#
#   BVN_EXEC0_USE_MODULES=ON（默认）：.cppm 作为 CXX_MODULES 文件集参与编译，开启模块扫描，
#       给 exec0_demo 定义 BVN_EXEC0_MODULES —— 各 .h 在 .cpp 里展开成 import；
#   BVN_EXEC0_USE_MODULES=OFF：纯头文件构建，不建 exec0_std、不扫描模块，.cppm 不参与编译，
#       .h 展开成声明。与 VS 的 DebugHeaders / ReleaseHeaders 配置是同一种代码形态，可在 Linux 上验证。
#
# 新增模块时：在 BVN_EXEC0_SOURCES 加 .cpp、BVN_EXEC0_MODULE_INTERFACES 加 .cppm、BVN_EXEC0_HEADERS 加 .h。

set(BVN_EXEC0_SOURCES
    main.cpp
    window/window.cpp
    input/input.cpp
    render/renderer.cpp
    renderable/buffer_type.cpp
    renderable/image.cpp
    renderable/font.cpp
    renderable/sprite.cpp
    renderable/vector_graph.cpp
    scene/menu/menu.cpp
)

# 声明的唯一来源；两种模式都通过它们拿到声明（模块模式经由 .cppm 包含）。
set(BVN_EXEC0_HEADERS
    common/std.h
    execution/execution.h
    window/window.h
    input/input.h
    render/renderer.h
    renderable/buffer_type.h
    renderable/image.h
    renderable/font.h
    renderable/sprite.h
    renderable/vector_graph.h
    scene/menu/menu.h
)

# 模块依赖：std <- demo.execution <- demo.input  <- main
#           std <- demo.window    <- demo.input
#                  demo.window    <- demo.render <- main
#                  demo.execution <- demo.render
set(BVN_EXEC0_MODULE_INTERFACES
    execution/execution.cppm
    window/window.cppm
    input/input.cppm
    render/renderer.cppm
    renderable/buffer_type.cppm
    renderable/image.cppm
    renderable/font.cppm
    renderable/sprite.cppm
    renderable/vector_graph.cppm
    scene/menu/menu.cppm
)

# bvn_exec0_add_demo_sources(<目标>)：按 BVN_EXEC0_USE_MODULES 把上面三组文件挂到目标上。
# 模块模式下目标还需要链接 exec0_std（由 CMakeLists.txt 负责，因为 exec0_std 只在该模式下存在）。
function(bvn_exec0_add_demo_sources TARGET)
    target_sources(${TARGET} PRIVATE ${BVN_EXEC0_SOURCES})
    # 头文件只为 IDE 浏览；不编译。
    target_sources(${TARGET} PRIVATE FILE_SET HEADERS BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${BVN_EXEC0_HEADERS})
    if(BVN_EXEC0_USE_MODULES)
        target_sources(${TARGET} PRIVATE FILE_SET CXX_MODULES FILES ${BVN_EXEC0_MODULE_INTERFACES})
        target_compile_definitions(${TARGET} PRIVATE BVN_EXEC0_MODULES)
    else()
        set_target_properties(${TARGET} PROPERTIES CXX_SCAN_FOR_MODULES OFF)
    endif()
endfunction()
