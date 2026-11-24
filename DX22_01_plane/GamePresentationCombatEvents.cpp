#include "GamePresentation.h"
#include "Game.h"
#include "GameEvent.h"
#include "CollisionPresentationRules.h"
#include "PlayerBallData.h"
#include "input.h"
#include "imgui/imgui.h"

#pragma execution_character_set("utf-8")

#include <algorithm>
#include <cstdio>
#include <utility>

using DirectX::SimpleMath::Vector3;

/// <summary>
/// 戦闘開始時に前の戦闘演出を初期化し、必要に応じてチュートリアルを開始する。
/// </summary>
void GamePresentation::OnBattleStarted(Game&)
{
	m_PlayerAura.Reset();
    m_AttackFlightAge = -1.0f;
	ClearHitReactions();
	m_CollisionVfx.Reset();
	m_CombatReactionDiagnostics = {};
	m_DefeatKeys.fill(0);
	m_TrackedDefeatCount = 0;
	m_CurrentShotDefeatCount = 0;
	m_CurrentShotChargeInterruptions = 0;
	m_CurrentShotResourceGained.fill(0);
	m_CurrentShotResourceConsumed.fill(0);
	m_CurrentShotFinisherMask = 0;
	m_HitSummaryLifetime = 0.0f;
	m_EnemyHpVisuals.clear();
	m_MultiDefeatDisplayLifetime = 0.0f;
	m_CurrentShotBallCategory = static_cast<int>(BallCategory::Standard);
	m_HeavyPhysicsChainCount = 0;
	m_HeavyChainDisplayLifetime = 0.0f;
	m_HeavyChainMilestone = false;
	m_ImpactFlashCooldown = 0.0f;
	m_CameraShakeCooldown = 0.0f;
	m_DeckListOpen = false;
	m_RelicListOpen = false;
	if (!m_TutorialCompleted && (!m_TutorialActive || m_TutorialReplay))
	{
		StartTutorial(false);
	}
}

/// <summary>
/// 発射した球種を記録し、前回のショット演出を消してチュートリアルを更新する。
/// </summary>
void GamePresentation::OnShotFired(int ballCategory)
{
	ClearHitReactions();
	m_CurrentShotBallCategory = ballCategory;
	m_HeavyPhysicsChainCount = 0;
	m_HeavyChainDisplayLifetime = 0.0f;
	m_HeavyChainMilestone = false;
	m_CollisionVfx.Reset();
	m_CombatReactionDiagnostics = {};
	m_DefeatKeys.fill(0);
	m_TrackedDefeatCount = 0;
	m_CurrentShotDefeatCount = 0;
	m_MultiDefeatDisplayLifetime = 0.0f;
	m_ImpactFlashCooldown = 0.0f;
	m_CameraShakeCooldown = 0.0f;
	m_CurrentShotHitCount = 0;
	m_CurrentShotDamage = 0;
	m_CurrentShotChargeInterruptions = 0;
	m_CurrentShotResourceGained.fill(0);
	m_CurrentShotResourceConsumed.fill(0);
	m_CurrentShotFinisherMask = 0;
	m_HitSummaryLifetime = 0.0f;
	if (m_TutorialActive && m_TutorialStep == TutorialStep::PracticeShot)
	{
		m_TutorialStep = TutorialStep::WatchShot;
	}
}

