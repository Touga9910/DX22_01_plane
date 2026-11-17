#pragma once
#include "RewardFieldLayout.h"
#include <string>
#include <vector>

struct PlayerBallData;
struct RelicDefinition;

struct ClearRewardBallView
{
	const PlayerBallData* ball = nullptr;
	int upgradeCost = -1;
};

struct ClearRewardPresentationModel
{
	RewardFieldLayout::Bounds fieldBounds;
	int stageRewardMoney = 0;
	int playerMoney = 0;
	int playerCurrentHp = 0;
	int playerMaxHp = 0;
	bool midBossRelicSelectionActive = false;
	bool clearRewardChosen = false; // bonus received; optional ball may still be bought
	bool rewardBallAcquired = false;
	int rewardBallCost = 25;
	int selectedRelicOfferIndex = 0;
	int selectedRewardIndex = 0;
	int selectedRewardBallIndex = 0;
	int extraMoneyAmount = 0;
	std::string rewardMessage;
	std::vector<const RelicDefinition*> relicOffers;
	std::vector<const PlayerBallData*> newBallOffers;
	std::vector<ClearRewardBallView> upgradeTargets;
};

struct ClearRewardPresentationIntent
{
	int selectedRelicOfferIndex = 0;
	int selectedRewardIndex = 0;
	int selectedRewardBallIndex = 0;
	bool confirm = false;
	bool skip = false;
	bool modalOpen = false;
};

namespace ClearRewardUi
{
    ClearRewardPresentationIntent Draw(const ClearRewardPresentationModel& model, int& pendingPurchaseOfferIndex);
}
