#include "Game.h"

#include "PlayerDeck.h"
#include "PlayerRunStatus.h"
#include "RunController.h"
#include "RunProgressConstants.h"

int Game::GetPlayerDeckCount() const
{
	return m_RunController->Deck().GetDrawPileCount();
}

int Game::GetPlayerDiscardCount() const
{
	return m_RunController->Deck().GetDiscardPileCount();
}

int Game::GetPlayerMoney() const
{
	return m_RunController->Status().money;
}

int Game::GetPlayerCurrentHp() const
{
	return m_RunController->Status().currentHp;
}

int Game::GetPlayerMaxHp() const
{
	return m_RunController->Status().maxHp;
}

bool Game::CanRestHeal() const
{
	return m_RunController->CanRestHeal();
}

int Game::GetNormalRouteAreaGoal() const
{
	return kNormalRouteAreaGoal;
}

const std::string& Game::GetSelectedStageId() const
{
	return m_RunController->Status().GetSelectedStageId();
}

int Game::GetDeckBallCount() const
{
	return m_RunController->Deck().GetRewardTargetCount();
}

int Game::GetMinimumDeckSize() const
{
	return PlayerDeck::MinimumDeckSize;
}

const PlayerBallData* Game::GetDeckBall(int index) const
{
	return m_RunController->Deck().GetRewardTarget(index);
}

const PlayerBallData* Game::GetDrawPileBall(int index) const
{
	const auto& drawPile = m_RunController->Deck().GetDrawPile();
	return index >= 0 && index < static_cast<int>(drawPile.size())
		? &drawPile[static_cast<std::size_t>(index)]
		: nullptr;
}

const PlayerBallData* Game::GetDiscardPileBall(int index) const
{
	const auto& discardPile = m_RunController->Deck().GetDiscardPile();
	return index >= 0 && index < static_cast<int>(discardPile.size())
		? &discardPile[static_cast<std::size_t>(index)]
		: nullptr;
}

const PlayerBallData* Game::GetCurrentPlayerBallData() const
{
	return m_RunController->Deck().GetCurrent();
}

bool Game::IsCurrentPlayerBallUsed() const
{
	return m_RunController->Deck().IsCurrentUsed();
}

int Game::GetShopBallCount() const
{
	return m_RunController->Deck().GetCatalogCount();
}

const PlayerBallData* Game::GetShopBall(int index) const
{
	return m_RunController->Deck().GetCatalogBall(index);
}

int Game::GetShopBallOfferCount() const
{
	return static_cast<int>(m_RunController->ShopBallOffers().size());
}

int Game::GetShopBallOfferCatalogIndex(int offerIndex) const
{
	return offerIndex >= 0 && offerIndex < GetShopBallOfferCount()
		? m_RunController->ShopBallOffers()[static_cast<std::size_t>(offerIndex)]
		: -1;
}

const PlayerBallData* Game::GetShopBallOffer(int offerIndex) const
{
	return GetShopBall(GetShopBallOfferCatalogIndex(offerIndex));
}

bool Game::HasRelic(RelicType type) const
{
	return m_RunController->HasRelic(type);
}

int Game::GetShopRelicOfferCount() const
{
	return static_cast<int>(m_RunController->ShopRelicOffers().size());
}

int Game::GetShopRelicOfferCatalogIndex(int offerIndex) const
{
	return offerIndex >= 0 && offerIndex < GetShopRelicOfferCount()
		? m_RunController->ShopRelicOffers()[static_cast<std::size_t>(offerIndex)]
		: -1;
}

const RelicDefinition* Game::GetShopRelicOffer(int offerIndex) const
{
	return GetRelic(GetShopRelicOfferCatalogIndex(offerIndex));
}

int Game::GetMidBossRelicOfferCount() const
{
	return static_cast<int>(m_RunController->MidBossRelicOffers().size());
}

int Game::GetMidBossRelicOfferCatalogIndex(int offerIndex) const
{
	return offerIndex >= 0 && offerIndex < GetMidBossRelicOfferCount()
		? m_RunController->MidBossRelicOffers()[static_cast<std::size_t>(offerIndex)]
		: -1;
}

const RelicDefinition* Game::GetMidBossRelicOffer(int offerIndex) const
{
	return GetRelic(GetMidBossRelicOfferCatalogIndex(offerIndex));
}
