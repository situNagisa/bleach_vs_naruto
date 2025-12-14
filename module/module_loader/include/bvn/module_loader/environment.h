#pragma once

#include <string_view>
#include <cstddef>
#include <span>
#include <memory>
#include <unordered_map>
#include <vector>
#include <ranges>
#include <concepts>
#include <tuple>
#include <atomic>

#include <nagisa/symbol_loader/symbol_loader.h>
#include <nagisa/symbol_loader/loader/native.h>

#include "bvn/environment.h"

#define NAGISA_BUILD_LIB_NAME BVN_NS::module_loader
#define NAGISA_BUILD_LIB_CONFIG_VERSION (1,0,0)
#include <nagisa/build_lib/construct.h>