# MSVC coroutine awaitable ICE repro

This folder contains reduced repros for MSVC 19.51.36246.0 internal compiler
errors originally seen while compiling Nagisa coroutine awaitable traits with
stdexec.

The two `standalone_*` files do not include Nagisa or stdexec. They only use the
C++ standard library.

Run from a Visual Studio Developer Command Prompt:

```bat
build.bat
```

The script first builds the standalone repros. It also builds the original
Nagisa/stdexec-shaped repros and assumes:

```bat
set STDEXEC_INCLUDE=D:\project\stdexec\include
set NAGISA_INCLUDE=D:\project\nagisa\include
```

Adjust those paths in `build.bat` if the dependencies are elsewhere.

## Standalone Repro 1: run_this-shaped await_suspend

File:

```text
standalone_run_this_ice.cpp
```

Minimal trigger:

```bat
cl /nologo /c /std:c++latest /permissive- /Zc:__cplusplus /Zc:preprocessor /EHsc /utf-8 /W4 standalone_run_this_ice.cpp
```

Observed with MSVC 19.51.36246.0:

```text
standalone_run_this_ice.cpp(13): fatal error C1001
```

The standalone shape is:

```cpp
template <class Promise, class Parent>
struct run_this
{
    constexpr static decltype(auto) await_suspend(
        std::coroutine_handle<Promise> self,
        std::coroutine_handle<Parent> parent) noexcept
    {
        return self;
    }
};
```

It is instantiated through a wrapper callable and `std::invocable`, matching the
shape that stdexec uses when it asks whether a task is awaitable.

## Standalone Repro 2: capture_stop_token-shaped create_context

File:

```text
standalone_capture_stop_token_ice.cpp
```

Minimal trigger:

```bat
cl /nologo /c /std:c++latest /permissive- /Zc:__cplusplus /Zc:preprocessor /EHsc /utf-8 /W4 standalone_capture_stop_token_ice.cpp
```

Observed with MSVC 19.51.36246.0:

```text
standalone_capture_stop_token_ice.cpp(29): fatal error C1001
```

The standalone shape is:

```cpp
template <class Source, class Promise, class Parent>
struct capture_stop_token
{
    using handle_type = std::coroutine_handle<Promise>;
    using context_type = connect_stop_token<Source, callback>;

    constexpr static auto create_context(handle_type self, Parent& parent) noexcept
    {
        return context_type{};
    }
};
```

It is also instantiated through a wrapper callable and `std::invocable`.

Both standalone repros:

- ICE with `/W4` or `/Wall`.
- Compile with `/W3`.
- Compile with `/W4 /wd4100`.

So the common reduced trigger is: an unused function parameter warning (`C4100`)
is emitted while MSVC is instantiating a constrained wrapper callable through
`std::invocable`.

## Nagisa/stdexec Repro 1: run_this

File:

```text
run_this_sender_ice.cpp
```

Minimal trigger:

```bat
cl /nologo /c /std:c++latest /permissive- /Zc:__cplusplus /Zc:preprocessor /EHsc /utf-8 /W4 ^
  /I D:\project\stdexec\include /I D:\project\nagisa\include ^
  run_this_sender_ice.cpp
```

Observed with MSVC 19.51.36246.0:

```text
nagisa\concurrency\_coroutine\component\workflow.h(34): fatal error C1001
```

This is triggered while evaluating `stdexec::sender<task>`. The relevant
awaitable trait is:

```cpp
template<class Promise, class ParentPromise>
struct run_this
{
    constexpr static decltype(auto) await_suspend(
        std::coroutine_handle<Promise> self,
        std::coroutine_handle<ParentPromise> parent) noexcept
    {
        return self;
    }
};
```

Notes:

- `/W4` or `/Wall` triggers the ICE.
- `/W3` compiles.
- `/W4 /wd4100` compiles.
- Replacing `decltype(auto)` with an explicit `std::coroutine_handle<Promise>`
  return type and omitting/naming away the unused parent parameter also compiles.

## Nagisa/stdexec Repro 2: capture_inplace_stop_token

File:

```text
capture_inplace_stop_token_sender_ice.cpp
```

Minimal trigger:

```bat
cl /nologo /c /std:c++latest /permissive- /Zc:__cplusplus /Zc:preprocessor /EHsc /utf-8 /W4 ^
  /I D:\project\stdexec\include /I D:\project\nagisa\include ^
  capture_inplace_stop_token_sender_ice.cpp
```

Observed with MSVC 19.51.36246.0:

```text
nagisa\concurrency\_coroutine\component\stop_token.h(159): fatal error C1001
```

This repro intentionally uses a local `resume_this` trait with an explicit
`std::coroutine_handle<Promise>` return type, so it does not depend on the
`run_this` ICE above.

Notes:

- `/W4` or `/Wall` triggers the ICE.
- `/W4 /wd4100` compiles.
- The crash happens while instantiating the indirect stop-token
  `create_context` path:

```cpp
constexpr static auto create_context(handle_type self, ParentPromise& parent) noexcept
{
    return context_type{};
}
```

## Smaller stop-token investigation case

File:

```text
capture_inplace_stop_token_min_ice.cpp
```

This keeps only `capture_inplace_stop_token` in the awaitable trait list. With
`/W4` it still reaches the same `stop_token.h(159)` C1001. With `/W3` or
`/W4 /wd4100`, it does not ICE, but the final `static_assert(stdexec::sender<task>)`
fails normally because this stripped task no longer has a complete awaiter.

That makes `capture_inplace_stop_token_sender_ice.cpp` the better file to attach
as the valid-code repro, and `capture_inplace_stop_token_min_ice.cpp` useful for
explaining the smaller crash trigger.
