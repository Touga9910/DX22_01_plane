#include "Game.h"
#include "PlayerBallText.h"
#include "PlayerDeck.h"
#include "PlayerRunStatus.h"
#include "RunController.h"
#include "RunEventView.h"
#include "RunProgressController.h"
#include "json/json.hpp"

#include <algorithm>
#include <cstdint>
#include <random>
#include <string_view>
#include <type_traits>
#include <utility>

namespace
{
using PlayerBallText::Utf8;

enum class EventType
{
	PolishingTable,
	WaterStation,
	LostBallLocker,
	BallRecycler,
	TradeWorkshop,
	PendulumSafe,
	Count,
};

enum class EventEffect
{
	Leave,
	UpgradeBall,
	Heal,
	AcquireBall,
	RemoveBall,
	TradeBall,
	GainMoney,
	Gamble,
};

struct EventCost
{
	int hp = 0;
	int money = 0;
};

struct EventOption
{
	RunEventChoice choice;
	EventEffect effect;
	EventCost cost;
	int amount;
};

struct EventOffer
{
	const char* id;
	const char* title;
	std::vector<EventOption> options;
};

constexpr int kGambleWinPercent = 40;
constexpr int kGambleReward = 20;

std::mt19937 MakeEventRandom(const RunMap& map, const RunMapNode& node, unsigned purpose)
{
	std::seed_seq seed{ map.Seed(), static_cast<unsigned>(node.id), purpose };
	return std::mt19937(seed);
}

const RunMapNode* ActiveEvent(const RunMap& map)
{
	const RunMapNode* node = map.Node(map.Active());
	return node != nullptr && node->type == StageRouteType::Event ? node : nullptr;
}

void Disable(EventOption& option, const char* reason)
{
	option.choice.enabled = false;
	option.choice.detail += Utf8(u8" / ");
	option.choice.detail += reason;
}

EventOption MakeOption(
	const PlayerRunStatus& status,
	EventEffect effect,
	std::string label,
	std::string detail,
	EventCost cost = {},
	int amount = 0)
{
	if (cost.hp > 0)
	{
		std::string payment = "HP -" + std::to_string(cost.hp);
		if (status.currentHp > cost.hp)
		{
			payment += Utf8(u8"（") + std::to_string(status.currentHp) + Utf8(u8" → ") +
				std::to_string(status.currentHp - cost.hp) + Utf8(u8"）");
		}
		detail = payment + " / " + detail;
	}
	if (cost.money > 0) detail = "Money -" + std::to_string(cost.money) + " / " + detail;
	EventOption option{ { std::move(label), std::move(detail), true, {} }, effect, cost, amount };
	if (cost.hp > 0 && status.currentHp <= cost.hp)
	{
		Disable(option, Utf8(u8"HP不足"));
	}
	if (cost.money > 0 && status.money < cost.money)
	{
		Disable(option, Utf8(u8"所持Money不足"));
	}
	return option;
}

void AddDeckTargets(EventOption& option, const PlayerDeck& deck)
{
	const int count = deck.GetRewardTargetCount();
	if (option.effect == EventEffect::RemoveBall && count <= PlayerDeck::MinimumDeckSize)
	{
		const std::string reason = Utf8(u8"デッキには最低") +
			std::to_string(PlayerDeck::MinimumDeckSize) + Utf8(u8"個の球が必要");
		Disable(option, reason.c_str());
		return;
	}

	const bool upgrading = option.effect == EventEffect::UpgradeBall;
	for (int index = 0; index < count; ++index)
	{
		const PlayerBallData* ball = deck.GetRewardTarget(index);
		if (ball == nullptr || (upgrading && !ball->CanUpgrade()))
		{
			continue;
		}
		std::string label = PlayerBallText::GetName(ball->definitionId);
		if (ball->upgradeLevel > 0) label += " +" + std::to_string(ball->upgradeLevel);
		std::string detail = upgrading
			? PlayerBallText::GetUpgradePreview(*ball)
			: PlayerBallText::GetStats(*ball, ball->status);
		option.choice.targets.push_back({ index, std::move(label), std::move(detail) });
	}
	if (option.choice.targets.empty())
	{
		Disable(option, upgrading ? Utf8(u8"強化できる球がない") : Utf8(u8"対象の球がない"));
	}
}

void AddCatalogOptions(
	EventOffer& offer,
	const PlayerRunStatus& status,
	const PlayerDeck& deck,
	std::mt19937& random,
	bool trade)
{
	const EventEffect effect = trade ? EventEffect::TradeBall : EventEffect::AcquireBall;
	const EventCost cost{ 0, trade ? 5 : 12 };
	const int catalogCount = deck.GetCatalogCount();
	if (catalogCount == 0)
	{
		EventOption option = MakeOption(status, effect, Utf8(u8"球を選ぶ"), Utf8(u8"候補なし"), cost);
		option.choice.enabled = false;
		offer.options.push_back(std::move(option));
		return;
	}

	const int first = static_cast<int>(random() % catalogCount);
	for (int slot = 0; slot < (std::min)(2, catalogCount); ++slot)
	{
		int index = first;
		if (slot == 1)
		{
			index = static_cast<int>(random() % (catalogCount - 1));
			if (index >= first) ++index;
		}
		const PlayerBallData& ball = *deck.GetCatalogBall(index);
		std::string label = PlayerBallText::GetName(ball.definitionId);
		label += trade ? Utf8(u8"に交換する") : Utf8(u8"を買う");
		EventOption option = MakeOption(
			status, effect, std::move(label), PlayerBallText::GetStats(ball, ball.status), cost, index);
		if (trade) AddDeckTargets(option, deck);
		offer.options.push_back(std::move(option));
	}
}

EventOffer BuildEvent(
	const RunMap& map,
	const RunMapNode& node,
	const PlayerRunStatus& status,
	const PlayerDeck& deck,
	int polishActions,
	std::string_view debugEventId)
{
	auto random = MakeEventRandom(map, node, 17);
	const auto& definitions = RunEventDefinitions();
	static_assert(std::tuple_size_v<std::remove_cvref_t<decltype(definitions)>> == static_cast<std::size_t>(EventType::Count));
	std::size_t index = random() % definitions.size();
	if (!debugEventId.empty())
	{
		if (const auto* forced = FindRunEventDefinition(debugEventId))
			index = static_cast<std::size_t>(forced - definitions.data());
	}
	const auto& definition = definitions[index];
	EventOffer offer{ definition.id, definition.title, {} };
	switch (static_cast<EventType>(index))
	{
	case EventType::PolishingTable:
		offer.options.push_back(MakeOption(status, EventEffect::UpgradeBall,
			Utf8(u8"球を磨く"), Utf8(u8"選んだ球を1段階強化。次回は消費HPが3増える"),
			{ 5 + 3 * polishActions, 0 }));
		AddDeckTargets(offer.options.back(), deck);
		break;
	case EventType::WaterStation:
	{
		const int heal = (std::min)(10, status.maxHp - status.currentHp);
		offer.options.push_back(MakeOption(status, EventEffect::Heal,
			Utf8(u8"水を飲む"), "HP +" + std::to_string(heal), { 0, 8 }, heal));
		if (heal <= 0) Disable(offer.options.back(), Utf8(u8"HPが満タン"));
		break;
	}
	case EventType::LostBallLocker:
		AddCatalogOptions(offer, status, deck, random, false);
		break;
	case EventType::BallRecycler:
		offer.options.push_back(MakeOption(status, EventEffect::RemoveBall,
			Utf8(u8"Moneyで回収を頼む"), Utf8(u8"選んだ球を1個削除"), { 0, 10 }));
		offer.options.push_back(MakeOption(status, EventEffect::RemoveBall,
			Utf8(u8"HPを使って回収する"), Utf8(u8"選んだ球を1個削除"), { 7, 0 }));
		for (EventOption& option : offer.options) AddDeckTargets(option, deck);
		break;
	case EventType::TradeWorkshop:
		AddCatalogOptions(offer, status, deck, random, true);
		break;
	case EventType::PendulumSafe:
		offer.options.push_back(MakeOption(status, EventEffect::GainMoney,
			Utf8(u8"振り子を押さえる"), "Money +12", { 6, 0 }, 12));
		offer.options.push_back(MakeOption(status, EventEffect::Gamble,
			Utf8(u8"金庫を開ける"), std::to_string(kGambleWinPercent) +
			Utf8(u8"%でMoney +") + std::to_string(kGambleReward) + Utf8(u8"、外れると0"), { 0, 5 }));
		break;
	case EventType::Count:
		break;
	}
	offer.options.push_back(MakeOption(status, EventEffect::Leave, Utf8(u8"立ち去る"), {}));
	return offer;
}
}

