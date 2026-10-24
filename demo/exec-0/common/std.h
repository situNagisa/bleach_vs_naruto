// 实现文件（.cpp）与 main.cpp 共用的标准库入口。
//   模块模式（定义 BVN_EXEC0_MODULES）：import std;
//   头文件模式：逐个包含实现文件实际用到的标准头。
// 必须放在所有第三方 #include 之后：在同一翻译单元里，标准头只能出现在 import std; 之前。
// 模块接口单元（.cppm）不包含本文件，它们自己 import std;。
#if defined(BVN_EXEC0_MODULES)

import std;

#else

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <future>
#include <ios>
#include <limits>
#include <memory>
#include <optional>
#include <print>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#endif
