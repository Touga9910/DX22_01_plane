#include "GameSceneCommands.h"
#include "GameView.h"
#include "RunRouteCommands.h"
#include "RunRouteView.h"

#include "Game.h"
#include "AudioFeedback.h"
#include "GameEvent.h"
#include "RunMap.h"
#include "StageData.h"
#include "json/json.hpp"

#include <utility>

namespace
{
    /// <summary>
    /// ルートの種別を計測ログで用いる表示名へ変換する。
    /// </summary>
    const char* RouteLogName(StageRouteType routeType)
    {
        switch (routeType)
        {
        case StageRouteType::NormalBattle: return "Normal Battle";
        case StageRouteType::MidBoss: return "Mid Boss";
        case StageRouteType::Shop: return "Shop";
        case StageRouteType::RestSite: return "Rest Site";
        case StageRouteType::Event: return "Event";
        case StageRouteType::FinalBoss: return "Final Boss";
        case StageRouteType::BossPreparation: return "Boss Preparation";
        default: return "Unknown";
        }
    }
}

/// <summary>
/// 実行中のランで使用しているルートマップを返す。
/// </summary>
const RunMap& RunRouteView::Map()
{
    return Game::GetInstance()->GetRunMap();
}

/// <summary>
/// 現在選択できるマップノードの識別子一覧を返す。
/// </summary>
std::vector<int> RunRouteView::AvailableNodeIds()
{
    return Map().Available();
}

/// <summary>
/// ルート画面で表示する進行度、戦闘モード、ボスの状況などを取得する。
/// </summary>
RunRouteStatusSnapshot RunRouteView::CaptureStatus()
{
    const Game& game = *Game::GetInstance();
    RunRouteStatusSnapshot view{};
    view.areaProgress = game.GetAreaProgress();
    view.normalRouteAreaGoal = game.GetNormalRouteAreaGoal();
    view.playerCurrentHp = game.GetPlayerCurrentHp();
    view.playerMaxHp = game.GetPlayerMaxHp();
    view.playerMoney = game.GetPlayerMoney();
    view.deckBallCount = game.GetDeckBallCount();
    return view;
}

/// <summary>
/// 指定したマップノードのルート種別を返す。
/// </summary>
StageRouteType RunRouteView::NodeType(int nodeId)
{
    const RunMapNode* node = Map().Node(nodeId);
    return node != nullptr ? node->type : StageRouteType::NormalBattle;
}

/// <summary>
/// 指定したルート種別のログ用名称を返す。
/// </summary>
const char* RunRouteView::LogName(StageRouteType routeType)
{
    return RouteLogName(routeType);
}

/// <summary>
/// 選択したルートノードを検証し、ルート種別に応じた次の場面への移動を進める。
/// </summary>
bool RunRouteCommands::Choose(
    int nodeId,
    int selectedIndex,
    const std::vector<int>& offeredNodeIds,
    const std::string& controllerType)
{
    Game& game = *Game::GetInstance();
    const RunMapNode* selectedNode = game.GetRunMap().Node(nodeId);
    if (selectedNode == nullptr)
    {
        return false;
    }

    const StageRouteType routeType = selectedNode->type;
    nlohmann::json offeredRoutes = nlohmann::json::array();
    for (const int offeredNodeId : offeredNodeIds)
    {
        const RunMapNode* offeredNode = game.GetRunMap().Node(offeredNodeId);
        offeredRoutes.push_back(
            offeredNode != nullptr ? RouteLogName(offeredNode->type) : "Unknown");
    }

    if (!game.ChooseMapNode(nodeId))
    {
        return false;
    }
    game.RecordBalanceEvent(
        "route_choice",
        {
            { "controller", controllerType },
            { "offered_routes", std::move(offeredRoutes) },
            { "selected_index", selectedIndex },
            { "map_node_id", nodeId },
            { "map_path", game.GetRunMap().Path() },
            { "map_version", game.GetRunMap().Version() },
            { "selected_route", RouteLogName(routeType) },
            { "area_progress", game.GetAreaProgress() },
            { "run_phase", ToString(game.GetRunPhase()) },
        });

    switch (routeType)
    {
    case StageRouteType::NormalBattle:
        game.StartNextBattle(StageType::Normal);
        break;
    case StageRouteType::MidBoss:
        game.StartNextBattle(StageType::MidBoss);
        break;
    case StageRouteType::Shop:
        game.ChangeScene(SceneType::Shop);
        break;
    case StageRouteType::RestSite:
        game.ChangeScene(SceneType::RestSite);
        break;
    case StageRouteType::Event:
        game.ChangeScene(SceneType::Event);
        break;
    case StageRouteType::FinalBoss:
        game.StartNextBattle(StageType::Boss);
        break;
    default:
        return false;
    }
    return game.GetRunMap().Active() == nodeId;
}