RunEventViewSnapshot Game::CaptureRunEvent() const
{
	if (GetCurrentSceneType() != SceneType::Event || GetRunPhase() != RunPhase::NormalRoute)
	{
		return {};
	}
	const RunMap& map = GetRunMap();
	const RunMapNode* node = ActiveEvent(map);
	if (node == nullptr) return {};

	const PlayerRunStatus& status = m_RunController->Status();
	const auto debugEvent = IsDebugRunMode() ? std::string_view(m_DebugRunEventId) : std::string_view{};
	EventOffer offer = BuildEvent(map, *node, status, m_RunController->Deck(), m_RunEventActions, debugEvent);
	RunEventViewSnapshot snapshot{ offer.title, status.currentHp, status.maxHp, status.money, {}, offer.id };
	for (EventOption& option : offer.options) snapshot.choices.push_back(std::move(option.choice));
	return snapshot;
}

bool Game::ChooseRunEvent(int choiceIndex, int targetIndex)
{
	if (GetCurrentSceneType() != SceneType::Event || GetRunPhase() != RunPhase::NormalRoute)
	{
		return false;
	}
	const RunMap& map = GetRunMap();
	const RunMapNode* node = ActiveEvent(map);
	if (node == nullptr || choiceIndex < 0) return false;

	PlayerRunStatus& status = m_RunController->Status();
	PlayerDeck& deck = m_RunController->Deck();
	const auto debugEvent = IsDebugRunMode() ? std::string_view(m_DebugRunEventId) : std::string_view{};
	const EventOffer offer = BuildEvent(map, *node, status, deck, m_RunEventActions, debugEvent);
	if (static_cast<std::size_t>(choiceIndex) >= offer.options.size()) return false;
	const EventOption& option = offer.options[choiceIndex];
	const auto& targets = option.choice.targets;
	if (!option.choice.enabled || (targets.empty() && targetIndex != -1) ||
		(!targets.empty() && std::none_of(targets.begin(), targets.end(), [targetIndex](const RunEventTarget& target)
			{ return target.index == targetIndex; })))
	{
		return false;
	}

	const int hpBefore = status.currentHp;
	const int moneyBefore = status.money;
	const PlayerBallData* target = deck.GetRewardTarget(targetIndex);
	const std::uint64_t targetId = target != nullptr ? target->instanceId : 0;
	const PlayerBallData* acquired = nullptr;
	int moneyAward = 0;
	switch (option.effect)
	{
	case EventEffect::Leave:
		break;
	case EventEffect::UpgradeBall:
		if (!m_RunController->UpgradeBall(targetIndex)) return false;
		RemoveBalanceAutoPendingBall(targetId);
		++m_RunEventActions;
		break;
	case EventEffect::Heal:
		status.currentHp += option.amount;
		break;
	case EventEffect::AcquireBall:
		if (!deck.AddCatalogBall(option.amount)) return false;
		acquired = deck.GetCatalogBall(option.amount);
		break;
	case EventEffect::RemoveBall:
		if (!deck.RemoveRewardTarget(targetIndex)) return false;
		RemoveBalanceAutoPendingBall(targetId);
		break;
	case EventEffect::TradeBall:
		if (!deck.ReplaceRewardTarget(targetIndex, option.amount)) return false;
		RemoveBalanceAutoPendingBall(targetId);
		acquired = deck.GetCatalogBall(option.amount);
		break;
	case EventEffect::GainMoney:
		moneyAward = option.amount;
		break;
	case EventEffect::Gamble:
	{
		auto random = MakeEventRandom(map, *node, 29);
		if (random() % 100 < kGambleWinPercent) moneyAward = kGambleReward;
		break;
	}
	}

	// The same option snapshot already verified affordability. A failed ball action never charges the player.
	status.currentHp -= option.cost.hp;
	if (option.cost.money > 0) m_RunController->SpendMoney(option.cost.money);
	if (moneyAward > 0) m_RunController->AddMoney(moneyAward);
	if (acquired != nullptr) NotifyBallAcquired(acquired->definitionId);
	RecordBalanceEvent("run_event_choice", {
		{ "event", offer.id }, { "node_id", node->id }, { "choice_index", choiceIndex },
		{ "target_instance_id", targetId }, { "acquired_ball_id", acquired != nullptr ? acquired->definitionId : "" },
		{ "hp_before", hpBefore }, { "hp_after", status.currentHp },
		{ "money_before", moneyBefore }, { "money_after", status.money },
		{ "money_award", moneyAward }, { "polish_actions", m_RunEventActions },
	});
	if (option.effect == EventEffect::UpgradeBall)
	{
		SaveRunCheckpoint(SceneType::Event, false, false);
	}
	else
	{
		m_DebugRunEventId.clear();
		CompleteNormalRouteArea("event");
		EnterNextRouteAfterArea();
	}
	return true;
}
