#pragma once

#include "GameTypes.h"

// Validated state-changing entry points used by non-battle scenes. UI code no
// longer reaches into the Game orchestrator to mutate run state directly.
struct CollisionImpactEvent;
class Scene;
class GameSceneCommands final
{
public:
    static void ChangeScene(SceneType sceneType);
    static void StartNewRun();
    static bool LoadSavedRun();
    static bool IsCurrentScene(const Scene* scene);
    static void OpenDebugMode();
    static void OpenTitleSettings();
    static void PlayTitleCue(bool shot, float strength);
    static bool TitleShakeEnabled();
    static void PlayTitleCollision(const CollisionImpactEvent& event);
    static bool TitleEffectsEnabled();
    static int TitleEffectDensity();
    static void SetSelectedAscension(int level);

    static void RollShopBallOffers();
    static void RollShopRelicOffers();
    static bool BuyShopRelicOffer(int index);
    static bool BuyShopBallOffer(int index, int cost);
    static bool RemoveShopBall(int index, int cost);
    static void LeaveShop();

    static bool RestHeal();
    static bool RestUpgradeBall(int index);
    static void LeaveRestSite();

    GameSceneCommands() = delete;
};
