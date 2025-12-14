#pragma once

#include "./environment.h"

NAGISA_BUILD_LIB_DETAIL_BEGIN

struct scene
{
	virtual ~scene() = default;

	virtual ::std::unique_ptr<scene> run() = 0;
};

NAGISA_BUILD_LIB_DETAIL_END