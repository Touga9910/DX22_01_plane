#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>

struct RunEventDefinition
{
	const char* id;
	const char* title;
};

inline const std::array<RunEventDefinition, 6>& RunEventDefinitions()
{
	static const std::array<RunEventDefinition, 6> events{{
		{ "polishing_table", reinterpret_cast<const char*>(u8"熱を帯びる研磨台") },
		{ "water_station", reinterpret_cast<const char*>(u8"道端の給水所") },
		{ "lost_ball_locker", reinterpret_cast<const char*>(u8"忘れ物ロッカー") },
		{ "ball_recycler", reinterpret_cast<const char*>(u8"古球回収所") },
		{ "trade_workshop", reinterpret_cast<const char*>(u8"下取り工房") },
		{ "pendulum_safe", reinterpret_cast<const char*>(u8"振り子金庫") },
	}};
	return events;
}

inline const RunEventDefinition* FindRunEventDefinition(std::string_view id)
{
	for (const auto& event : RunEventDefinitions())
	{
		if (id == event.id) return &event;
	}
	return nullptr;
}

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
	bool requiresConfirmation = false;
	std::string effectId;
	int hpCost = 0;
	int moneyCost = 0;
	int amount = 0;
	int catalogIndex = -1;
};

// Retained until the player acknowledges it. Saving this value prevents an
// already-applied choice from being applied again after loading a checkpoint.
struct RunEventResult
{
	bool pending = false;
	bool repeatable = false;
	std::string eventId;
	std::string title;
	std::string detail;
	int hpBefore = 0;
	int hpAfter = 0;
	int moneyBefore = 0;
	int moneyAfter = 0;
};

struct RunEventViewSnapshot
{
	std::string title;
	int hp = 0;
	int maxHp = 0;
	int money = 0;
	std::vector<RunEventChoice> choices;
	std::string eventId;
	RunEventResult result;
	std::vector<RunEventTarget> deck;
};
