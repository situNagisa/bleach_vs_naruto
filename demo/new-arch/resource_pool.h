#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <mutex>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include <stdexec/execution.hpp>

#include <nagisa/concurrency/lease.h>

template<class T>
using resource_pool = ::nagisa::concurrency::bounded_lease_pool<T*, 4>;
