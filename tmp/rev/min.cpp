#include "dynamic_when_all.h"
#include <vector>
#include <cstdio>
namespace ex = ::stdexec;
struct tick { auto operator()() const -> void {} };
using plain = decltype(ex::then(ex::just(), tick{}));
int main()
{
	::std::vector<plain> children;
	children.push_back(ex::then(ex::just(), tick{}));
	ex::sync_wait(dynamic_when_all(::std::move(children)));
	::std::printf("ok\n");
	return 0;
}
