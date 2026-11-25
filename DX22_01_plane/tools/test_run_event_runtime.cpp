#include "RunEventRuntime.h"
#include "PlayerBallText.h"
#include "PlayerDeck.h"
#include "PlayerRunStatus.h"
#include "RunController.h"
#include "RunMap.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <limits>

namespace
{
PlayerBallData Ball(const char* id)
{
	PlayerBallData ball;
	ball.definitionId = id;
	ball.status.attack = 1;
	ball.upgradeTable[0].attack = 2;
	ball.upgradeTable[1].attack = 3;
	return ball;
}

void Configure(RunController& run, int count = 5)
{
	const auto basic = Ball("player_standard");
	run.Deck().SetDefaultDeck(std::vector<PlayerBallData>(count, basic));
	run.Deck().SetCatalog({ basic, Ball("player_pierce") });
	run.Deck().Seed(100);
	run.Deck().ResetToDefault();
	run.Status().maxHp = 40;
	run.Status().currentHp = 40;
	run.Status().money = 100;
}

int FindEffect(const RunEvents::Offer& offer, const char* effect)
{
	for (int i = 0; i < static_cast<int>(offer.options.size()); ++i)
		if (offer.options[i].choice.effectId == effect && offer.options[i].choice.enabled) return i;
	return -1;
}
}

