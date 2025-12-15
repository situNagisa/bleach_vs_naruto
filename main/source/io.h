#pragma once

#include <memory>


struct io
{
	struct keyboard
	{
		virtual ~keyboard() = default;

		virtual bool a() const = 0;
		virtual bool d() const = 0;
		virtual bool w() const = 0;
		virtual bool s() const = 0;
		virtual bool j() const = 0;
		virtual bool k() const = 0;
		virtual bool l() const = 0;
		virtual bool u() const = 0;
		virtual bool i() const = 0;
		virtual bool o() const = 0;
	};

	::std::unique_ptr<keyboard>(*default_keyboard)();
};