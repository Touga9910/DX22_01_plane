#include "Game.h"
#include "GameRuntime.h"
#include "GameWorld.h"
#include "GameBattleState.h"
#include "GamePersistenceRuntime.h"
#include "GameRewardState.h"
#include "PlayerDeck.h"
#include "PlayerRunStatus.h"
#include "RunController.h"
#include "ShotRelicRules.h"
#include "PlayerBall.h"
#include "json/json.hpp"

#include <algorithm>
bool Game::BuyRelic(int relicIndex)
{
	const RelicDefinition* relic = GetRelic(relicIndex);
	if (relic == nullptr || HasRelic(relic->type))
	{
		return false;
	}

	const int cost = (std::max)(0, relic->price);
	const int moneyBefore = m_RunController->Status().money;
	if (!m_RunController->BuyRelic(relicIndex))
	{
		return false;
	}
	for (PlayerBall* player : GameRuntime::World().GetComponents<PlayerBall>())
	{
		ApplyRelicModifiersTo(player);
	}
	RecordBalanceEvent(
		"relic_acquired",
		{
			{ "relic_index", relicIndex },
			{ "relic_name", relic->name },
			{ "rarity", ToString(relic->rarity) },
			{ "source", "shop" },
		});
	RecordBalanceEvent(
		"relic_purchased",
		{
			{ "relic_index", relicIndex },
			{ "relic_name", relic->name },
			{ "cost", cost },
			{ "money_before", moneyBefore },
			{ "money_after", m_RunController->Status().money },
		});

	return true;
}

// Relicを付与する。

bool Game::GrantRelic(int relicIndex, const char* source)
{
	const RelicDefinition* relic = GetRelic(relicIndex);
	if (relic == nullptr || HasRelic(relic->type))
	{
		return false;
	}

	if (!m_RunController->GrantRelic(relicIndex))
	{
		return false;
	}
	for (PlayerBall* player : GameRuntime::World().GetComponents<PlayerBall>())
	{
		ApplyRelicModifiersTo(player);
	}
	RecordBalanceEvent(
		"relic_acquired",
		{
			{ "relic_index", relicIndex },
			{ "relic_name", relic->name },
			{ "rarity", ToString(relic->rarity) },
			{ "source", source != nullptr ? source : "unknown" },
		});
	return true;
}

// Relic Offersの候補を抽選する。

std::vector<int> Game::RollRelicOffers(int count, bool midBoss)
{
	return m_RunController->RollRelicOffers(
		count,
		midBoss,
		m_PersistenceRuntime->Profile());
}

// Shop Relic Offersの候補を抽選する。

void Game::RollShopRelicOffers()
{
	m_RunController->ShopRelicOffers() = RollRelicOffers(3, false);
}

// Shop Relic Offeredかどうかを判定する。

bool Game::IsShopRelicOffered(int relicIndex) const
{
	return std::find(
		m_RunController->ShopRelicOffers().begin(),
		m_RunController->ShopRelicOffers().end(),
		relicIndex) != m_RunController->ShopRelicOffers().end();
}

// Shop Relic Offerを購入する。

bool Game::BuyShopRelicOffer(int offerIndex)
{
	return BuyShopRelic(GetShopRelicOfferCatalogIndex(offerIndex));
}

// Shop Relicを購入する。

bool Game::BuyShopRelic(int relicIndex)
{
	if (!IsShopRelicOffered(relicIndex))
	{
		return false;
	}
	if (!BuyRelic(relicIndex))
	{
		return false;
	}
	return true;
}

// Mid Boss Relic Offersの候補を抽選する。

void Game::RollMidBossRelicOffers()
{
	m_RunController->MidBossRelicOffers() = RollRelicOffers(3, true);
	m_RewardState->StartMidBossRelicSelection(
		!m_RunController->MidBossRelicOffers().empty());
}

// Mid Boss Relic Offerを獲得する。

bool Game::AcquireMidBossRelicOffer(int offerIndex)
{
	return AcquireMidBossRelic(GetMidBossRelicOfferCatalogIndex(offerIndex));
}

// Mid Boss Relicを獲得する。

bool Game::AcquireMidBossRelic(int relicIndex)
{
	if (!m_RewardState->IsMidBossRelicSelectionActive() ||
		std::find(m_RunController->MidBossRelicOffers().begin(),
			m_RunController->MidBossRelicOffers().end(), relicIndex) ==
			m_RunController->MidBossRelicOffers().end() ||
		!GrantRelic(relicIndex, "midboss"))
	{
		return false;
	}
	m_RewardState->FinishMidBossRelicSelection();
	return true;
}

// Owned Relic Countを取得する。

int Game::GetOwnedRelicCount() const
{
	return m_RunController->GetOwnedRelicCount();
}

// Relic Attack Bonusを取得する。

int Game::GetRelicAttackBonus() const
{
	return HasRelic(RelicType::AllBallAttackUp) ? 1 : 0;
}

// Effective Player Ball Attackを取得する。

int Game::GetEffectivePlayerBallAttack(
	const PlayerBallData* ball) const
{
	return ball == nullptr
		? 0
		: ball->status.attack + GetRelicAttackBonus();
}

// Player Status Toを適用する。

void Game::ApplyRelicModifiersTo(PlayerBall* player)
{
	if (player == nullptr || player->GetBall() == nullptr)
	{
		return;
	}

	const int collisionBonus =
		HasRelic(RelicType::CollisionAttackUp)
			? m_BattleState->controller.GetShotRelicRules().collisionBonus
			: 0;
	player->GetBall()->SetCombatModifiers(
		GetRelicAttackBonus() + collisionBonus,
		0);
}

// Shot Relic Stateを初期状態へ戻す。

ShotRelicRules Game::MakePredictionShotRules(float launchPower) const
{
    ShotRelicRules rules;
    rules.relics = m_RunController->Relics();
    const auto* ball = m_RunController->Deck().GetCurrent();
    if (ball == nullptr)
        ball = m_RunController->Deck().GetOffer(GetSelectedBallOfferIndex());
    if (ball != nullptr) rules.ballId = ball->definitionId;
    rules.launchPower = launchPower;
    return rules;
}

// Shot Relic Rulesを取得して保持する。

ShotRelicRules Game::CaptureShotRelicRules() const
{
    auto rules = m_BattleState->controller.GetShotRelicRules();
    // Live build effects require a committed deck ball, preserving shot lifecycle semantics.
    if (!m_RunController->Deck().GetCurrent()) rules.ballId.clear();
    return rules;
}

// Shot Relic Rulesを確定する。

void Game::CommitShotRelicRules(const ShotRelicRules& rules)
{
    m_BattleState->controller.SetShotRelicRules(rules);
}

