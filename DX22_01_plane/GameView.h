#pragma once

#include <string>
#include <vector>

struct PlayerBallData;
class ProgressionProfile;
struct RelicDefinition;
struct RunResultSnapshot;

// Each UI scene receives only the immutable values it needs. Keeping these
// views separate also prevents expensive queries (such as save inspection)
// from leaking into unrelated per-frame paths.
struct RestSiteViewSnapshot
{
    int playerCurrentHp = 0;
    int playerMaxHp = 0;
    int deckBallCount = 0;
    int restHealAmount = 0;
    int restHealPercent = 0;
    bool bossPreparation = false;
    bool canRestHeal = false;
    bool availableRestBenefit = false;
};

struct ShopViewSnapshot
{
    int playerMoney = 0;
    int deckBallCount = 0;
    int shopBallCount = 0;
    int shopRelicOfferCount = 0;
    int ownedRelicCount = 0;
    int relicCount = 0;
    int minimumDeckSize = 0;
};

struct TitleViewSnapshot
{
    bool validRunSave = false;
};

struct ResultViewSnapshot
{
    int activeAscension = 0;
};

class GameView final
{
public:
    static RestSiteViewSnapshot CaptureRestSite();
    static ShopViewSnapshot CaptureShop();
    static TitleViewSnapshot CaptureTitle();
    static ResultViewSnapshot CaptureResult();

    static const PlayerBallData* DeckBall(int index);
    static const PlayerBallData* ShopBall(int index);
    static int EffectivePlayerBallAttack(const PlayerBallData* ball);
    static const RelicDefinition* ShopRelicOffer(int index);
    static bool IsShopRelicOfferOwned(int index);

    static const ProgressionProfile& Progression();
    static const RunResultSnapshot& LastRunResult();
    static const std::vector<std::string>& LastProgressionUnlocks();
    static std::string RunSaveSummary();
    static std::string SaveLoadMessage();

    GameView() = delete;
};
