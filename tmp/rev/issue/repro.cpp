// Minimal reproducer: stdexec::when_all can run __complete() on two threads at once,
// which double-destroys __on_stop_ and completes the receiver twice.
//
// Build:  c++ -std=c++2b -O2 -I<stdexec>/include repro.cpp -o repro -lpthread
// Run:    ./repro            # hits within a few thousand rounds on a multicore box
//
// ---------------------------------------------------------------------------
// The race
//
//   __when_all::__state::__arrive():
//       if (1 == __count_.fetch_sub(1, acq_rel)) __complete();
//   __when_all::__state::__complete():
//       if constexpr (_UsesStopSource) __on_stop_.reset();      // <-- unregisters
//       switch (__state_.load(relaxed)) { ...complete the receiver... }
//
//   Between the fetch_sub that drops __count_ to 0 and the reset() that
//   unregisters the stop callback, an external request_stop() can still *start*
//   __forward_stop_request. That callback does:
//       __count_.fetch_add(1, relaxed);   // 0 -> 1
//       ...
//       __arrive();                       // 1 -> 0, so it also observes 1
//   so it calls __complete() as well.
//
//   The temporary increment is documented as protecting against concurrent /
//   recursive arrivals pulling the rug out from under the callback, and it does
//   handle the case where request_stop() makes children complete synchronously.
//   It does not handle the case where __count_ already reached zero before the
//   callback was entered: there the increment resurrects the counter and
//   manufactures a second "I am the last arrival".
//
//   ~inplace_stop_callback only guarantees that destruction waits for an
//   already-running callback; it cannot prevent one from being started. And
//   request_stop() unlinks the callback before invoking it, so the thread inside
//   reset() simply waits for it -- then proceeds to complete the receiver too.
//
//   Observable results, either of which this program reports:
//     * the receiver is completed twice, or
//     * the process deadlocks, because both threads destroy the same
//       __optional<__stop_callback_t>.
// ---------------------------------------------------------------------------
#include <stdexec/execution.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <thread>

namespace ex = stdexec;

// A child sender whose completion is delivered manually, so the test controls
// the exact moment of the final arrival.
struct trigger
{
  std::atomic<void*> object{nullptr};
  void (*fire)(void*) noexcept = nullptr;
};

struct manual_sender
{
  using sender_concept = ex::sender_t;
  using completion_signatures = ex::completion_signatures<
    ex::set_value_t(),
    ex::set_error_t(std::exception_ptr),
    ex::set_stopped_t()>;

  trigger* t;

  template <class Receiver>
  struct operation
  {
    using operation_state_concept = ex::operation_state_t;

    Receiver rcvr;
    trigger* t;

    void start() & noexcept
    {
      t->fire = [](void* p) noexcept {
        ex::set_value(std::move(static_cast<operation*>(p)->rcvr));
      };
      t->object.store(this, std::memory_order_release);
    }
  };

  template <ex::receiver Receiver>
  auto connect(Receiver r) && -> operation<Receiver>
  {
    return operation<Receiver>{std::move(r), t};
  }
};

// A conforming operation completes its receiver exactly once.
struct counting_receiver
{
  using receiver_concept = ex::receiver_t;

  std::atomic<int>* completions;
  ex::inplace_stop_token token;

  void set_value() noexcept { completions->fetch_add(1, std::memory_order_relaxed); }
  void set_error(std::exception_ptr) noexcept { completions->fetch_add(1, std::memory_order_relaxed); }
  void set_stopped() noexcept { completions->fetch_add(1, std::memory_order_relaxed); }

  auto get_env() const noexcept { return ex::prop{ex::get_stop_token, token}; }
};

std::atomic<int> g_round{0};

int main()
{
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  constexpr int rounds = 200000;

  // Reports the deadlock variant instead of hanging forever.
  std::thread watchdog{[] {
    int last = -1;
    for (;;)
    {
      std::this_thread::sleep_for(std::chrono::seconds{5});
      int const now = g_round.load(std::memory_order_relaxed);
      if (now == last)
      {
        std::printf(
          "DEADLOCK at round %d: two threads are both inside __complete(), "
          "destroying the same __on_stop_.\n",
          now);
        std::_Exit(2);
      }
      last = now;
    }
  }};
  watchdog.detach();

  for (int round = 1; round <= rounds; ++round)
  {
    g_round.store(round, std::memory_order_relaxed);

    trigger t;
    ex::inplace_stop_source source;
    std::atomic<int> completions{0};

    // Two children: with a single child when_all elides the internal stop
    // source entirely, so __forward_stop_request is never registered.
    auto op = ex::connect(
      ex::when_all(manual_sender{&t}, ex::just()),
      counting_receiver{&completions, source.get_token()});
    ex::start(op); // registers __forward_stop_request, starts the children

    std::atomic<int> ready{0};
    std::atomic<bool> go{false};

    // Thread A: deliver the final child completion -> __arrive() drops __count_ to 0.
    std::thread a{[&] {
      ready.fetch_add(1, std::memory_order_release);
      while (!go.load(std::memory_order_acquire)) { }
      t.fire(t.object.load(std::memory_order_acquire));
    }};

    // Thread B: external cancellation -> __forward_stop_request.
    std::thread b{[&] {
      ready.fetch_add(1, std::memory_order_release);
      while (!go.load(std::memory_order_acquire)) { }
      source.request_stop();
    }};

    while (ready.load(std::memory_order_acquire) < 2) { }
    go.store(true, std::memory_order_release);

    a.join();
    b.join();

    if (int const seen = completions.load(std::memory_order_relaxed); seen != 1)
    {
      std::printf("round %d: receiver was completed %d times (expected exactly 1)\n", round, seen);
      // Everything past a double completion is UB; leave without unwinding.
      std::_Exit(1);
    }
  }

  std::printf("%d rounds, no double completion observed\n", rounds);
  return 0;
}
