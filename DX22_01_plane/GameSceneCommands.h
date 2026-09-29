#pragma once

#include "GameTypes.h"

// Validated state-changing entry points used by non-battle scenes. UI code no
// longer reaches into the Game orchestrator to mutate run state directly.
class GameSceneCommands final
{
public:
    static void ChangeScene(SceneType sceneType);
    static void StartNewRun();
    static bool LoadSavedRun();
    static void OpenDebugMode();
    static void SetSelectedAscension(int level);

    static void RollShopRelicOffers();
    static bool BuyShopRelicOffer(int index);
    static bool BuyShopBall(int index, int cost);
    static bool RemoveShopBall(int index, int cost);
    static void LeaveShop();

    static bool RestHeal();
    static bool RestUpgradeBall(int index);
    static void LeaveRestSite();

    GameSceneCommands() = delete;
};
