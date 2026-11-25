#pragma once

#include "RunEventView.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

class PlayerDeck;
class RunController;
class RunMap;
struct RunMapNode;
struct PlayerRunStatus;

// The UI, MCP and tests share the same offer validation and effect execution.
namespace RunEvents
{
enum class Effect { Leave, Upgrade, Heal, Acquire, Remove, Trade, Money, Gamble };

struct Option
{
	RunEventChoice choice;
	Effect effect = Effect::Leave;
	int hpCost = 0;
	int moneyCost = 0;
	int amount = 0;
};

struct Offer
{
	std::string id;
	std::string title;
	std::vector<Option> options;
};

struct Application
{
	std::uint64_t targetInstanceId = 0;
	std::string acquiredBallId;
	std::string effectId;
	int moneyAward = 0;
};

std::string SelectEventId(const RunMap& map, const RunMapNode& node,
	const PlayerRunStatus& status, const PlayerDeck& deck);
Offer Build(const RunMap& map, const RunMapNode& node,
	const PlayerRunStatus& status, const PlayerDeck& deck, int polishActions,
	std::string_view eventId);
std::vector<RunEventTarget> CaptureDeck(const PlayerDeck& deck);
bool Apply(const RunMap& map, const RunMapNode& node, std::string_view eventId,
	RunController& run, int& polishActions, RunEventResult& result,
	int choiceIndex, int targetIndex, Application& application);
}