/// <summary>
/// 現在のHPや強化候補など、休憩所の表示に必要な状態を取得する。
/// </summary>
RestSiteViewSnapshot GameView::CaptureRestSite()
{
    const Game& game = *Game::GetInstance();
    RestSiteViewSnapshot view{};
    view.playerCurrentHp = game.GetPlayerCurrentHp();
    view.playerMaxHp = game.GetPlayerMaxHp();
    view.deckBallCount = game.GetDeckBallCount();
    view.restHealAmount = game.GetRestHealAmount();
    view.restHealPercent = game.GetRestHealPercent();
    view.bossPreparation = game.IsBossPreparation();
    view.canRestHeal = game.CanRestHeal();
    view.availableRestBenefit = game.HasAvailableRestBenefit();
    return view;
}

RunEventViewSnapshot GameView::CaptureRunEvent()
{
    return Game::GetInstance()->CaptureRunEvent();
}

bool GameSceneCommands::ChooseRunEvent(int choiceIndex, int targetIndex)
{
    return Game::GetInstance()->ChooseRunEvent(choiceIndex, targetIndex);
}

/// <summary>
/// 所持金や提示中の商品など、ショップの表示に必要な状態を取得する。
/// </summary>
ShopViewSnapshot GameView::CaptureShop()
{
    const Game& game = *Game::GetInstance();
    ShopViewSnapshot view{};
    view.playerMoney = game.GetPlayerMoney();
    view.deckBallCount = game.GetDeckBallCount();
    view.shopBallOfferCount = game.GetShopBallOfferCount();
    view.shopRelicOfferCount = game.GetShopRelicOfferCount();
    view.ownedRelicCount = game.GetOwnedRelicCount();
    view.relicCount = game.GetRelicCount();
    view.minimumDeckSize = game.GetMinimumDeckSize();
    return view;
}

/// <summary>
/// セーブの有無など、タイトル画面の表示に必要な状態を取得する。
/// </summary>
TitleViewSnapshot GameView::CaptureTitle()
{
    const Game& game = *Game::GetInstance();
    TitleViewSnapshot view{};
    view.validRunSave = game.HasValidRunSave();
    view.transitionTiming = game.GetSceneTransitionTiming();
    return view;
}

/// <summary>
/// 結果画面で表示する現在のアセンション段階を取得する。
/// </summary>
ResultViewSnapshot GameView::CaptureResult()
{
    ResultViewSnapshot view{};
    view.activeAscension = Game::GetInstance()->GetActiveAscension();
    return view;
}

/// <summary>
/// デッキの指定位置にある球を表示用に取得する。
/// </summary>
const PlayerBallData* GameView::DeckBall(int index)
{
    return Game::GetInstance()->GetDeckBall(index);
}

/// <summary>
/// ショップ用カタログの指定位置にある球を表示用に取得する。
/// </summary>
const PlayerBallData* GameView::ShopBall(int index)
{
    return Game::GetInstance()->GetShopBall(index);
}

