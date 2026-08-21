# vkfu reference implementation

`vkfu` is a C++20, header-only prototype of a typed Vulkan construction
expression system. It implements the device graph from the design discussion:

```text
device
|- feature2
|  |- timeline_semaphore
|  |- host_query_reset
|  `- cluster_culling
|     `- cluster_culling_vrs
`- allocator (create-call metadata, not a pNext node)
```

The explicit and concise forms normalize to the same AST type:

```cpp
auto explicit_expression =
    branch(feature2_param{})
    | feature(timeline_semaphore_param{true});

auto concise_expression =
    feature2_param{}
    | timeline_semaphore_param{true};

static_assert(std::same_as<
    decltype(explicit_expression), decltype(concise_expression)>);
```

## Implemented properties

- `root`, `branch`, and `feature` categories with a stable Vulkan object tag.
- Constrained `operator|`: an illegal direct attachment or a duplicate
  non-repeatable feature is rejected during overload resolution.
- Validation is local to each node. A nested branch is not flattened for the
  duplicate check.
- Positional recursive storage, so repeatable generated nodes do not depend on
  `std::get<T>`.
- Post-order evaluation and a deterministic, expression-order `pNext` chain.
- Custom copy/move construction and assignment rebuild internal `pNext`
  pointers after relocation.
- Input `span`s remain borrowed. Evaluation, copying, and moving never allocate,
  copy, or repair the memory viewed by a span.
- An evaluated lvalue child is borrowed by reference. An evaluated rvalue child
  is moved into the parent storage. Const evaluated lvalues are also borrowable;
  parent evaluation never mutates the borrowed chain.
- `evaluate(evaluated_lvalue)` is an identity operation.
- `create_device(expression)` and `create_device(evaluated)` share the same path
  and throw the returned `VkResult` on failure.
- Injectable `PFN_vkCreateDevice`, allowing examples/tests to run without
  creating a real Vulkan device.

## Borrowed evaluated branches

A Vulkan `pNext` graph is physically a singly linked list. A borrowed evaluated
subtree cannot have its tail changed without mutating the borrowed object and
breaking reuse by another parent. Therefore a borrowed subtree must be the last
`pNext`-participating child at its level. Non-chain metadata such as
`allocator_param` may still follow it. The `operator|` constraint enforces this.

The caller owns the lifetime and address stability of borrowed evaluated values,
just as the caller owns memory viewed by input spans.

An evaluated value remains an `expression` so that `evaluate(value)` is an
identity operation, it can be used as a child, and a root can be passed directly
to `create_device`. This implementation intentionally does not reopen an
evaluated value as the left side of `operator|`; attach further features before
evaluating that node.

## Build

From the repository root, using the checked-in vcpkg installation:

```powershell
cmake -S vkfu -B vkfu/out/build -G Ninja `
  -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_CXX_COMPILER=clang-cl `
  -DVulkan_INCLUDE_DIR=D:/project/bvn/vcpkg_installed/x64-windows/include `
  -DVulkan_LIBRARY=D:/project/bvn/vcpkg_installed/x64-windows/debug/lib/vulkan-1.lib
cmake --build vkfu/out/build
ctest --test-dir vkfu/out/build --output-on-failure
./vkfu/out/build/vkfu_device_example.exe
```

`parameter_traits`, `object_traits`, and `attachment_rule` are the intended
code-generation surface. The provided `device.hpp` is the generated-style
implementation for the example graph; it deliberately performs only static
structural validation, not extension-enable or physical-device-support checks.
The direct attachment rules mirror `vk.xml` `structextends` entries. For
example, timeline semaphore, host query reset, and cluster culling may extend
either `VkPhysicalDeviceFeatures2` or `VkDeviceCreateInfo`, while cluster
culling VRS extends the cluster-culling feature struct.
