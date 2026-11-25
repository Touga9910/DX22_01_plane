#include "RunEventRuntime.h"
#include "PlayerBallText.h"
#include "PlayerDeck.h"
#include "PlayerRunStatus.h"
#include "RunController.h"
#include "RunMap.h"

#include <algorithm>
#include <array>
#include <random>
#include <utility>

namespace RunEvents
{
namespace
{
using PlayerBallText::Utf8;
constexpr int GambleWinPercent = 40;
constexpr int GambleReward = 20;

std::mt19937 Random(const RunMap& map, const RunMapNode& node, unsigned purpose)
{
	std::seed_seq seed{ map.Seed(), static_cast<unsigned>(node.id), purpose };
	return std::mt19937(seed);
}

const char* EffectId(Effect effect)
{
	switch (effect)
	{
	case Effect::Upgrade: return "upgrade";
	case Effect::Heal: return "heal";
	case Effect::Acquire: return "acquire";
	case Effect::Remove: return "remove";
	case Effect::Trade: return "trade";
	case Effect::Money: return "money";
	case Effect::Gamble: return "gamble";
	default: return "leave";
	}
}

std::string BallLabel(const PlayerBallData& ball)
{
	return std::string(PlayerBallText::GetName(ball.definitionId)) + " +" + std::to_string(ball.upgradeLevel);
}

std::string BallDetail(const PlayerBallData& ball)
{
	return PlayerBallText::GetStats(ball, ball.status) + "\n" + PlayerBallText::GetShortDescription(ball);
}

void Disable(Option& option, const std::string& reason)
{
	option.choice.enabled = false;
	option.choice.detail += "\n" + reason;
}

Option MakeOption(const PlayerRunStatus& status, Effect effect,
	std::string label, std::string detail, int hpCost = 0, int moneyCost = 0, int amount = 0)
{
	if (hpCost > 0)
	{
		std::string payment = "HP -" + std::to_string(hpCost);
		if (status.currentHp > hpCost)
			payment += Utf8(u8"（") + std::to_string(status.currentHp) + Utf8(u8" → ") +
				std::to_string(status.currentHp - hpCost) + Utf8(u8"）");
		detail = payment + " / " + detail;
	}
	if (moneyCost > 0) detail = "Money -" + std::to_string(moneyCost) + " / " + detail;
	Option option{ { std::move(label), std::move(detail), true, {} }, effect, hpCost, moneyCost, amount };
	option.choice.requiresConfirmation = effect == Effect::Remove || effect == Effect::Trade;
	option.choice.effectId = EffectId(effect);
	option.choice.hpCost = hpCost;
	option.choice.moneyCost = moneyCost;
	option.choice.amount = amount;
	if (effect == Effect::Acquire || effect == Effect::Trade) option.choice.catalogIndex = amount;
	if (hpCost > 0 && status.currentHp <= hpCost) Disable(option, Utf8(u8"HP不足"));
	if (moneyCost > status.money) Disable(option, Utf8(u8"所持Money不足"));
	return option;
}

void AddDeckTargets(Option& option, const PlayerDeck& deck)
{
	const int count = deck.GetRewardTargetCount();
	if (option.effect == Effect::Remove && count <= PlayerDeck::MinimumDeckSize)
	{
		Disable(option, Utf8(u8"デッキには最低") + std::to_string(PlayerDeck::MinimumDeckSize) + Utf8(u8"個の球が必要"));
		return;
	}
	const auto* replacement = option.effect == Effect::Trade ? deck.GetCatalogBall(option.amount) : nullptr;
	for (int index = 0; index < count; ++index)
	{
		const auto* ball = deck.GetRewardTarget(index);
		if (ball == nullptr || (option.effect == Effect::Upgrade && !ball->CanUpgrade())) continue;
		// Charging for an identical, unupgraded ball cannot improve the deck.
		if (replacement && ball->upgradeLevel == 0 && ball->definitionId == replacement->definitionId) continue;
		std::string label = BallLabel(*ball);
		std::string detail = option.effect == Effect::Upgrade
			? PlayerBallText::GetUpgradePreview(*ball) + "\n" + PlayerBallText::GetShortDescription(*ball)
			: BallDetail(*ball);
		if (replacement)
		{
			label += Utf8(u8" → ") + BallLabel(*replacement);
			detail = Utf8(u8"交換元：") + BallDetail(*ball) + "\n" +
				Utf8(u8"交換後：") + BallDetail(*replacement) + "\n" +
				Utf8(u8"交換元の強化は引き継がれません。");
		}
		option.choice.targets.push_back({ index, std::move(label), std::move(detail) });
	}
	if (option.choice.targets.empty())
		Disable(option, option.effect == Effect::Upgrade ? Utf8(u8"強化できる球がない") : Utf8(u8"対象の球がない"));
}

void AddCatalogOptions(Offer& offer, const PlayerRunStatus& status,
	const PlayerDeck& deck, std::mt19937& random, bool trade)
{
	const auto effect = trade ? Effect::Trade : Effect::Acquire;
	const int count = deck.GetCatalogCount();
	if (count == 0)
	{
		auto option = MakeOption(status, effect, Utf8(u8"球を選ぶ"), Utf8(u8"候補なし"), 0, trade ? 5 : 12);
		option.choice.enabled = false;
		offer.options.push_back(std::move(option));
		return;
	}
	const int first = static_cast<int>(random() % count);
	for (int slot = 0; slot < (std::min)(2, count); ++slot)
	{
		int index = first;
		if (slot == 1)
		{
			index = static_cast<int>(random() % (count - 1));
			if (index >= first) ++index;
		}
		const auto& ball = *deck.GetCatalogBall(index);
		auto label = BallLabel(ball) + (trade ? Utf8(u8"に交換する") : Utf8(u8"を買う"));
		auto option = MakeOption(status, effect, std::move(label), BallDetail(ball), 0, trade ? 5 : 12, index);
		if (trade) AddDeckTargets(option, deck);
		offer.options.push_back(std::move(option));
	}
}

Offer BuildRaw(const RunMap& map, const RunMapNode& node, const PlayerRunStatus& status,
	const PlayerDeck& deck, int polishActions, std::string_view eventId)
{
	const auto* definition = FindRunEventDefinition(eventId);
	if (!definition) return {};
	Offer offer{ definition->id, definition->title, {} };
	auto random = Random(map, node, 17);
	(void)random(); // Keep catalog offers stable relative to the previous event draw.
	if (eventId == "polishing_table")
	{
		offer.options.push_back(MakeOption(status, Effect::Upgrade, Utf8(u8"球を磨く"),
			Utf8(u8"選んだ球を1段階強化。次回は消費HPが3増える"), 5 + 3 * polishActions));
		AddDeckTargets(offer.options.back(), deck);
	}
	else if (eventId == "water_station")
	{
		const int heal = (std::max)(0, (std::min)(10, status.maxHp - status.currentHp));
		offer.options.push_back(MakeOption(status, Effect::Heal, Utf8(u8"水を飲む"), "HP +" + std::to_string(heal), 0, 8, heal));
		if (heal == 0) Disable(offer.options.back(), Utf8(u8"HPが満タン"));
	}
	else if (eventId == "lost_ball_locker") AddCatalogOptions(offer, status, deck, random, false);
	else if (eventId == "ball_recycler")
	{
		offer.options.push_back(MakeOption(status, Effect::Remove, Utf8(u8"Moneyで回収を頼む"), Utf8(u8"選んだ球を1個削除"), 0, 10));
		offer.options.push_back(MakeOption(status, Effect::Remove, Utf8(u8"HPを使って回収する"), Utf8(u8"選んだ球を1個削除"), 7));
		for (auto& option : offer.options) AddDeckTargets(option, deck);
	}
	else if (eventId == "trade_workshop") AddCatalogOptions(offer, status, deck, random, true);
	else if (eventId == "pendulum_safe")
	{
		offer.options.push_back(MakeOption(status, Effect::Money, Utf8(u8"振り子を押さえる"), "Money +12", 6, 0, 12));
		offer.options.push_back(MakeOption(status, Effect::Gamble, Utf8(u8"金庫を開ける"),
			std::to_string(GambleWinPercent) + Utf8(u8"%でMoney +") + std::to_string(GambleReward) + Utf8(u8"、外れると0"), 0, 5));
	}
	return offer;
}

bool HasAction(const Offer& offer)
{
	return std::any_of(offer.options.begin(), offer.options.end(), [](const auto& option)
		{ return option.effect != Effect::Leave && option.choice.enabled; });
}

bool AnyAction(const RunMap& map, const RunMapNode& node, const PlayerRunStatus& status,
	const PlayerDeck& deck, int polishActions)
{
	for (const auto& definition : RunEventDefinitions())
		if (HasAction(BuildRaw(map, node, status, deck, polishActions, definition.id))) return true;
	return false;
}
}

std::string SelectEventId(const RunMap& map, const RunMapNode& node,
	const PlayerRunStatus& status, const PlayerDeck& deck)
{
	auto random = Random(map, node, 17);
	std::array<std::size_t, 6> order{ 0, 1, 2, 3, 4, 5 };
	const auto& definitions = RunEventDefinitions();
	for (std::size_t i = 0; i < order.size(); ++i)
	{
		const auto pick = i + random() % (order.size() - i);
		std::swap(order[i], order[pick]);
		const auto* id = definitions[order[i]].id;
		if (HasAction(BuildRaw(map, node, status, deck, 0, id))) return id;
	}
	return "water_station";
}

Offer Build(const RunMap& map, const RunMapNode& node, const PlayerRunStatus& status,
	const PlayerDeck& deck, int polishActions, std::string_view eventId)
{
	auto offer = BuildRaw(map, node, status, deck, polishActions, eventId);
	if (offer.id.empty()) return offer;
	// A small, one-use fallback only appears when all six normal events are
	// unusable. It cannot be repeated by polishing or by reopening the result.
	if (!HasAction(offer) && !AnyAction(map, node, status, deck, polishActions))
	{
		const int heal = (std::max)(0, (std::min)(3, status.maxHp - status.currentHp));
		if (heal > 0)
			offer.options.push_back(MakeOption(status, Effect::Heal, Utf8(u8"応急手当を受ける"),
				Utf8(u8"利用できる取引がないため、一度だけ無料で手当てを受ける。HP +") + std::to_string(heal), 0, 0, heal));
		else
			offer.options.push_back(MakeOption(status, Effect::Money, Utf8(u8"片付けを手伝う"),
				Utf8(u8"利用できる取引がないため、一度だけ手伝いの謝礼を受け取る。Money +3"), 0, 0, 3));
	}
	offer.options.push_back(MakeOption(status, Effect::Leave, Utf8(u8"立ち去る"), {}));
	return offer;
}

std::vector<RunEventTarget> CaptureDeck(const PlayerDeck& deck)
{
	std::vector<RunEventTarget> result;
	for (int index = 0; index < deck.GetRewardTargetCount(); ++index)
		if (const auto* ball = deck.GetRewardTarget(index))
			result.push_back({ index, BallLabel(*ball), BallDetail(*ball) });
	return result;
}

bool Apply(const RunMap& map, const RunMapNode& node, std::string_view eventId,
	RunController& run, int& polishActions, RunEventResult& result,
	int choiceIndex, int targetIndex, Application& application)
{
	if (result.pending || choiceIndex < 0 || polishActions < 0) return false;
	auto& status = run.Status();
	auto& deck = run.Deck();
	const auto offer = Build(map, node, status, deck, polishActions, eventId);
	if (static_cast<std::size_t>(choiceIndex) >= offer.options.size()) return false;
	const auto& option = offer.options[choiceIndex];
	const auto& targets = option.choice.targets;
	if (!option.choice.enabled || (targets.empty() && targetIndex != -1) ||
		(!targets.empty() && std::none_of(targets.begin(), targets.end(), [targetIndex](const auto& target)
			{ return target.index == targetIndex; }))) return false;
	// Validate against the current status, rather than trusting a UI snapshot.
	if (status.currentHp <= option.hpCost || status.money < option.moneyCost) return false;

	RunEventResult applied;
	applied.pending = true;
	applied.repeatable = option.effect == Effect::Upgrade;
	applied.eventId = offer.id;
	applied.hpBefore = status.currentHp;
	applied.moneyBefore = status.money;
	application = {};
	application.effectId = option.choice.effectId;
	const auto* target = deck.GetRewardTarget(targetIndex);
	const auto previousLabel = target ? BallLabel(*target) : std::string{};
	const auto previousStats = target ? PlayerBallText::GetStats(*target, target->status) : std::string{};
	application.targetInstanceId = target ? target->instanceId : 0;
	const PlayerBallData* acquired = nullptr;
	switch (option.effect)
	{
	case Effect::Leave:
		applied.title = Utf8(u8"イベントを終えました");
		applied.detail = Utf8(u8"何もせず、その場を離れます。");
		break;
	case Effect::Upgrade:
		if (!run.UpgradeBall(targetIndex)) return false;
		++polishActions;
		target = deck.GetRewardTarget(targetIndex);
		applied.title = Utf8(u8"球を強化しました");
		applied.detail = previousLabel + Utf8(u8" → ") + BallLabel(*target) + "\n" +
			Utf8(u8"強化前：") + previousStats + "\n" +
			Utf8(u8"強化後：") + PlayerBallText::GetStats(*target, target->status) + "\n" +
			Utf8(u8"次に磨く場合の消費HP：") + std::to_string(5 + 3 * polishActions);
		break;
	case Effect::Heal:
		status.currentHp = (std::min)(status.maxHp, status.currentHp + option.amount);
		applied.title = Utf8(u8"HPを回復しました");
		applied.detail = Utf8(u8"HPが") + std::to_string(status.currentHp - applied.hpBefore) + Utf8(u8"回復しました。");
		break;
	case Effect::Acquire:
		if (!deck.AddCatalogBall(option.amount)) return false;
		acquired = deck.GetCatalogBall(option.amount);
		applied.title = Utf8(u8"球を獲得しました");
		applied.detail = BallLabel(*acquired) + Utf8(u8"をデッキに追加しました。\n") + BallDetail(*acquired);
		break;
	case Effect::Remove:
		if (!deck.RemoveRewardTarget(targetIndex)) return false;
		applied.title = Utf8(u8"球を回収しました");
		applied.detail = previousLabel + Utf8(u8"をデッキから取り除きました。\n残りの球：") + std::to_string(deck.GetRewardTargetCount());
		break;
	case Effect::Trade:
		if (!deck.ReplaceRewardTarget(targetIndex, option.amount)) return false;
		acquired = deck.GetCatalogBall(option.amount);
		applied.title = Utf8(u8"球を交換しました");
		applied.detail = previousLabel + Utf8(u8" → ") + BallLabel(*acquired) + "\n" + BallDetail(*acquired);
		break;
	case Effect::Money:
		application.moneyAward = option.amount;
		applied.title = Utf8(u8"Moneyを受け取りました");
		applied.detail = option.choice.label + Utf8(u8"。\nMoney +") + std::to_string(option.amount);
		break;
	case Effect::Gamble:
	{
		auto random = Random(map, node, 29);
		const bool won = random() % 100 < GambleWinPercent;
		if (won) application.moneyAward = GambleReward;
		applied.title = won ? Utf8(u8"金庫を開けることに成功！") : Utf8(u8"金庫は開きませんでした");
		applied.detail = won ? Utf8(u8"金庫からMoney 20を獲得しました。挑戦料はMoney 5です。")
			: Utf8(u8"報酬はありません。挑戦料としてMoney 5を支払いました。");
		break;
	}
	}
	// Ball actions must succeed before charging. The validated offer includes
	// the same current resources and targets used by the mutation above.
	status.currentHp -= option.hpCost;
	if (option.moneyCost > 0) run.SpendMoney(option.moneyCost);
	if (application.moneyAward > 0) run.AddMoney(application.moneyAward);
	if (acquired) application.acquiredBallId = acquired->definitionId;
	applied.hpAfter = status.currentHp;
	applied.moneyAfter = status.money;
	result = std::move(applied);
	return true;
}
}