/// <summary>
/// 戦闘のダメージと撃破を画面表示、被弾演出、振動に反映する。
/// </summary>
void GamePresentation::OnCombatFeedback(
	const Vector3& worldPosition,
	int damage,
	bool defeated,
	bool enemyEnemyCollision,
	std::uintptr_t targetKey,
	const std::string& targetEnemyId)
{
	(void)targetEnemyId;
	BeginHitReaction(targetKey, damage, defeated);
	if (defeated && !BeginDefeatReaction(worldPosition, targetKey))
	{
		return;
	}
	FloatingFeedback feedback;
	feedback.worldPosition = worldPosition;
	feedback.totalLifetime = defeated
		? 1.35f
		: CombatPresentationRules::DamageFeedbackLifetimeSeconds;
	feedback.lifetime = feedback.totalLifetime;
	feedback.fontScale = defeated
		? m_CurrentShotDefeatCount >= 3 ? 1.38f
		: m_CurrentShotDefeatCount == 2 ? 1.28f : 1.20f
		: 0.86f;
	const bool enhancedImpact =
		Game::GetInstance()->IsCollisionImpactEffectsEnabled();
	feedback.drawRing = !enhancedImpact;
	feedback.enemyEnemyCollision = enemyEnemyCollision;
	feedback.createdFrame = m_PresentationFrame;
	feedback.createdTimeSeconds = m_PresentationTimeSeconds;
	feedback.targetKey = targetKey;
	feedback.followsDamageTarget = !defeated && damage > 0 && targetKey != 0;
	feedback.targetFollowLifetime = feedback.followsDamageTarget
		? CombatPresentationRules::DamageTargetFollowSeconds
		: 0.0f;
	feedback.color = defeated
		? IM_COL32(255, 205, 70, 255)
		: enemyEnemyCollision
		? IM_COL32(110, 220, 255, 255)
		: IM_COL32(255, 118, 80, 255);
	if (defeated)
	{
		feedback.text = m_CurrentShotDefeatCount >= 3
			? "連続撃破！  -" + std::to_string(damage)
			: "撃破！  -" + std::to_string(damage);
	}
	else if (damage > 0)
	{
		feedback.text = "-" + std::to_string(damage) + " HP";
	}
	else
	{
		feedback.text = "防御";
	}
	bool merged = false;
	if (!defeated && damage > 0)
	{
		for (auto iterator = m_Feedback.rbegin();
			iterator != m_Feedback.rend();
			++iterator)
		{
			FloatingFeedback& existing = *iterator;
			const float ageSeconds = (std::max)(
				0.0f,
				m_PresentationTimeSeconds - existing.createdTimeSeconds);
			const Vector3 separation =
				existing.worldPosition - worldPosition;
			int existingDamage = 0;
			if (!existing.playerHudMessage &&
				existing.enemyEnemyCollision == enemyEnemyCollision &&
				CombatPresentationRules::CanMergeDamage(
					existing.targetKey,
					targetKey,
					ageSeconds,
					separation.LengthSquared(),
					defeated) &&
				sscanf_s(existing.text.c_str(), "-%d HP", &existingDamage) == 1)
			{
				existing.text = "-" +
					std::to_string(existingDamage + damage) + " HP";
				existing.worldPosition = worldPosition;
				existing.createdFrame = m_PresentationFrame;
				existing.createdTimeSeconds = m_PresentationTimeSeconds;
				existing.followsDamageTarget = feedback.followsDamageTarget;
				existing.targetFollowLifetime = feedback.targetFollowLifetime;
				existing.totalLifetime = (std::max)(
					existing.totalLifetime,
					feedback.totalLifetime);
				existing.lifetime = existing.totalLifetime;
				existing.fontScale = (std::max)(existing.fontScale, 1.08f);
				merged = true;
				break;
			}
		}
	}
	if (!merged)
	{
		int visibleDamageNumbers = 0;
		for (const FloatingFeedback& existing : m_Feedback)
		{
			int visibleDamage = 0;
			if (sscanf_s(
				existing.text.c_str(),
				"-%d HP",
				&visibleDamage) == 1)
			{
				++visibleDamageNumbers;
			}
		}
		if (defeated || damage <= 0 ||
			visibleDamageNumbers <
			CombatPresentationRules::MaximumFloatingDamageNumbers)
		{
			m_Feedback.push_back(std::move(feedback));
		}
	}
	if (!enhancedImpact || defeated)
	{
		m_ImpactFlashLifetime = (std::max)(m_ImpactFlashLifetime, 0.11f);
	}
	m_CurrentShotHitCount++;
	m_CurrentShotDamage += (std::max)(0, damage);
	if (Game::GetInstance()->IsCameraShakeEnabled() &&
		(!enhancedImpact || defeated))
	{
		m_CameraShakeLifetime = (std::max)(
			m_CameraShakeLifetime,
			defeated ? 0.24f : 0.12f);
		m_CameraShakeStrength = (std::max)(
			m_CameraShakeStrength,
			defeated ? 1.25f : 0.55f);
	}
	if (Game::GetInstance()->IsVibrationEnabled())
	{
		Input::SetVibration(defeated ? 7 : 3, defeated ? 0.78f : 0.36f);
	}
}

