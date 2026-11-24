#include "GamePresentation.h"
#include "Game.h"
#include "GameRuntime.h"
#include "GameWorld.h"
#include "EnemyBall.h"

#pragma execution_character_set("utf-8")

#include <algorithm>

using DirectX::SimpleMath::Vector3;

namespace
{
	/// <summary>
	/// 識別キーに対応する、現在ワールドに登録されている敵コンポーネントを探す。
	/// </summary>
	EnemyBall* FindLiveHitTarget(std::uintptr_t key)
	{
		if (key == 0) return nullptr;
		EnemyBall* target = nullptr;
		// Resolve from owned, typed components; never cast/dereference a stale key.
		GameRuntime::World().ForEachComponent<EnemyBall>([&](EnemyBall* enemy)
		{
			if (reinterpret_cast<std::uintptr_t>(enemy) == key) target = enemy;
		});
		return target;
	}
}

/// <summary>
/// 登録中の被弾演出を消去し、対象となる敵の発光と診断用カウンターを初期化する。
/// </summary>
void GamePresentation::ClearHitReactions()
{
	for (HitReaction& reaction : m_HitReactions)
	{
		if (reaction.active && reaction.targetKey != 0)
		{
			EnemyBall* target = FindLiveHitTarget(reaction.targetKey);
			if (target != nullptr)
				target->SetPresentationHitFlash(0.0f);
		}
		reaction = {};
	}
	m_CombatReactionDiagnostics.activeHitFlashes = 0;
}

/// <summary>
/// ダメージと撃破状態を確認し、対象の敵に短時間の被弾発光を登録する。
/// </summary>
void GamePresentation::BeginHitReaction(
	std::uintptr_t targetKey,
	int damage,
	bool defeated)
{
	++m_CombatReactionDiagnostics.hitFlashRequests;
	if (damage <= 0 && !defeated)
	{
		++m_CombatReactionDiagnostics.zeroDamageFlashesSuppressed;
		return;
	}
	Game* game = Game::GetInstance();
	EnemyBall* target = FindLiveHitTarget(targetKey);
	if (game == nullptr || !game->IsEnemyHitFlashEnabled() ||
		target == nullptr)
	{
		++m_CombatReactionDiagnostics.hitFlashesSkipped;
		return;
	}

	CombatPresentationRules::HitFlash flash =
		CombatPresentationRules::FlashForDamage((std::max)(1, damage));
	if (defeated)
	{
		flash.duration = (std::max)(flash.duration, 0.18f);
		flash.intensity = 1.0f;
	}

	HitReaction* slot = nullptr;
	for (HitReaction& reaction : m_HitReactions)
	{
		if (reaction.active && reaction.targetKey == targetKey)
		{
			slot = &reaction;
			break;
		}
		if (slot == nullptr && !reaction.active) slot = &reaction;
	}
	if (slot == nullptr)
	{
		slot = &*std::min_element(
			m_HitReactions.begin(),
			m_HitReactions.end(),
			[](const HitReaction& left, const HitReaction& right)
			{
				return left.lifetime < right.lifetime;
			});
		EnemyBall* replaced = FindLiveHitTarget(slot->targetKey);
		if (replaced != nullptr)
			replaced->SetPresentationHitFlash(0.0f);
	}

	*slot = HitReaction{
		targetKey,
		flash.duration,
		flash.duration,
		flash.intensity,
		true,
	};
	target->SetPresentationHitFlash(flash.intensity);
	++m_CombatReactionDiagnostics.hitFlashesTriggered;
	const int active = static_cast<int>(std::count_if(
		m_HitReactions.begin(),
		m_HitReactions.end(),
		[](const HitReaction& reaction) { return reaction.active; }));
	m_CombatReactionDiagnostics.activeHitFlashes = active;
	m_CombatReactionDiagnostics.maximumActiveHitFlashes = (std::max)(
		m_CombatReactionDiagnostics.maximumActiveHitFlashes,
		active);
}

/// <summary>
/// 被弾発光の残り時間と強度を更新し、無効な対象や期限切れの演出を消去する。
/// </summary>
void GamePresentation::UpdateHitReactions(Game& game, float deltaSeconds)
{
	const bool enabled = game.IsEnemyHitFlashEnabled() && IsBattleScene(game);
	int active = 0;
	for (HitReaction& reaction : m_HitReactions)
	{
		if (!reaction.active) continue;
		EnemyBall* target = FindLiveHitTarget(reaction.targetKey);
		if (!enabled || target == nullptr)
		{
			if (target != nullptr)
				target->SetPresentationHitFlash(0.0f);
			reaction = {};
			continue;
		}
		reaction.lifetime -= deltaSeconds;
		if (reaction.lifetime <= 0.0f)
		{
			target->SetPresentationHitFlash(0.0f);
			reaction = {};
			continue;
		}
		const float life = std::clamp(
			reaction.lifetime / reaction.totalLifetime,
			0.0f,
			1.0f);
		target->SetPresentationHitFlash(
			reaction.intensity * life * life);
		++active;
	}
	m_CombatReactionDiagnostics.activeHitFlashes = active;
}

