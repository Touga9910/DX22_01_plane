#include "Game.h"

#include "PlayerDeck.h"
#include "PlayerRunStatus.h"
#include "RunController.h"
#include "RunProgressConstants.h"
#include "RunProgressController.h"

/// <summary>
/// プレイヤーの山札に残っている球の数を返す。
/// </summary>
int Game::GetPlayerDeckCount() const
{
	return m_RunController->Deck().GetDrawPileCount();
}

/// <summary>
/// プレイヤーの捨て札にある球の数を返す。
/// </summary>
int Game::GetPlayerDiscardCount() const
{
	return m_RunController->Deck().GetDiscardPileCount();
}

/// <summary>
/// プレイヤーの現在の所持金を返す。
/// </summary>
int Game::GetPlayerMoney() const
{
	return m_RunController->Status().money;
}

/// <summary>
/// プレイヤーの現在のHPを返す。
/// </summary>
int Game::GetPlayerCurrentHp() const
{
	return m_RunController->Status().currentHp;
}

/// <summary>
/// プレイヤーの最大HPを返す。
/// </summary>
int Game::GetPlayerMaxHp() const
{
	return m_RunController->Status().maxHp;
}

/// <summary>
/// 休憩所でプレイヤーが回復できるかを返す。
/// </summary>
bool Game::CanRestHeal() const
{
	return m_RunController->CanRestHeal();
}

/// <summary>
/// 通常ルートの目標エリア数を返す。
/// </summary>
int Game::GetNormalRouteAreaGoal() const
{
	return kNormalRouteAreaGoal;
}

/// <summary>
/// 現在選択されているステージの識別子を返す。
/// </summary>
const std::string& Game::GetSelectedStageId() const
{
	return m_RunController->Status().GetSelectedStageId();
}

/// <summary>
/// デッキ内で報酬や強化の対象になる球の数を返す。
/// </summary>
int Game::GetDeckBallCount() const
{
	return m_RunController->Deck().GetRewardTargetCount();
}

/// <summary>
/// デッキに残す必要がある球の最小数を返す。
/// </summary>
int Game::GetMinimumDeckSize() const
{
	return PlayerDeck::MinimumDeckSize;
}

/// <summary>
/// 報酬や強化の対象になるデッキ内の球を、指定位置から取得する。
/// </summary>
const PlayerBallData* Game::GetDeckBall(int index) const
{
	return m_RunController->Deck().GetRewardTarget(index);
}

/// <summary>
/// 山札の指定位置にある球を返す。範囲外ならヌルを返す。
/// </summary>
const PlayerBallData* Game::GetDrawPileBall(int index) const
{
	const auto& drawPile = m_RunController->Deck().GetDrawPile();
	return index >= 0 && index < static_cast<int>(drawPile.size())
		? &drawPile[static_cast<std::size_t>(index)]
		: nullptr;
}

/// <summary>
/// 捨て札の指定位置にある球を返す。範囲外ならヌルを返す。
/// </summary>
const PlayerBallData* Game::GetDiscardPileBall(int index) const
{
	const auto& discardPile = m_RunController->Deck().GetDiscardPile();
	return index >= 0 && index < static_cast<int>(discardPile.size())
		? &discardPile[static_cast<std::size_t>(index)]
		: nullptr;
}

/// <summary>
/// 現在選択中のプレイヤー球のデータを返す。
/// </summary>
const PlayerBallData* Game::GetCurrentPlayerBallData() const
{
	return m_RunController->Deck().GetCurrent();
}

/// <summary>
/// 現在選択中のプレイヤー球が使用済みかを返す。
/// </summary>
bool Game::IsCurrentPlayerBallUsed() const
{
	return m_RunController->Deck().IsCurrentUsed();
}

/// <summary>
/// ショップ用カタログに登録されている球の種類数を返す。
/// </summary>
int Game::GetShopBallCount() const
{
	return m_RunController->Deck().GetCatalogCount();
}

/// <summary>
/// ショップ用カタログの指定位置にある球を返す。
/// </summary>
const PlayerBallData* Game::GetShopBall(int index) const
{
	return m_RunController->Deck().GetCatalogBall(index);
}

/// <summary>
/// 現在ショップで提示されている球の数を返す。
/// </summary>
int Game::GetShopBallOfferCount() const
{
	return static_cast<int>(m_RunController->ShopBallOffers().size());
}

/// <summary>
/// ショップで提示されている球の位置をカタログ上の位置に変換する。
/// </summary>
int Game::GetShopBallOfferCatalogIndex(int offerIndex) const
{
	return offerIndex >= 0 && offerIndex < GetShopBallOfferCount()
		? m_RunController->ShopBallOffers()[static_cast<std::size_t>(offerIndex)]
		: -1;
}

