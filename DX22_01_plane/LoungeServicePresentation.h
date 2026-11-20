#pragma once
#include <string>
#include <vector>
struct PlayerBallData;
struct RelicDefinition;

namespace LoungeServiceUi
{
    // Presentation returns the existing scene menu's action; it never spends
    // money, upgrades a ball, or changes scene state directly.
    struct State
    {
        int pendingAction = -1;
        int pendingIndex = -1;
        int detailIndex = -1;
        int deckIndex = 0;
        bool modalOpen = false;
    };
    struct Intent { int action = -1; int index = -1; };
    struct ShopModel
    {
        int money = 0, ballPrice = 20, removePrice = 15, minimumDeck = 5;
        int selectedAction = 0, selectedBall = 0, selectedRelic = 0;
        std::vector<const PlayerBallData*> balls, deck;
        std::vector<const RelicDefinition*> relics;
        std::vector<bool> relicOwned;
        std::string message;
    };
    struct RestModel
    {
        int hp = 0, maxHp = 1, money = 0, healAmount = 0, healPercent = 0;
        bool canHeal = false, availableBenefit = false, used = false, bossPreparation = false;
        int selectedAction = 0;
        std::vector<const PlayerBallData*> deck;
        std::string message;
    };
    Intent DrawShop(const ShopModel& model, State& state);
    Intent DrawRest(const RestModel& model, State& state);
}
