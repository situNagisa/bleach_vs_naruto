#pragma once

/// 编译开关。
///
/// SDAG_REFLECTION：编译期计算用 C++26 静态反射（P2996 + 展开语句）还是传统模板元编程。
///   - 默认自动探测：编译器打开了反射（g++ 16 的 -freflection）并且有 <meta> 时为 1，否则为 0。
///   - 也可以在命令行上强制指定 -DSDAG_REFLECTION=0 / 1。
/// 有两份编译期计算的文件（grep -rln "#if SDAG_REFLECTION" include/sdag），每个文件里两种实现用 #if SDAG_REFLECTION 隔开，
/// 对外名字相同；其余代码只有一份。同一编译器下两种模式生成的机器码相同。
///
/// 等所用的编译器都支持反射之后，删除模板版：
///   1. 删掉这些文件里的 #else 分支；
///   2. 下面"SDAG_REFLECTION 为 0"的分支改成 #error；
///   3. CMake 与预设里的 SDAG_REFLECTION 开关一并去掉（见 README）。

#if !defined(SDAG_REFLECTION)
#if defined(__cpp_impl_reflection) && __has_include(<meta>)
#define SDAG_REFLECTION 1
#else
#define SDAG_REFLECTION 0
#endif
#endif

#if SDAG_REFLECTION && !defined(__cpp_impl_reflection)
#error "SDAG_REFLECTION=1 需要编译器支持 C++26 静态反射（例如 g++ 16 加 -freflection）"
#endif

/// 两种模式生成的实体放进不同的 inline namespace：同一个程序里混用两种模式编译的翻译单元时，
/// 链接期就会报"找不到符号"，而不是静默的 ODR 违规。
#if SDAG_REFLECTION
#define SDAG_ABI_NAMESPACE reflection_abi
#else
#define SDAG_ABI_NAMESPACE templates_abi
#endif

/// MSVC ABI（clang-cl）下 [[no_unique_address]] 被忽略，要用 [[msvc::no_unique_address]]；
/// 多个空基类也只有第一个能做空基类优化，要加 __declspec(empty_bases)。
#if defined(_MSC_VER)
#define SDAG_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#define SDAG_EMPTY_BASES __declspec(empty_bases)
#else
#define SDAG_NO_UNIQUE_ADDRESS [[no_unique_address]]
#define SDAG_EMPTY_BASES
#endif

/// 容器里非空元素要不要也加 no_unique_address（让后面的元素复用它的尾部填充）。
/// GCC 不对"可能重叠的子对象"做复制消除（CWG2403）：这样的成员从纯右值初始化时要求移动构造，
/// 而 operation state 不可移动，所以 GCC 上不加；clang 照样消除，加上能省下尾部填充。
#if defined(__clang__)
#define SDAG_OVERLAPPING_ELEMENT SDAG_NO_UNIQUE_ADDRESS
#else
#define SDAG_OVERLAPPING_ELEMENT
#endif