/// <summary>
/// 衝突時の相対速度や連鎖段階をもとに、衝突エフェクトと画面揺れを設定する。
/// </summary>
void GamePresentation::OnCollisionImpact(const CollisionImpactEvent& event)
{
	const CollisionPresentationRules::Strength strength =
		CollisionPresentationRules::Classify(event.relativeApproachSpeed);
	const bool secondary =
		event.collisionKind == CombatCollisionKind::EnemyEnemy;
	const bool heavyShot = event.shotBallCategory ==
		static_cast<int>(BallCategory::Heavy);

	const float feedbackScale = strength == CollisionPresentationRules::Strength::Strong
		? 1.10f
		: strength == CollisionPresentationRules::Strength::Medium
		? 0.98f
		: 0.86f;
	for (FloatingFeedback& feedback : m_Feedback)
	{
		if (feedback.createdFrame != m_PresentationFrame ||
			feedback.enemyEnemyCollision != secondary)
		{
			continue;
		}
		const Vector3 firstDelta =
			feedback.worldPosition - event.firstTargetPosition;
		const Vector3 secondDelta =
			feedback.worldPosition - event.secondTargetPosition;
		if (firstDelta.LengthSquared() <= 0.25f ||
			(secondary && secondDelta.LengthSquared() <= 0.25f))
		{
			feedback.fontScale = (std::max)(feedback.fontScale, feedbackScale);
		}
	}

	bool milestone = false;
	int chainTier = 0;
	if (secondary && heavyShot)
	{
		++m_HeavyPhysicsChainCount;
		chainTier = CollisionPresentationRules::ChainTier(
			m_HeavyPhysicsChainCount);
		milestone = CollisionPresentationRules::IsChainMilestone(
			m_HeavyPhysicsChainCount);
		if (milestone)
		{
			m_HeavyChainDisplayTotalLifetime = chainTier >= 3 ? 1.55f : 1.25f;
			m_HeavyChainDisplayLifetime = m_HeavyChainDisplayTotalLifetime;
			m_HeavyChainMilestone = true;
		}
		else
		{
			m_HeavyChainDisplayTotalLifetime = (std::max)(
				m_HeavyChainDisplayTotalLifetime,
				0.95f);
			m_HeavyChainDisplayLifetime = (std::max)(
				m_HeavyChainDisplayLifetime,
				0.95f);
		}
	}

	Game* game = Game::GetInstance();
	const bool showMilestone =
		milestone && game->IsHeavyChainEffectsEnabled();
	if (showMilestone)
	{
		if (game->IsScreenFlashEnabled() && m_ImpactFlashCooldown <= 0.0f)
		{
			m_ImpactFlashLifetime = (std::max)(m_ImpactFlashLifetime, 0.11f);
			m_ImpactFlashCooldown = 0.16f;
		}
		if (game->IsCameraShakeEnabled() && m_CameraShakeCooldown <= 0.0f)
		{
			const float milestoneStrength = m_HeavyPhysicsChainCount >= 15
				? 1.35f
				: m_HeavyPhysicsChainCount >= 10
				? 1.05f
				: m_HeavyPhysicsChainCount >= 6 ? 0.82f : 0.62f;
			m_CameraShakeLifetime = (std::max)(m_CameraShakeLifetime, 0.18f);
			m_CameraShakeStrength = (std::max)(
				m_CameraShakeStrength,
				milestoneStrength);
			m_CameraShakeCooldown = 0.12f;
		}
	}

	const bool defeatAlreadyPresented = event.defeated &&
		(HasTrackedDefeat(event.firstTargetKey) ||
			HasTrackedDefeat(event.secondTargetKey));
	if (defeatAlreadyPresented || !game->IsCollisionImpactEffectsEnabled())
	{
		return;
	}

	CollisionVfxSystem::SpawnRequest vfx;
	vfx.worldPosition = event.worldPosition;
	vfx.collisionAxis = event.firstTargetPosition - event.secondTargetPosition;
	vfx.strength = strength;
	vfx.chainTier = chainTier;
	vfx.priority = event.defeated || showMilestone
		? 3
		: strength == CollisionPresentationRules::Strength::Strong
		? 2
		: strength == CollisionPresentationRules::Strength::Medium ? 1 : 0;
	vfx.density = game->GetDecorativeRingAmount();
	vfx.frame = m_PresentationFrame;
	vfx.secondaryCollision = secondary;
	vfx.milestone = showMilestone;
	vfx.defeated = event.defeated;
	m_CollisionVfx.Spawn(vfx);

	if (strength == CollisionPresentationRules::Strength::Strong &&
		game->IsScreenFlashEnabled() && m_ImpactFlashCooldown <= 0.0f)
	{
		m_ImpactFlashLifetime = (std::max)(m_ImpactFlashLifetime, 0.08f);
		m_ImpactFlashCooldown = 0.12f;
	}
	if (strength != CollisionPresentationRules::Strength::Weak &&
		game->IsCameraShakeEnabled() && m_CameraShakeCooldown <= 0.0f)
	{
        const auto shake=CollisionPresentationRules::ImpactShake(strength,secondary);
        const float requestedStrength=shake.strength;
        m_CameraShakeLifetime=(std::max)(m_CameraShakeLifetime,shake.lifetime);
		m_CameraShakeStrength = std::clamp(
			(std::max)(m_CameraShakeStrength, requestedStrength),
			0.0f,
			1.5f);
		m_CameraShakeCooldown = 0.08f;
	}
}

