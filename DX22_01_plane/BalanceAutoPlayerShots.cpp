#include "BalanceAutoPlayer.h"
#include "BalanceAutoPlayerPolicy.h"
#include "Game.h"
#include "GameRuntime.h"
#include "GameWorld.h"
#include "PlayerBall.h"
#include "EnemyBall.h"
#include "BallComponent.h"
#include "PlayerBallData.h"
#include "Pocket.h"
#include "Collision.h"
#include "json/json.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <vector>

#pragma execution_character_set("utf-8")

using namespace BalanceAutoPolicy;

namespace
{
struct AutoShotTarget
{
	EnemyBall* bestTarget = nullptr;
	DirectX::SimpleMath::Vector3 bestDirection = DirectX::SimpleMath::Vector3::UnitZ;
	float bestDistance = 0.0f;
	float bestPocketRisk = 0.0f;
	bool bestTargetIsKillable = false;
};

AutoShotTarget FindAutoShotTarget(
	PlayerBall* player,
	const std::vector<EnemyBall*>& enemies,
	const std::vector<Pocket*>& pockets)
{
	const DirectX::SimpleMath::Vector3 playerPosition =
		player->GetPosition();

	EnemyBall* bestTarget = nullptr;
	DirectX::SimpleMath::Vector3 bestDirection =
		DirectX::SimpleMath::Vector3::UnitZ;
	float bestDistance = 0.0f;
	float bestPocketRisk = 0.0f;
	bool bestTargetIsKillable = false;
	float bestTargetScore =
		-(std::numeric_limits<float>::max)();
	const float playerRadius = player->GetBall() == nullptr
		? 0.0f
		: player->GetBall()->GetRadius();

	for (EnemyBall* target : enemies)
	{
		if (target == nullptr || !target->CanParticipateInCombat())
		{
			continue;
		}

		DirectX::SimpleMath::Vector3 direction =
			target->GetPosition() - playerPosition;
		direction.y = 0.0f;
		const float distance = direction.Length();
		if (distance <= 0.0001f)
		{
			continue;
		}
		direction /= distance;

		float chainScore = 0.0f;
		for (EnemyBall* other : enemies)
		{
			if (other == nullptr ||
				other == target ||
				!other->CanParticipateInCombat())
			{
				continue;
			}

			DirectX::SimpleMath::Vector3 relative =
				other->GetPosition() - target->GetPosition();
			relative.y = 0.0f;
			const float forward =
				relative.Dot(direction);
			if (forward <= 0.0f)
			{
				continue;
			}

			const float lateralSquared =
				(std::max)(
					0.0f,
					relative.LengthSquared() -
					forward * forward);
			const float targetRadius =
				target->GetBall() == nullptr
				? 0.0f
				: target->GetBall()->GetRadius();
			const float otherRadius =
				other->GetBall() == nullptr
				? 0.0f
				: other->GetBall()->GetRadius();
			const float chainWidth =
				targetRadius + otherRadius + 1.5f;

			if (lateralSquared <= chainWidth * chainWidth)
			{
				chainScore +=
					20.0f / (1.0f + forward * 0.05f);
			}
		}

		float pocketRisk = 0.0f;
		for (Pocket* pocket : pockets)
		{
			if (pocket == nullptr)
			{
				continue;
			}

			const Collision::Sphere pocketSphere = pocket->GetSphere();
			DirectX::SimpleMath::Vector3 relative =
				pocketSphere.center - playerPosition;
			relative.y = 0.0f;
			const float forward = relative.Dot(direction);
			if (forward <= 0.0f)
			{
				continue;
			}

			const float lateralSquared = (std::max)(
				0.0f,
				relative.LengthSquared() - forward * forward);
			const float corridorRadius =
				pocketSphere.radius + playerRadius * 0.65f;
			if (lateralSquared > corridorRadius * corridorRadius)
			{
				continue;
			}

			if (forward <= distance + playerRadius * 2.0f)
			{
				pocketRisk += 100.0f;
			}
			else if (forward <= distance + 30.0f)
			{
				pocketRisk += 160.0f;
			}
		}

		const bool isKillable =
			target->GetHP() <= player->GetAttack();
		const int missingHp = (std::max)(
			0,
			target->GetMaxHP() - target->GetHP());
		const float targetScore =
			chainScore -
			distance * 0.10f -
			pocketRisk +
			static_cast<float>(target->GetAttack()) * 8.0f +
			static_cast<float>(missingHp) * 10.0f +
			(isKillable ? 180.0f : 0.0f);
		if (targetScore > bestTargetScore)
		{
			bestTargetScore = targetScore;
			bestTarget = target;
			bestDirection = direction;
			bestDistance = distance;
			bestPocketRisk = pocketRisk;
			bestTargetIsKillable = isKillable;
		}
	}

	return { bestTarget, bestDirection, bestDistance, bestPocketRisk, bestTargetIsKillable };
}
}

