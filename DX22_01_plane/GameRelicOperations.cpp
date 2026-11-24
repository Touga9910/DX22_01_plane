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
/// <summary>
/// レリックが未所持で購入できるかを確認し、代金を支払って取得する。
/// </summary>
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

/// <summary>
/// 未所持のレリックを巡回中の所持品に加え、入手元を記録する。
/// </summary>
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

/// <summary>
/// 指定された件数と中ボス報酬の条件に応じて、候補となるレリックを抽選する。
/// </summary>
std::vector<int> Game::RollRelicOffers(int count, bool midBoss)
{
	return m_RunController->RollRelicOffers(
		count,
		midBoss,
		m_PersistenceRuntime->Profile());
}

// Shop Relic Offersの候補を抽選する。

/// <summary>
/// ショップに並ぶレリック候補を最大三つ抽選する。
/// </summary>
void Game::RollShopRelicOffers()
{
	m_RunController->ShopRelicOffers() = RollRelicOffers(3, false);
}

// Shop Relic Offeredかどうかを判定する。

/// <summary>
/// 指定のレリックが現在ショップで販売されているかを判定する。
/// </summary>
bool Game::IsShopRelicOffered(int relicIndex) const
{
	return std::find(
		m_RunController->ShopRelicOffers().begin(),
		m_RunController->ShopRelicOffers().end(),
		relicIndex) != m_RunController->ShopRelicOffers().end();
}

// Shop Relic Offerを購入する。

/// <summary>
/// ショップの候補番号をカタログ番号に変換し、対応するレリックを購入する。
/// </summary>
bool Game::BuyShopRelicOffer(int offerIndex)
{
	return BuyShopRelic(GetShopRelicOfferCatalogIndex(offerIndex));
}

// Shop Relicを購入する。

/// <summary>
/// ショップの販売候補に含まれるレリックに限って購入を試みる。
/// </summary>
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

/// <summary>
/// 中ボス報酬のレリックを最大三つ抽選し、候補があれば選択を開始する。
/// </summary>
void Game::RollMidBossRelicOffers()
{
	m_RunController->MidBossRelicOffers() = RollRelicOffers(3, true);
	m_RewardState->StartMidBossRelicSelection(
		!m_RunController->MidBossRelicOffers().empty());
}

// Mid Boss Relic Offerを獲得する。

/// <summary>
/// 中ボス報酬の候補番号に対応するレリックを取得する。
/// </summary>
bool Game::AcquireMidBossRelicOffer(int offerIndex)
{
	return AcquireMidBossRelic(GetMidBossRelicOfferCatalogIndex(offerIndex));
}

// Mid Boss Relicを獲得する。

/// <summary>
/// 中ボス報酬の候補であることを確認してレリックを付与し、報酬選択を終了する。
/// </summary>
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

/// <summary>
/// 所持しているレリックの数を返す。
/// </summary>
int Game::GetOwnedRelicCount() const
{
	return m_RunController->GetOwnedRelicCount();
}

// Relic Attack Bonusを取得する。

/// <summary>
/// 全球攻撃力上昇レリックによる攻撃力補正を返す。
/// </summary>
int Game::GetRelicAttackBonus() const
{
	return HasRelic(RelicType::AllBallAttackUp) ? 1 : 0;
}

// Effective Player Ball Attackを取得する。

/// <summary>
/// 指定した球の基礎攻撃力にレリックの補正を加えた値を返す。
/// </summary>
int Game::GetEffectivePlayerBallAttack(
	const PlayerBallData* ball) const
{
	return ball == nullptr
		? 0
		: ball->status.attack + GetRelicAttackBonus();
}

// Player Status Toを適用する。

/// <summary>
/// 自球に、全体効果と衝突回数に応じたレリックの攻撃補正を反映する。
/// </summary>
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

/// <summary>
/// レリック、選択中の球、発射威力から弾道予測用のショットルールを作る。
/// </summary>
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

/// <summary>
/// 現在のショットに適用されたレリックルールを取得し、使用球が未確定なら球の識別子を空にする。
/// </summary>
ShotRelicRules Game::CaptureShotRelicRules() const
{
    auto rules = m_BattleState->controller.GetShotRelicRules();
    // Live build effects require a committed deck ball, preserving shot lifecycle semantics.
    if (!m_RunController->Deck().GetCurrent()) rules.ballId.clear();
    return rules;
}

// Shot Relic Rulesを確定する。

/// <summary>
/// 指定されたレリックルールを現在のショットへ反映する。
/// </summary>
void Game::CommitShotRelicRules(const ShotRelicRules& rules)
{
    m_BattleState->controller.SetShotRelicRules(rules);
}