/// <summary>
/// 生存中の敵のHP表示を更新し、盤面から消えた対象の表示状態を除く。
/// </summary>
void GamePresentation::UpdateEnemyHpVisuals(float deltaSeconds)
{
	std::vector<std::uintptr_t> liveKeys;
	for (const EnemyBall* enemy :
		GameRuntime::World().GetComponents<EnemyBall>())
	{
		if (enemy == nullptr || enemy->IsDefeated() || enemy->IsPocketed())
		{
			continue;
		}
		const std::uintptr_t key = reinterpret_cast<std::uintptr_t>(enemy);
		liveKeys.push_back(key);
		auto visual = std::find_if(
			m_EnemyHpVisuals.begin(),
			m_EnemyHpVisuals.end(),
			[key](const EnemyHpVisual& value)
			{
				return value.targetKey == key;
			});
		const int hp = (std::max)(0, enemy->GetHP());
		const int maximumHp = (std::max)(1, enemy->GetMaxHP());
		if (visual == m_EnemyHpVisuals.end())
		{
			m_EnemyHpVisuals.push_back({
				key,
				hp,
				maximumHp,
				static_cast<float>(hp),
				0.0f,
				});
			continue;
		}
		if (hp < visual->currentHp)
		{
			visual->trailingHp = (std::max)(
				visual->trailingHp,
				static_cast<float>(visual->currentHp));
			visual->damageHold = 0.28f;
		}
		else if (hp > visual->currentHp)
		{
			visual->trailingHp = static_cast<float>(hp);
		}
		visual->currentHp = hp;
		visual->maximumHp = maximumHp;
		visual->damageHold = (std::max)(
			0.0f,
			visual->damageHold - deltaSeconds);
		if (visual->damageHold <= 0.0f)
		{
			const float approach = (std::max)(
				18.0f,
				static_cast<float>(maximumHp) * 2.5f) * deltaSeconds;
			visual->trailingHp = (std::max)(
				static_cast<float>(hp),
				visual->trailingHp - approach);
		}
	}
	std::erase_if(
		m_EnemyHpVisuals,
		[&liveKeys](const EnemyHpVisual& visual)
		{
			return std::find(liveKeys.begin(), liveKeys.end(), visual.targetKey) ==
				liveKeys.end();
		});
}

/// <summary>
/// 指定された敵の撃破をすでに追跡しているかを返す。
/// </summary>
bool GamePresentation::HasTrackedDefeat(std::uintptr_t targetKey) const
{
	if (targetKey == 0) return false;
	return std::find(
		m_DefeatKeys.begin(),
		m_DefeatKeys.begin() + m_TrackedDefeatCount,
		targetKey) != m_DefeatKeys.begin() + m_TrackedDefeatCount;
}

/// <summary>
/// 初回の撃破を記録し、個別の撃破演出や複数撃破のバナーを開始する。
/// </summary>
bool GamePresentation::BeginDefeatReaction(
	const Vector3& worldPosition,
	std::uintptr_t targetKey)
{
	++m_CombatReactionDiagnostics.defeatRequests;
	if (HasTrackedDefeat(targetKey))
	{
		++m_CombatReactionDiagnostics.duplicateDefeatsSuppressed;
		return false;
	}
	if (targetKey != 0 &&
		m_TrackedDefeatCount < static_cast<int>(m_DefeatKeys.size()))
	{
		m_DefeatKeys[m_TrackedDefeatCount++] = targetKey;
	}
	++m_CurrentShotDefeatCount;
	++m_CombatReactionDiagnostics.uniqueDefeats;

	Game* game = Game::GetInstance();
	if (game != nullptr && game->IsDefeatVfxEnabled())
	{
		CollisionVfxSystem::SpawnRequest request;
		request.worldPosition = worldPosition;
		request.collisionAxis = Vector3::UnitX;
		request.strength = CollisionPresentationRules::Strength::Strong;
		request.chainTier = CollisionPresentationRules::ChainTier(
			m_HeavyPhysicsChainCount);
		request.priority = 4;
		request.density = game->GetDecorativeRingAmount();
		request.frame = m_PresentationFrame;
		request.defeated = true;
		m_CollisionVfx.Spawn(request);
	}

	if (game != nullptr && game->IsMultiDefeatEffectsEnabled() &&
		m_CurrentShotDefeatCount >= 2)
	{
		// Every additional defeat restarts the same readable pop/hold/fade cycle.
		m_MultiDefeatDisplayTotalLifetime = 2.17f;
		m_MultiDefeatDisplayLifetime = m_MultiDefeatDisplayTotalLifetime;
		++m_CombatReactionDiagnostics.multiDefeatDisplays;
	}
	return true;
}