void BalanceAutoPlayer::SelectBall(Game& game)
{
    const auto bossChoices = game.EvaluateBossShots();
    if (bossChoices.contains("recommended") && !bossChoices["recommended"].is_null())
    {
		game.ApplyBalanceAutoBallSelection(
			bossChoices["recommended"]["offer_index"].get<int>());
        return;
    }
	const int offerCount = game.GetBallOfferCount();
	if (offerCount <= 0)
	{
		return;
	}

	int bestIndex = 0;
	float bestScore =
		-(std::numeric_limits<float>::max)();
	for (int index = 0; index < offerCount; index++)
	{
		const PlayerBallData* ball =
			game.GetBallOffer(index);
		if (ball == nullptr)
		{
			continue;
		}

		float score = GetAutoBallValue(*ball);
		score += (std::max)(0.0f, ball->status.mass - 2.0f);
		score -= static_cast<float>(
			game.GetBallShotCount(ball->definitionId)) * 1.5f;

		if (score > bestScore)
		{
			bestScore = score;
			bestIndex = index;
		}
	}

	game.ApplyBalanceAutoBallSelection(bestIndex);
}

bool BalanceAutoPlayer::FireShot(Game& game)
{
    const auto bossChoices = game.EvaluateBossShots();
    if (bossChoices.contains("recommended") && !bossChoices["recommended"].is_null())
        return game.FireBossPlannedShot(bossChoices["recommended"]["candidate_id"].get<std::string>(),
            bossChoices["state_key"].get<std::string>());
	std::vector<PlayerBall*> players =
		GameRuntime::World().GetComponents<PlayerBall>();
	std::vector<EnemyBall*> enemies =
		GameRuntime::World().GetComponents<EnemyBall>();
	std::vector<Pocket*> pockets =
		GameRuntime::World().GetComponents<Pocket>();

	if (players.empty() || players[0] == nullptr)
	{
		return false;
	}

	PlayerBall* player = players[0];
	const AutoShotTarget targetSelection = FindAutoShotTarget(player, enemies, pockets);
	if (targetSelection.bestTarget == nullptr)
	{
		if (game.AreAllEnemiesDefeated())
		{
			m_DecisionFrame = 0;
			return false;
		}
		game.RecordBalanceEvent(
			"autoplay_waiting",
			{
				{ "reason", "no_live_target" },
				{ "battle_state", ToString(game.GetBattleState()) },
			});
		return false;
	}

	constexpr float DegreesToRadians =
		3.14159265358979323846f / 180.0f;
	const bool needsSafeShot =
		targetSelection.bestPocketRisk > 0.0f ||
		game.GetPlayerCurrentHp() * 2 <= game.GetPlayerMaxHp();
	const float appliedJitterDegrees = needsSafeShot
		? m_AimJitterDegrees * 0.5f
		: m_AimJitterDegrees;
	std::uniform_real_distribution<float> jitterDistribution(
		-appliedJitterDegrees,
		appliedJitterDegrees);
	const float jitter =
		jitterDistribution(m_RandomEngine) *
		DegreesToRadians;
	const float cosine = std::cos(jitter);
	const float sine = std::sin(jitter);
	const DirectX::SimpleMath::Vector3 shotDirection(
		targetSelection.bestDirection.x * cosine +
			targetSelection.bestDirection.z * sine,
		0.0f,
		-targetSelection.bestDirection.x * sine +
			targetSelection.bestDirection.z * cosine);

	const float safeMaximumPower = (std::max)(
		m_MinimumShotPower,
		needsSafeShot
		? (std::min)(m_MaximumShotPower, 5.5f)
		: m_MaximumShotPower);
	const float shotPower = std::clamp(
		3.0f + targetSelection.bestDistance * 0.03f,
		m_MinimumShotPower,
		safeMaximumPower);

	std::cout
		<< "[BalanceAutoPlay] Target="
		<< targetSelection.bestTarget->GetEnemyId()
		<< " Power=" << shotPower
		<< " Killable=" << (targetSelection.bestTargetIsKillable ? "yes" : "no")
		<< " PocketRisk=" << targetSelection.bestPocketRisk
		<< std::endl;

	return game.TryFireAutomatedShot(shotDirection * shotPower);
}
