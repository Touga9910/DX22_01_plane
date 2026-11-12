#include "Game.h"
#include "GameSceneDebugName.h"
#include "BallStatusJson.h"
#include "PlayerDeck.h"
#include "PlayerRunStatus.h"
#include "RunController.h"
#include "RestSiteScene.h"
#include "json/json.hpp"

#include <algorithm>

bool Game::RestHeal()
{
	RestSiteScene* restSite =
		dynamic_cast<RestSiteScene*>(GetCurrentScene());
	if (restSite != nullptr && restSite->HasUsedAction())
	{
		return false;
	}
	if (!m_RunController->CanRestHeal())
	{
		return false;
	}

	const int hpBefore = m_RunController->Status().currentHp;
	const int configuredHealAmount = m_RunController->GetRestHealAmount();
	if (!m_RunController->RestHeal())
	{
		return false;
	}
	const int actualHealAmount =
		m_RunController->Status().currentHp - hpBefore;
	if (restSite != nullptr)
	{
		restSite->MarkActionUsed();
	}
	RecordBalanceEvent(
		"rest_heal",
		{
			{ "hp_before", hpBefore },
			{ "hp_after", m_RunController->Status().currentHp },
			{ "heal_amount", actualHealAmount },
			{ "configured_heal_amount", configuredHealAmount },
			{ "heal_ratio", m_RunController->RestHealRatio() },
			{ "boss_preparation", IsBossPreparation() },
			{ "capped_at_max_hp",
				actualHealAmount < configuredHealAmount },
			{ "source_scene", GetSceneDebugName(GetCurrentScene()) },
		});
	return true;
}

// Rest Heal Amountを取得する。

int Game::GetRestHealAmount() const
{
	return m_RunController->GetRestHealAmount();
}

// Rest Heal Percentを取得する。

int Game::GetRestHealPercent() const
{
	return m_RunController->GetRestHealPercent();
}

// Clear Reward Upgrade Costを取得する。

bool Game::RestUpgradeBall(int ballIndex)
{
	RestSiteScene* restSite =
		dynamic_cast<RestSiteScene*>(GetCurrentScene());
	if (restSite != nullptr && restSite->HasUsedAction())
	{
		return false;
	}
	PlayerBallData* ball = m_RunController->Deck().GetRewardTarget(ballIndex);
	if (ball == nullptr ||
		!ball->CanUpgrade())
	{
		return false;
	}

	const std::uint64_t instanceId = ball->instanceId;
	const std::string ballId = ball->definitionId;
	const int upgradeLevelBefore = ball->upgradeLevel;
	if (!m_RunController->UpgradeBall(ballIndex))
	{
		return false;
	}
	RemoveBalanceAutoPendingBall(instanceId);
	if (restSite != nullptr)
	{
		restSite->MarkActionUsed();
	}
	RecordBalanceEvent(
		"ball_upgraded",
		{
			{ "instance_id", instanceId },
			{ "ball_id", ballId },
			{ "upgrade_level_before", upgradeLevelBefore },
			{ "upgrade_level_after", ball->upgradeLevel },
			{ "attack_after", ball->status.attack },
			{ "status_after", WritePlayerBallStatus(ball->status) },
			{ "source_scene", GetSceneDebugName(GetCurrentScene()) },
		});
	return true;
}

