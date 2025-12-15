#include <stdexec/execution.hpp>

#include "./fighter.h"

#undef main
int main()
{
	::stdexec::sync_wait(fighter_scene());
	return 0;
}