/// <summary>
/// 連鎖が届いた範囲と命中数を、一定時間表示する演出に追加する。
/// </summary>
void GamePresentation::OnChainImpact(
	const Vector3& worldPosition,
	float radius,
	int hitCount)
{
	if (radius <= 0.0f)
	{
		return;
	}
	ChainRangeFeedback feedback;
	feedback.worldPosition = worldPosition;
	feedback.radius = radius;
	feedback.hitCount = (std::max)(0, hitCount);
	feedback.totalLifetime = 1.05f;
	feedback.lifetime = feedback.totalLifetime;
	m_ChainRanges.push_back(feedback);
}

/// <summary>
/// ポケットに落ちた対象とダメージや撃破の結果に応じて、画面表示と振動を設定する。
/// </summary>
void GamePresentation::OnPocketFeedback(
	const Vector3& worldPosition,
	bool playerPocket,
	bool finisher,
	int damage,
	std::uintptr_t targetKey,
	const std::string& targetEnemyId)
{
	(void)targetEnemyId;
	if (!playerPocket && finisher)
	{
		BeginHitReaction(targetKey, damage, true);
		BeginDefeatReaction(worldPosition, targetKey);
	}
	FloatingFeedback feedback;
	feedback.worldPosition = worldPosition;
	feedback.totalLifetime = 1.5f;
	feedback.lifetime = feedback.totalLifetime;
	feedback.drawRing = true;
	if (playerPocket)
	{
		feedback.text = "スクラッチ  -" + std::to_string(damage) + " HP";
		feedback.color = IM_COL32(255, 88, 96, 255);
		m_DamageFlashLifetime = 0.28f;
	}
	else if (finisher)
	{
		feedback.text = "ポケット撃破！";
		feedback.color = IM_COL32(255, 215, 64, 255);
	}
	else
	{
		feedback.text = "ポケット制圧";
		feedback.color = IM_COL32(92, 224, 255, 255);
	}
	m_Feedback.push_back(std::move(feedback));
	if (Game::GetInstance()->IsCameraShakeEnabled())
	{
		m_CameraShakeLifetime = (std::max)(m_CameraShakeLifetime, 0.24f);
		m_CameraShakeStrength = finisher ? 1.45f : 0.85f;
	}
	if (Game::GetInstance()->IsVibrationEnabled())
	{
		Input::SetVibration(finisher ? 9 : 5, finisher ? 0.9f : 0.55f);
	}
}

/// <summary>
/// 敵の攻撃の出発点と目標地点を記録し、飛翔演出を開始する。
/// </summary>
void GamePresentation::OnEnemyAttackLaunched(const Vector3& start, const Vector3& target)
{
    m_AttackStart = start;
    m_AttackTarget = target;
    m_AttackFlightAge = 0.0f;
}

/// <summary>
/// 敵の攻撃の飛翔表示を終え、シールドで受け止めた結果を自球のHUDに反映する。
/// </summary>
void GamePresentation::OnEnemyAttackImpact(int hpDamage, int shieldAbsorbed)
{
    m_AttackFlightAge = -1.0f;
    m_LastAttackShieldAbsorbed = hpDamage > 0 ? shieldAbsorbed : 0;
    std::erase_if(m_Feedback, [](const FloatingFeedback& f) { return f.playerHudMessage; });
    if (hpDamage > 0 || shieldAbsorbed <= 0) return;
    m_DamageFlashLifetime = 0.0f;
    // Full block: no red flash. HP damage keeps the existing red-hit reaction.
    if (Game::GetInstance()->IsCameraShakeEnabled())
    {
        m_CameraShakeLifetime = 0.16f;
        m_CameraShakeStrength = 0.7f;
    }
    FloatingFeedback feedback;
    feedback.text = "BLOCK  " + std::to_string(shieldAbsorbed);
    feedback.playerHudMessage = true;
    feedback.totalLifetime = feedback.lifetime = 0.65f;
    feedback.color = IM_COL32(100, 205, 255, 255);
    m_Feedback.push_back(std::move(feedback));
}

