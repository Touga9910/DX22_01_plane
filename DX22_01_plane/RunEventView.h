#pragma once

#include <string>
#include <vector>

struct RunEventTarget
{
	int index = -1;
	std::string label;
	std::string detail;
};

struct RunEventChoice
{
	std::string label;
	std::string detail;
	bool enabled = false;
	std::vector<RunEventTarget> targets;
};

struct RunEventViewSnapshot
{
	std::string title;
	int hp = 0;
	int maxHp = 0;
	int money = 0;
	std::vector<RunEventChoice> choices;
};
