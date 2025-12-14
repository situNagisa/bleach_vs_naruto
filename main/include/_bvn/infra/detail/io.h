#pragma once

#include "./environment.h"

NAGISA_BUILD_LIB_DETAIL_BEGIN

struct key
{
	virtual ~key() = default;
	virtual bool pressed() const = 0;
};

NAGISA_BUILD_LIB_DETAIL_END