/// <summary>
/// 自球が受けたダメージに応じて、画面の明滅、表示、振動を設定する。
/// </summary>
void GamePresentation::OnPlayerDamage(
	int damage,
	const std::string& source)
{
	if (damage <= 0)
	{
		return;
	}
	m_DamageFlashLifetime = (std::max)(m_DamageFlashLifetime, 0.22f);
	if (source != "pocket")
	{
		FloatingFeedback feedback;
		feedback.worldPosition = Vector3::Zero;
		feedback.text = "プレイヤー  -" + std::to_string(damage) + " HP";
        if (m_LastAttackShieldAbsorbed > 0)
            feedback.text += " / BLOCK " + std::to_string(m_LastAttackShieldAbsorbed);
        m_LastAttackShieldAbsorbed = 0;
		feedback.playerHudMessage = true;
		feedback.totalLifetime = 1.1f;
		feedback.lifetime = feedback.totalLifetime;
		feedback.color = IM_COL32(255, 86, 92, 255);
		m_Feedback.push_back(std::move(feedback));
	}
	if (Game::GetInstance()->IsCameraShakeEnabled())
	{
		m_CameraShakeLifetime = (std::max)(m_CameraShakeLifetime, 0.20f);
		m_CameraShakeStrength = 1.0f;
	}
	if (Game::GetInstance()->IsVibrationEnabled())
	{
		Input::SetVibration(5, 0.62f);
	}
}

/// <summary>
/// 敵の溜め行動を中断した理由に応じて、戦闘中の表示を追加する。
/// </summary>
void GamePresentation::OnChargeInterrupted(
	const Vector3& worldPosition,
	ChargeInterruptReason reason)
{
	FloatingFeedback feedback;
	feedback.worldPosition = worldPosition;
	feedback.totalLifetime = 1.45f;
	feedback.lifetime = feedback.totalLifetime;
	feedback.drawRing = true;
	feedback.color = IM_COL32(205, 92, 255, 255);
	switch (reason)
	{
	case ChargeInterruptReason::Movement:
		feedback.text = "CHARGE BREAK  重量";
		break;
	case ChargeInterruptReason::PierceDamage:
		feedback.text = "CHARGE BREAK  貫通";
		break;
	case ChargeInterruptReason::Pocketed:
		feedback.text = "CHARGE BREAK  ポケット";
		break;
	case ChargeInterruptReason::Defeated:
		feedback.text = "CHARGE BREAK  撃破";
		break;
	default:
		return;
	}
	m_Feedback.push_back(std::move(feedback));
}

/// <summary>
/// 敵の能力状態が変わったとき、対応する説明やリングの表示を追加する。
/// </summary>
void GamePresentation::OnEnemyAbilityStateChanged(
	const EnemyAbilityStateChangedEvent& event)
{
	FloatingFeedback feedback;
	feedback.worldPosition = event.worldPosition;
	feedback.totalLifetime = 1.35f;
	feedback.lifetime = feedback.totalLifetime;
	feedback.drawRing = true;
	feedback.color = IM_COL32(92, 222, 246, 255);
	if (event.abilityId == "swarm_amplifier")
	{
		feedback.text = event.currentAttack < event.previousAttack
			? "SWARM DOWN  ATK -" +
			std::to_string(event.previousAttack - event.currentAttack)
			: "SWARM UP  ATK +" +
			std::to_string(event.currentAttack - event.previousAttack);
		feedback.color = IM_COL32(255, 92, 158, 255);
	}
	else if (event.abilityId == "impact_brittle")
	{
		feedback.text = event.currentStage > 0
			? "WALL BREAK  ATK " + std::to_string(event.currentAttack)
			: "BRITTLE END";
		CollisionVfxSystem::SpawnRequest request;
		request.worldPosition = event.worldPosition;
		request.strength = CollisionPresentationRules::Strength::Strong;
		request.priority = 2;
		request.density = 8;
		request.frame = m_PresentationFrame;
		request.milestone = true;
		request.enhanced = true;
		m_CollisionVfx.Spawn(request);
	}
	else if (event.abilityId == "protection_resonance")
	{
		feedback.text = event.currentStage > 0
			? "PROTECTED"
			: "PROTECTION LOST";
	}
	else
	{
		return;
	}
	m_Feedback.push_back(std::move(feedback));
}
