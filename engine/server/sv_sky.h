#pragma once

#include <sky/sky.h>

namespace sky
{
	struct FragEvent
	{
		std::string killer_name;
		std::string victim_name;
		std::string weapon;
		bool killer_is_bot;
		bool victim_is_bot;
	};
}