/// <summary>
/// ショップで提示された指定位置の球を取得する。
/// </summary>
const PlayerBallData* GameView::ShopBallOffer(int index)
{
    return Game::GetInstance()->GetShopBallOffer(index);
}

/// <summary>
/// 装備条件や補正を適用した指定球の実効攻撃力を取得する。
/// </summary>
int GameView::EffectivePlayerBallAttack(const PlayerBallData* ball)
{
    return Game::GetInstance()->GetEffectivePlayerBallAttack(ball);
}

/// <summary>
/// ショップで提示された指定位置のレリックを取得する。
/// </summary>
const RelicDefinition* GameView::ShopRelicOffer(int index)
{
    return Game::GetInstance()->GetShopRelicOffer(index);
}

/// <summary>
/// ショップで提示された指定位置のレリックをすでに所持しているかを返す。
/// </summary>
bool GameView::IsShopRelicOfferOwned(int index)
{
    const RelicDefinition* relic = ShopRelicOffer(index);
    return relic != nullptr && Game::GetInstance()->HasRelic(relic->type);
}

/// <summary>
/// 永続的な進行状態を画面表示用に取得する。
/// </summary>
const ProgressionProfile& GameView::Progression()
{
    return Game::GetInstance()->GetProgressionProfile();
}

/// <summary>
/// 直近に終了したランの結果を取得する。
/// </summary>
const RunResultSnapshot& GameView::LastRunResult()
{
    return Game::GetInstance()->GetLastRunResult();
}

/// <summary>
/// 直近の進行更新で解放された要素の識別子一覧を取得する。
/// </summary>
const std::vector<std::string>& GameView::LastProgressionUnlocks()
{
    return Game::GetInstance()->GetLastProgressionUnlocks();
}

/// <summary>
/// セーブデータの表示用概要を取得する。
/// </summary>
std::string GameView::RunSaveSummary()
{
    return Game::GetInstance()->GetRunSaveSummary();
}

/// <summary>
/// セーブやロードの結果を伝える表示用メッセージを取得する。
/// </summary>
std::string GameView::SaveLoadMessage()
{
    return Game::GetInstance()->GetSaveLoadMessage();
}

/// <summary>
/// 指定した種別のシーンへ切り替える。
/// </summary>
void GameSceneCommands::ChangeScene(SceneType sceneType)
{
    Game::GetInstance()->ChangeScene(sceneType);
}

/// <summary>
/// ゲーム側で新しいランを開始する。
/// </summary>
void GameSceneCommands::StartNewRun()
{
    Game::GetInstance()->StartNewRun();
}

/// <summary>
/// 画面遷移のフェードを伴ってセーブ済みのランを読み込む。
/// </summary>
bool GameSceneCommands::LoadSavedRun()
{
    return Game::GetInstance()->LoadSavedRunWithFade();
}

/// <summary>
/// ゲームのデバッグモードを開く。
/// </summary>
void GameSceneCommands::OpenDebugMode()
{
    Game::GetInstance()->OpenDebugMode();
}

/// <summary>
/// 次のランで使用するアセンション段階の変更を要求する。
/// </summary>
void GameSceneCommands::SetSelectedAscension(int level)
{
    Game::GetInstance()->SetSelectedAscension(level);
}

/// <summary>
/// ショップで提示するレリックの候補を抽選する。
/// </summary>
void GameSceneCommands::RollShopRelicOffers()
{
    Game::GetInstance()->RollShopRelicOffers();
}

/// <summary>
/// ショップで提示する球の候補を抽選する。
/// </summary>
void GameSceneCommands::RollShopBallOffers()
{
    Game::GetInstance()->RollShopBallOffers();
}

/// <summary>
/// ショップで提示された指定位置のレリックを購入する。
/// </summary>
bool GameSceneCommands::BuyShopRelicOffer(int index)
{
    return Game::GetInstance()->BuyShopRelicOffer(index);
}

/// <summary>
/// ショップで提示された指定位置の球を、指定価格で購入する。
/// </summary>
bool GameSceneCommands::BuyShopBallOffer(int index, int cost)
{
    return Game::GetInstance()->BuyShopBallOffer(index, cost);
}

