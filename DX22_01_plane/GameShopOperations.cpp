#include "Game.h"
#include "PlayerDeck.h"
#include "PlayerRunStatus.h"
#include "RunController.h"
#include "json/json.hpp"

#include <algorithm>

bool Game::BuyShopBall(int catalogIndex, int cost)
{
	cost = (std::max)(0, cost);
	const PlayerBallData* catalogBall =
		m_RunController->Deck().GetCatalogBall(catalogIndex);
	const std::string ballId =
		catalogBall != nullptr ? catalogBall->definitionId : std::string();
	const int moneyBefore = m_RunController->Status().money;
	if (!m_RunController->BuyShopBall(catalogIndex, cost))
	{
		return false;
	}

	PublishGameEvent(BallAcquiredEvent{ ballId });
	RecordBalanceEvent(
		"shop_ball_purchased",
		{
			{ "catalog_index", catalogIndex },
			{ "ball_id", ballId },
			{ "cost", cost },
			{ "money_before", moneyBefore },
			{ "money_after", m_RunController->Status().money },
		});
	return true;
}

// Shop入店中に提示するBallを、重複なしで最大3枠抽選する。

void Game::RollShopBallOffers()
{
	m_RunController->ShopBallOffers() =
		m_RunController->Deck().RollDistinctCatalogOfferIndices(kShopBallOfferSize);
}

// Shopの提示位置から対応するCatalog Ballを購入する。

bool Game::BuyShopBallOffer(int offerIndex, int cost)
{
	return BuyShopBall(GetShopBallOfferCatalogIndex(offerIndex), cost);
}

// Shop Ballを取り除く。

bool Game::RemoveShopBall(int ballIndex, int cost)
{
	cost = (std::max)(0, cost);
	if (m_RunController->Status().money < cost ||
		m_RunController->Deck().GetRewardTargetCount() <=
			PlayerDeck::MinimumDeckSize)
	{
		return false;
	}

	const PlayerBallData* ball =
		m_RunController->Deck().GetRewardTarget(ballIndex);
	if (ball == nullptr)
	{
		return false;
	}
	const std::uint64_t instanceId = ball->instanceId;
	const std::string ballId = ball->definitionId;
	const int moneyBefore = m_RunController->Status().money;

	if (!m_RunController->RemoveShopBall(ballIndex, cost))
	{
		return false;
	}

	RemoveBalanceAutoPendingBall(instanceId);
	RecordBalanceEvent(
		"shop_ball_removed",
		{
			{ "instance_id", instanceId },
			{ "ball_id", ballId },
			{ "cost", cost },
			{ "money_before", moneyBefore },
			{ "money_after", m_RunController->Status().money },
			{ "deck_count_after", m_RunController->Deck().GetRewardTargetCount() },
		});
	return true;
}

