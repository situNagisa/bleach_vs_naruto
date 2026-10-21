module;
#include <stop_token>
export module stop_origin;
struct callback { void operator()() const noexcept {} };
export inline void attach(::std::stop_token token) { ::std::stop_callback<callback> registration{token, callback{}}; }