/// <summary>
/// ショップで提示されている球を、提示順の位置から取得する。
/// </summary>
const PlayerBallData* Game::GetShopBallOffer(int offerIndex) const
{
	return GetShopBall(GetShopBallOfferCatalogIndex(offerIndex));
}

/// <summary>
/// プレイヤーが指定した種類のレリックを所持しているかを返す。
/// </summary>
bool Game::HasRelic(RelicType type) const
{
	return m_RunController->HasRelic(type);
}

/// <summary>
/// 現在ショップで提示されているレリックの数を返す。
/// </summary>
int Game::GetShopRelicOfferCount() const
{
	return static_cast<int>(m_RunController->ShopRelicOffers().size());
}

/// <summary>
/// ショップで提示されているレリックの位置をカタログ上の位置に変換する。
/// </summary>
int Game::GetShopRelicOfferCatalogIndex(int offerIndex) const
{
	return offerIndex >= 0 && offerIndex < GetShopRelicOfferCount()
		? m_RunController->ShopRelicOffers()[static_cast<std::size_t>(offerIndex)]
		: -1;
}

/// <summary>
/// ショップで提示されているレリックを、提示順の位置から取得する。
/// </summary>
const RelicDefinition* Game::GetShopRelicOffer(int offerIndex) const
{
	return GetRelic(GetShopRelicOfferCatalogIndex(offerIndex));
}

/// <summary>
/// 中ボス戦の報酬として提示されているレリックの数を返す。
/// </summary>
int Game::GetMidBossRelicOfferCount() const
{
	return static_cast<int>(m_RunController->MidBossRelicOffers().size());
}

/// <summary>
/// 中ボス戦で提示されたレリックの位置をカタログ上の位置に変換する。
/// </summary>
int Game::GetMidBossRelicOfferCatalogIndex(int offerIndex) const
{
	return offerIndex >= 0 && offerIndex < GetMidBossRelicOfferCount()
		? m_RunController->MidBossRelicOffers()[static_cast<std::size_t>(offerIndex)]
		: -1;
}

/// <summary>
/// 中ボス戦の報酬レリックを、提示順の位置から取得する。
/// </summary>
const RelicDefinition* Game::GetMidBossRelicOffer(int offerIndex) const
{
	return GetRelic(GetMidBossRelicOfferCatalogIndex(offerIndex));
}

/// <summary>
/// ランで現在到達しているエリアの進行度を返す。
/// </summary>
int Game::GetAreaProgress() const
{
	return m_RunController->Progress().GetAreaProgress();
}

/// <summary>
/// 現在のランで使っているルートマップを返す。
/// </summary>
const RunMap& Game::GetRunMap() const
{
	return m_RunController->Progress().GetMap();
}

/// <summary>
/// 識別子で指定したマップのノードを選択し、成否を返す。
/// </summary>
bool Game::ChooseMapNode(int nodeId)
{
	return m_RunController->Progress().ChooseNode(nodeId);
}

/// <summary>
/// ランの現在の進行段階を返す。
/// </summary>
RunPhase Game::GetRunPhase() const
{
	return m_RunController->Progress().GetPhase();
}

/// <summary>
/// 現在のランに適用されている戦闘モードを返す。
/// </summary>
RunCombatMode Game::GetRunCombatMode() const
{
	return m_RunController->GetCombatMode();
}

/// <summary>
/// フェーズ戦闘で使用しているラン進行方式を返す。
/// </summary>
PhaseRunProgressionMode Game::GetPhaseProgressionMode() const
{
	return m_RunController->GetPhaseProgressionMode();
}

/// <summary>
/// フェーズ制の通常戦に遭遇した回数を返す。
/// </summary>
int Game::GetPhaseNormalBattleEncounterCount() const
{
	return m_RunController->Progress().GetPhaseNormalBattleEncounterCount();
}

/// <summary>
/// 直近に要求されたランの戦闘モードを返す。
/// </summary>
RunCombatMode Game::GetLastRequestedRunCombatMode() const
{
	return m_LastRequestedRunCombatMode;
}

/// <summary>
/// フェーズ制のテスト実行要求に応じて次のランの既定戦闘モードを返す。
/// </summary>
RunCombatMode Game::GetNextRunDefaultCombatMode() const
{
	return m_PhaseTestRunRequested
		? RunCombatMode::PhaseTest
		: RunCombatMode::Legacy;
}

/// <summary>
/// 現在ボス戦の準備段階にいるかを返す。
/// </summary>
bool Game::IsBossPreparation() const
{
	return m_RunController->Progress().IsBossPreparation();
}

/// <summary>
/// 最終ボスのルートに進める状態かを返す。
/// </summary>
bool Game::IsFinalBossRoute() const
{
	return m_RunController->Progress().IsFinalBossReady();
}