/// <summary>
/// 指定価格を支払って、指定位置の球をデッキから除外する。
/// </summary>
bool GameSceneCommands::RemoveShopBall(int index, int cost)
{
    return Game::GetInstance()->RemoveShopBall(index, cost);
}

/// <summary>
/// ショップを出てランの進行を再開する。
/// </summary>
void GameSceneCommands::LeaveShop()
{
    Game::GetInstance()->LeaveShop();
}

/// <summary>
/// 休憩所でプレイヤーのHPを回復し、成否を返す。
/// </summary>
bool GameSceneCommands::RestHeal()
{
    return Game::GetInstance()->RestHeal();
}

/// <summary>
/// 休憩所でデッキの指定位置にある球を強化し、成否を返す。
/// </summary>
bool GameSceneCommands::RestUpgradeBall(int index)
{
    return Game::GetInstance()->RestUpgradeBall(index);
}

/// <summary>
/// 休憩所を出てランの進行を再開する。
/// </summary>
void GameSceneCommands::LeaveRestSite()
{
    Game::GetInstance()->LeaveRestSite();
}

/// <summary>
/// タイトル画面の設定を開くため、一時停止表示の切り替えを要求する。
/// </summary>
void GameSceneCommands::OpenTitleSettings()
{
    Game::GetInstance()->RequestPauseToggle();
}

/// <summary>
/// タイトル演出に応じて、効果音のショット開始処理または衝撃通知を呼び出す。
/// </summary>
void Game::PlayTitleCue(bool shot, float strength)
{
    if(!m_AudioFeedback)return;
    if(shot)m_AudioFeedback->OnShotFired(ShotFiredEvent{"player_standard",0});
    else {CollisionImpactEvent event{};event.relativeApproachSpeed=strength;m_AudioFeedback->OnCollisionImpact(event,0);}
}
/// <summary>
/// タイトル演出のショット開始または衝撃の通知をゲーム側に渡す。
/// </summary>
void GameSceneCommands::PlayTitleCue(bool shot, float strength)
{
    Game::GetInstance()->PlayTitleCue(shot,strength);
}

/// <summary>
/// タイトル演出にも適用するカメラの揺れが有効かを返す。
/// </summary>
bool GameSceneCommands::TitleShakeEnabled()
{
    return Game::GetInstance()->IsCameraShakeEnabled();
}

/// <summary>
/// タイトル演出の衝突を種別に応じた効果音のイベントへ変換する。
/// </summary>
void Game::PlayTitleCollision(const CollisionImpactEvent& event)
{
    if(!m_AudioFeedback)return;
    if(event.collisionKind==CombatCollisionKind::Wall)
        m_AudioFeedback->OnWallImpact(WallImpactEvent{event.worldPosition,event.relativeApproachSpeed,0});
    else m_AudioFeedback->OnCollisionImpact(event,0);
}
/// <summary>
/// タイトル演出で発生した衝突の効果音をゲーム側に要求する。
/// </summary>
void GameSceneCommands::PlayTitleCollision(const CollisionImpactEvent& event)
{ Game::GetInstance()->PlayTitleCollision(event); }
/// <summary>
/// タイトル演出にも適用する衝突エフェクトが有効かを返す。
/// </summary>
bool GameSceneCommands::TitleEffectsEnabled()
{ return Game::GetInstance()->IsCollisionImpactEffectsEnabled(); }
/// <summary>
/// タイトル演出の装飾リングに適用する密度の設定値を返す。
/// </summary>
int GameSceneCommands::TitleEffectDensity()
{ return Game::GetInstance()->GetDecorativeRingAmount(); }

/// <summary>
/// 指定したシーンが現在実行中のシーンかを返す。
/// </summary>
bool GameSceneCommands::IsCurrentScene(const Scene* scene)
{
    return Game::GetInstance()->GetCurrentScene()==scene;
}