int main()
{
	RunMap map;
	map.Generate(1125);
	assert(map.SetDebugEventEntry());
	const auto& node = *map.Node(0);
	RunController run;
	Configure(run);
	// Rewards must remain saveable at the persistence limit and must not
	// overflow when the caller supplies a large nonnegative reward.
	run.Status().money = PlayerRunStatus::MaximumMoney;
	run.AddMoney(20);
	assert(run.Status().money == PlayerRunStatus::MaximumMoney);
	run.Status().money = 100;
	run.AddMoney((std::numeric_limits<int>::max)());
	assert(run.Status().money == PlayerRunStatus::MaximumMoney);
	run.AddMoney(-10);
	assert(run.Status().money == PlayerRunStatus::MaximumMoney);
	run.Status().money = 100;
	run.AddMoney(25);
	assert(run.Status().money == 125);
	run.Status().money = 100;
	RunEventResult result;
	RunEvents::Application applied;
	int actions = 0;

	// A result is a durable acknowledgement barrier: retries do not charge or
	// modify the deck, and another polish uses the increased HP cost.
	assert(RunEvents::Apply(map, node, "polishing_table", run, actions, result, 0, 0, applied));
	assert(actions == 1 && run.Status().currentHp == 35 && result.pending && result.repeatable);
	assert(run.Deck().GetRewardTarget(0)->upgradeLevel == 1);
	assert(result.hpBefore == 40 && result.hpAfter == 35 && result.moneyAfter == 100);
	assert(!RunEvents::Apply(map, node, "polishing_table", run, actions, result, 0, 0, applied));
	assert(run.Status().currentHp == 35 && actions == 1);
	result = {};
	assert(RunEvents::Apply(map, node, "polishing_table", run, actions, result, 0, 0, applied));
	assert(actions == 2 && run.Status().currentHp == 27 && run.Deck().GetRewardTarget(0)->upgradeLevel == 2);
	result = {};
	assert(!RunEvents::Apply(map, node, "polishing_table", run, actions, result, 0, 0, applied));
	assert(run.Status().currentHp == 27 && actions == 2 && !result.pending);

	// Insufficient resources and stale targets are rejected before payment.
	Configure(run);
	actions = 0;
	run.Status().currentHp = 5;
	assert(!RunEvents::Apply(map, node, "polishing_table", run, actions, result, 0, 0, applied));
	assert(run.Status().currentHp == 5 && run.Deck().GetRewardTarget(0)->upgradeLevel == 0);
	run.Status().currentHp = 20;
	run.Status().money = 7;
	assert(!RunEvents::Apply(map, node, "water_station", run, actions, result, 0, -1, applied));
	assert(run.Status().currentHp == 20 && run.Status().money == 7);
	run.Status().money = 8;
	assert(!RunEvents::Apply(map, node, "water_station", run, actions, result, 0, 999, applied));
	assert(run.Status().currentHp == 20 && run.Status().money == 8);
	assert(RunEvents::Apply(map, node, "water_station", run, actions, result, 0, -1, applied));
	assert(run.Status().currentHp == 30 && run.Status().money == 0 && !result.repeatable);
	result = {};
	assert(!RunEvents::Apply(map, node, "ball_recycler", run, actions, result, 0, 0, applied));
	assert(run.Deck().GetRewardTargetCount() == 5);

	// Identical base-ball exchanges are excluded, but meaningful replacements
	// include the comparison and a warning about losing the old upgrade.
	Configure(run);
	auto trade = RunEvents::Build(map, node, run.Status(), run.Deck(), 0, "trade_workshop");
	int standardOffer = -1, pierceOffer = -1;
	for (int i = 0; i < static_cast<int>(trade.options.size()); ++i)
	{
		if (trade.options[i].choice.catalogIndex == 0) standardOffer = i;
		if (trade.options[i].choice.catalogIndex == 1) pierceOffer = i;
	}
	assert(standardOffer >= 0 && pierceOffer >= 0);
	assert(!trade.options[standardOffer].choice.enabled && trade.options[standardOffer].choice.targets.empty());
	assert(trade.options[pierceOffer].choice.requiresConfirmation);
	assert(!RunEvents::Apply(map, node, "trade_workshop", run, actions, result, standardOffer, 0, applied));
	assert(run.Status().money == 100);
	assert(run.UpgradeBall(0));
	trade = RunEvents::Build(map, node, run.Status(), run.Deck(), 0, "trade_workshop");
	assert(trade.options[standardOffer].choice.targets.size() == 1);
	assert(trade.options[standardOffer].choice.targets[0].detail.find(PlayerBallText::Utf8(u8"強化は引き継がれません")) != std::string::npos);
	assert(RunEvents::Apply(map, node, "trade_workshop", run, actions, result, pierceOffer, 0, applied));
	assert(run.Deck().GetRewardTargetCount() == 5 && run.Status().money == 95);
	assert(run.Deck().GetRewardTarget(0)->definitionId == "player_pierce" && run.Deck().GetRewardTarget(0)->upgradeLevel == 0);
	assert(result.detail.find("+1") != std::string::npos && result.detail.find("+0") != std::string::npos);
	result = {};

	Configure(run, 6);
	assert(RunEvents::Apply(map, node, "ball_recycler", run, actions, result, 0, 0, applied));
	assert(run.Deck().GetRewardTargetCount() == 5 && run.Status().money == 90);
	result = {};
	const auto locker = RunEvents::Build(map, node, run.Status(), run.Deck(), 0, "lost_ball_locker");
	assert(RunEvents::Apply(map, node, "lost_ball_locker", run, actions, result, 0, -1, applied));
	assert(run.Deck().GetRewardTargetCount() == 6 && run.Status().money == 78 && !applied.acquiredBallId.empty());
	result = {};

	// Deterministic admission skips unusable normal events, and supplies a
	// one-use recovery only when all six normal events would be unusable.
	Configure(run);
	run.Status().currentHp = 5;
	run.Status().money = 0;
	const auto selected = RunEvents::SelectEventId(map, node, run.Status(), run.Deck());
	assert(selected == "water_station");
	assert(RunEvents::SelectEventId(map, node, run.Status(), run.Deck()) == selected);
	const auto emergency = RunEvents::Build(map, node, run.Status(), run.Deck(), 0, selected);
	const int heal = FindEffect(emergency, "heal");
	assert(heal >= 0 && emergency.options[heal].choice.moneyCost == 0);
	assert(RunEvents::Apply(map, node, selected, run, actions, result, heal, -1, applied));
	assert(run.Status().currentHp == 8 && result.pending && !result.repeatable);
	assert(!RunEvents::Apply(map, node, selected, run, actions, result, heal, -1, applied));
	result = {};
	Configure(run);
	run.Status().maxHp = run.Status().currentHp = 3;
	run.Status().money = 0;
	const auto fullEmergencyId = RunEvents::SelectEventId(map, node, run.Status(), run.Deck());
	const auto fullEmergency = RunEvents::Build(map, node, run.Status(), run.Deck(), 0, fullEmergencyId);
	const int help = FindEffect(fullEmergency, "money");
	assert(help >= 0 && fullEmergency.options[help].choice.hpCost == 0 && fullEmergency.options[help].choice.amount == 3);
	assert(RunEvents::Apply(map, node, fullEmergencyId, run, actions, result, help, -1, applied));
	assert(run.Status().currentHp == 3 && run.Status().money == 3 && !result.repeatable);
	result = {};
	Configure(run);
	run.Status().currentHp = run.Status().maxHp;
	run.Status().money = 0;
	const auto usableId = RunEvents::SelectEventId(map, node, run.Status(), run.Deck());
	const auto usable = RunEvents::Build(map, node, run.Status(), run.Deck(), 0, usableId);
	assert(std::any_of(usable.options.begin(), usable.options.end(), [](const auto& o)
		{ return o.choice.enabled && o.effect != RunEvents::Effect::Leave; }));

	// Both random outcomes show explicit results, and repeating the same
	// seed after reconstruction cannot change a paid gamble into another roll.
	bool sawWin = false, sawLoss = false;
	for (unsigned seed = 1; seed <= 50; ++seed)
	{
		RunMap sample;
		sample.Generate(seed);
		Configure(run);
		result = {};
		assert(RunEvents::Apply(sample, node, "pendulum_safe", run, actions, result, 1, -1, applied));
		const int award = applied.moneyAward;
		sawWin |= award == 20;
		sawLoss |= award == 0;
		assert(result.moneyBefore == 100 && result.moneyAfter == 95 + award && !result.title.empty());
		Configure(run);
		result = {};
		assert(RunEvents::Apply(sample, node, "pendulum_safe", run, actions, result, 1, -1, applied));
		assert(applied.moneyAward == award);
	}
	assert(sawWin && sawLoss);
	result = {};
	const auto safe = RunEvents::Build(map, node, run.Status(), run.Deck(), 0, "pendulum_safe");
	assert(RunEvents::Apply(map, node, "pendulum_safe", run, actions, result,
		static_cast<int>(safe.options.size()) - 1, -1, applied));
	assert(result.pending && !result.repeatable && result.hpBefore == result.hpAfter && result.moneyBefore == result.moneyAfter);
	std::cout << "PASS: six event effects, result barrier, costs, upgrades, exchange validation, fallback, seeded outcomes\n";
}
