#include "GameSceneCommands.h"
#include "GameView.h"
#include "RunRouteCommands.h"
#include "RunRouteView.h"

#include "Game.h"
#include "RunMap.h"
#include "json/json.hpp"

#include <utility>

namespace
{
    const char* RouteLogName(StageRouteType routeType)
    {
        switch (routeType)
        {
        case StageRouteType::NormalBattle: return "Normal Battle";
        case StageRouteType::MidBoss: return "Mid Boss";
        case StageRouteType::Shop: return "Shop";
        case StageRouteType::RestSite: return "Rest Site";
        case StageRouteType::FinalBoss: return "Final Boss";
        case StageRouteType::BossPreparation: return "Boss Preparation";
        default: return "Unknown";
        }
    }
}

const RunMap& RunRouteView::Map()
{
    return Game::GetInstance()->GetRunMap();
}

std::vector<int> RunRouteView::AvailableNodeIds()
{
    return Map().Available();
}

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

StageRouteType RunRouteView::NodeType(int nodeId)
{
    const RunMapNode* node = Map().Node(nodeId);
    return node != nullptr ? node->type : StageRouteType::NormalBattle;
}

const char* RunRouteView::LogName(StageRouteType routeType)
{
    return RouteLogName(routeType);
}

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
            { "map_version", 1 },
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
    case StageRouteType::FinalBoss:
        game.StartNextBattle(StageType::Boss);
        break;
    default:
        return false;
    }
    return game.GetRunMap().Active() == nodeId;
}

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

ShopViewSnapshot GameView::CaptureShop()
{
    const Game& game = *Game::GetInstance();
    ShopViewSnapshot view{};
    view.playerMoney = game.GetPlayerMoney();
    view.deckBallCount = game.GetDeckBallCount();
    view.shopBallCount = game.GetShopBallCount();
    view.shopRelicOfferCount = game.GetShopRelicOfferCount();
    view.ownedRelicCount = game.GetOwnedRelicCount();
    view.relicCount = game.GetRelicCount();
    view.minimumDeckSize = game.GetMinimumDeckSize();
    return view;
}

TitleViewSnapshot GameView::CaptureTitle()
{
    const Game& game = *Game::GetInstance();
    TitleViewSnapshot view{};
    view.validRunSave = game.HasValidRunSave();
    return view;
}

ResultViewSnapshot GameView::CaptureResult()
{
    ResultViewSnapshot view{};
    view.activeAscension = Game::GetInstance()->GetActiveAscension();
    return view;
}

const PlayerBallData* GameView::DeckBall(int index)
{
    return Game::GetInstance()->GetDeckBall(index);
}

const PlayerBallData* GameView::ShopBall(int index)
{
    return Game::GetInstance()->GetShopBall(index);
}

int GameView::EffectivePlayerBallAttack(const PlayerBallData* ball)
{
    return Game::GetInstance()->GetEffectivePlayerBallAttack(ball);
}

const RelicDefinition* GameView::ShopRelicOffer(int index)
{
    return Game::GetInstance()->GetShopRelicOffer(index);
}

bool GameView::IsShopRelicOfferOwned(int index)
{
    const RelicDefinition* relic = ShopRelicOffer(index);
    return relic != nullptr && Game::GetInstance()->HasRelic(relic->type);
}

const ProgressionProfile& GameView::Progression()
{
    return Game::GetInstance()->GetProgressionProfile();
}

const RunResultSnapshot& GameView::LastRunResult()
{
    return Game::GetInstance()->GetLastRunResult();
}

const std::vector<std::string>& GameView::LastProgressionUnlocks()
{
    return Game::GetInstance()->GetLastProgressionUnlocks();
}

std::string GameView::RunSaveSummary()
{
    return Game::GetInstance()->GetRunSaveSummary();
}

std::string GameView::SaveLoadMessage()
{
    return Game::GetInstance()->GetSaveLoadMessage();
}

void GameSceneCommands::ChangeScene(SceneType sceneType)
{
    Game::GetInstance()->ChangeScene(sceneType);
}

void GameSceneCommands::StartNewRun()
{
    Game::GetInstance()->StartNewRun();
}

bool GameSceneCommands::LoadSavedRun()
{
    return Game::GetInstance()->LoadSavedRun();
}

void GameSceneCommands::OpenDebugMode()
{
    Game::GetInstance()->OpenDebugMode();
}

void GameSceneCommands::SetSelectedAscension(int level)
{
    Game::GetInstance()->SetSelectedAscension(level);
}

void GameSceneCommands::RollShopRelicOffers()
{
    Game::GetInstance()->RollShopRelicOffers();
}

bool GameSceneCommands::BuyShopRelicOffer(int index)
{
    return Game::GetInstance()->BuyShopRelicOffer(index);
}

bool GameSceneCommands::BuyShopBall(int index, int cost)
{
    return Game::GetInstance()->BuyShopBall(index, cost);
}

bool GameSceneCommands::RemoveShopBall(int index, int cost)
{
    return Game::GetInstance()->RemoveShopBall(index, cost);
}

void GameSceneCommands::LeaveShop()
{
    Game::GetInstance()->LeaveShop();
}

bool GameSceneCommands::RestHeal()
{
    return Game::GetInstance()->RestHeal();
}

bool GameSceneCommands::RestUpgradeBall(int index)
{
    return Game::GetInstance()->RestUpgradeBall(index);
}

void GameSceneCommands::LeaveRestSite()
{
    Game::GetInstance()->LeaveRestSite();
}
