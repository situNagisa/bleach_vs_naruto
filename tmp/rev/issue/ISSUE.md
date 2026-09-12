# `when_all`: `__forward_stop_request` resurrects `__count_` after the barrier is released, running `__complete()` on two threads

## Summary

When an external `request_stop()` races with the final child arrival, `when_all`'s stop callback can take `__count_` from `0` back to `1` and then back to `0`, so it observes "I am the last arrival" a second time. Two threads then run `__state::__complete()` concurrently, which

* destroys the same `__optional<__stop_callback_t>` twice, and
* completes the same receiver twice.

Reproduces on `main` (`2c56ffe7f8a2b8b5221918159092be379ae8b40f`, 2026-09-04) with both clang 22.1.8 and gcc 16.1.1, usually on the **first** iteration of the reproducer below. In practice it shows up as a livelock inside `~inplace_stop_callback`.

## The race

`__when_all.hpp` (line numbers from `2c56ffe7`):

```cpp
// 604
constexpr void __arrive() noexcept
{
  if (1 == __count_.fetch_sub(1, __std::memory_order_acq_rel))
  {
    __complete();
  }
}

// 612
constexpr void __complete() noexcept
{
  // Stop callback is no longer needed. Destroy it.
  if constexpr (_UsesStopSource)
  {
    __on_stop_.reset();          // <-- 617: unregisters
  }
  switch (__state_.load(__std::memory_order_relaxed)) { /* complete the receiver */ }
}

// 563
struct __forward_stop_request
{
  constexpr void operator()() const noexcept
  {
    // Temporarily increment the count to avoid concurrent/recursive arrivals to
    // pull the rug under our feet. Relaxed memory order is fine here.
    __state_->__count_.fetch_add(1, __std::memory_order_relaxed);
    ...
    __state_->__arrive();
  }
};
```

Interleaving:

```
thread A (last child arrives)            thread B (external request_stop)
──────────────────────────────────       ─────────────────────────────────────
__arrive: fetch_sub -> returns 1
          __count_ is now 0
"I am the last arrival"
                                         request_stop() takes the source lock,
                                         *unlinks* the callback, invokes it
                                         __forward_stop_request:
                                           fetch_add  -> __count_ 0 -> 1
                                           __arrive: fetch_sub -> returns 1
                                           "I am the last arrival" too
__complete:
  __on_stop_.reset()                       __complete:
    ~inplace_stop_callback                   __on_stop_.reset()   <-- same object
      waits for the running callback           ~inplace_stop_callback (again)
                                             complete the receiver     <-- 1st
  complete the receiver     <-- 2nd
```

The temporary increment is correct for the case it documents — `request_stop()` making children complete synchronously from inside the callback, which would otherwise let `__complete()` destroy the callback while it is still running. It does not cover the case where `__count_` had **already** reached zero before the callback was entered: there the `fetch_add` resurrects a released barrier and manufactures a second "last arrival".

`~inplace_stop_callback` only guarantees that destruction waits for an already-running callback; it cannot prevent one from being *started*. And since `request_stop()` unlinks the callback before invoking it, the thread that reaches `reset()` first simply waits for it — and then completes the receiver as well.

## Reproducer

`repro.cpp` (attached below). Build and run:

```
c++ -std=c++2b -O2 -I <stdexec>/include repro.cpp -o repro -lpthread
./repro
```

Note it uses **two** children. With a single child `__uses_stop_source` is `false`, the internal stop source is elided, the outer token is passed straight through to the child, and `__forward_stop_request` is never registered — so a one-child version reproduces nothing.

## Observed

```
$ clang++ -std=c++2b -O2 -isystem stdexec/include repro.cpp -o repro -lpthread && ./repro
DEADLOCK at round 1: two threads are both inside __complete(), destroying the same __on_stop_.

$ g++ -std=c++2b -O2 -isystem stdexec/include repro.cpp -o repro -lpthread && ./repro
DEADLOCK at round 1: two threads are both inside __complete(), destroying the same __on_stop_.
```

Expected: `200000 rounds, no double completion observed`.

The reproducer also reports `round N: receiver was completed 2 times (expected exactly 1)` when the two threads happen not to livelock first.

## Backtrace of the hang

Both threads reached `__complete()`. The one that got there second destroyed the callback from inside the callback itself; the first is now spinning in `__remove_callback_` waiting on an object that no longer exists:

```
Thread 3:
#3  stdexec::__stok::__spin_wait::__wait ()                      at stop_token.hpp:106
#4  stdexec::inplace_stop_source::__remove_callback_ (...)       at stop_token.hpp:418
#5  stdexec::inplace_stop_callback<
      stdexec::__when_all::__forward_stop_request<...>
    >::~inplace_stop_callback ()                                 at stop_token.hpp:244
#6  std::destroy_at<stdexec::inplace_stop_callback<...>> (...)
#7  stdexec::__manual_lifetime<...>::__destroy ()                at __manual_lifetime.hpp:82
#8  stdexec::__opt::__optional<...>::reset ()                    at __optional.hpp:206
#9  stdexec::__when_all::__state<...>::__complete ()             at __when_all.hpp:617
```

## Candidate fix

Only take the temporary reference when the barrier has not already been released. While the callback runs, the thread that released the barrier is blocked in `~inplace_stop_callback`, so the state is guaranteed to still be alive — reading `__count_` here is safe, and bailing out leaves the single legitimate completion to that thread.

```diff
--- a/include/stdexec/__detail/__when_all.hpp
+++ b/include/stdexec/__detail/__when_all.hpp
@@
       constexpr void operator()() const noexcept
       {
         // Temporarily increment the count to avoid concurrent/recursive arrivals to
-        // pull the rug under our feet. Relaxed memory order is fine here.
-        __state_->__count_.fetch_add(1, __std::memory_order_relaxed);
+        // pull the rug under our feet. Only do so if the barrier has not already been
+        // released: a plain fetch_add would take __count_ from 0 back to 1, and this
+        // callback's own __arrive() would then observe 1 and run __complete() a second
+        // time, concurrently with the thread that released the barrier. While this
+        // callback runs, that thread is blocked in ~inplace_stop_callback, so the state
+        // is guaranteed to still be alive here.
+        auto __old = __state_->__count_.load(__std::memory_order_relaxed);
+        do
+        {
+          if (__old == 0)
+          {
+            return;
+          }
+        }
+        while (!__state_->__count_.compare_exchange_weak(
+          __old, __old + 1, __std::memory_order_acq_rel, __std::memory_order_relaxed));
```

With this applied on top of `2c56ffe7`, the reproducer runs 200000 rounds clean on both compilers. I also ran a focused behaviour check (all-success, one child errors, one child stops, error trumps stopped, externally pre-stopped token, and "a failing child cancels its siblings' tokens") — identical results patched and unpatched. I did **not** run the full test suite.

## Environment

| | |
|---|---|
| stdexec | `2c56ffe7f8a2b8b5221918159092be379ae8b40f` (main, 2026-09-04) |
| also reproduces on | `f91f63636f24a85b594dfb19b79d191ccdccd5ec` (2026-08-22) |
| compilers | clang 22.1.8, gcc 16.1.1 (libstdc++ 16) |
| OS | Arch Linux on WSL2, 20 cores |
| flags | `-std=c++2b -O2` |
