#pragma once

#include "CollisionPresentationRules.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

enum class CombatSeCategory
{
	DirectImpact,
	EnemyImpact,
	EnemyDefeat,
	ChainMilestone,
	WallImpact,
    PlayerHurt,
    ShieldBlock,
	Count,
};

enum class CombatSeSkipReason
{
	None,
	Muted,
	DeviceUnavailable,
	MissingAsset,
	Cooldown,
	CategoryLimit,
	VoiceLimit,
	BackendError,
};

struct CombatSeTuning final
{
	float cooldownSeconds = 0.0f;
	float baseVolume = 1.0f;
	float minimumVolume = 0.0f;
	float maximumVolume = 1.0f;
	float weakPitch = 1.0f;
	float mediumPitch = 1.0f;
	float strongPitch = 1.0f;
	int maximumVoices = 1;
	int priority = 0;
};

class CombatAudioRules final
{
public:
	static constexpr int MaximumVoices = 16;
	static constexpr float MinimumPitch = 0.92f;
	static constexpr float MaximumPitch = 1.08f;

	static constexpr std::array<CombatSeTuning,
		static_cast<std::size_t>(CombatSeCategory::Count)> Tuning = {{
		// cooldown, base, min, max, weak pitch, medium pitch, strong pitch,
		// category voices, priority
		// The same recorded ball material stays recognizable across collision strengths.
		{ 0.025f, 0.72f, 0.24f, 0.90f, 1.01f, 1.00f, 0.99f, 4, 60 },
		{ 0.040f, 0.54f, 0.18f, 0.72f, 1.01f, 1.00f, 0.99f, 3, 45 },
		{ 0.060f, 0.92f, 0.72f, 1.00f, 1.02f, 1.00f, 0.98f, 3, 90 },
		{ 0.080f, 0.96f, 0.78f, 1.00f, 1.04f, 1.00f, 0.96f, 4, 100 },
		{ 0.045f, 0.45f, 0.16f, 0.62f, 1.05f, 1.01f, 0.98f, 2, 25 },
        { 0.04f, 0.85f, 0.6f, 1.0f, 1, 1, 1, 2, 100 },
        { 0.04f, 0.72f, 0.5f, 0.9f, 1, 1, 1, 2, 100 },
	}};

	/// <summary>
	/// 戦闘効果音の分類を、調整値テーブルの添字に変換する。
	/// </summary>
	static constexpr std::size_t Index(CombatSeCategory category)
	{
		return static_cast<std::size_t>(category);
	}

	/// <summary>
	/// 指定分類の戦闘効果音設定を返し、対応する添字がなければ先頭の設定を使う。
	/// </summary>
	static const CombatSeTuning& Get(CombatSeCategory category)
	{
		const std::size_t index = Index(category);
		return Tuning[index < Tuning.size() ? index : 0];
	}

	/// <summary>
	/// 衝突速度を効果音演出に使える範囲に収め、有限でない値は0として扱う。
	/// </summary>
	static float SanitizeSpeed(float speed)
	{
		return std::isfinite(speed)
			? std::clamp(speed, 0.0f,
				CollisionPresentationRules::MaximumPresentationSpeed)
			: 0.0f;
	}

	/// <summary>
	/// 有効範囲に収めた衝突速度を、演出の最大速度に対する比率に変換する。
	/// </summary>
	static float StrengthRatio(float speed)
	{
		return SanitizeSpeed(speed) /
			CollisionPresentationRules::MaximumPresentationSpeed;
	}

	/// <summary>
	/// 効果音の分類、衝突の強さと速度、連鎖の節目に応じて再生音量を求める。
	/// </summary>
	static float Volume(
		CombatSeCategory category,
		CollisionPresentationRules::Strength strength,
		float speed,
		int chainMilestone = 0)
	{
		const CombatSeTuning& tuning = Get(category);
		float strengthScale = 1.0f;
		switch (strength)
		{
		case CollisionPresentationRules::Strength::Weak:
			strengthScale = 0.62f;
			break;
		case CollisionPresentationRules::Strength::Strong:
			strengthScale = 1.12f;
			break;
		default:
			break;
		}
		const float speedScale = 0.88f + StrengthRatio(speed) * 0.24f;
		const float milestoneScale = category == CombatSeCategory::ChainMilestone
			? 0.86f + 0.14f * static_cast<float>(
				CollisionPresentationRules::ChainTier(chainMilestone)) / 4.0f
			: 1.0f;
		const float value = tuning.baseVolume * strengthScale * speedScale *
			milestoneScale;
		return std::isfinite(value)
			? std::clamp(value, tuning.minimumVolume, tuning.maximumVolume)
			: tuning.minimumVolume;
	}

	/// <summary>
	/// 効果音の分類、衝突の強さ、連鎖の節目に応じて再生ピッチを選ぶ。
	/// </summary>
	static float Pitch(
		CombatSeCategory category,
		CollisionPresentationRules::Strength strength,
		int chainMilestone = 0)
	{
		const CombatSeTuning& tuning = Get(category);
		float pitch = tuning.mediumPitch;
		if (strength == CollisionPresentationRules::Strength::Weak)
			pitch = tuning.weakPitch;
		else if (strength == CollisionPresentationRules::Strength::Strong)
			pitch = tuning.strongPitch;
		if (category == CombatSeCategory::ChainMilestone)
		{
			pitch += 0.015f * static_cast<float>((std::max)(
				0, CollisionPresentationRules::ChainTier(chainMilestone) - 1));
		}
		return std::isfinite(pitch)
			? std::clamp(pitch, MinimumPitch, MaximumPitch)
			: 1.0f;
	}

	/// <summary>
	/// 分類ごとの基本優先度に衝突強度を反映し、効果音の再生優先度を求める。
	/// </summary>
	static int Priority(
		CombatSeCategory category,
		CollisionPresentationRules::Strength strength)
	{
		int priority = Get(category).priority;
		if (strength == CollisionPresentationRules::Strength::Strong)
			priority += 12;
		else if (strength == CollisionPresentationRules::Strength::Weak)
			priority -= 8;
		return priority;
	}

	/// <summary>
	/// 戦闘効果音の分類を、設定や診断で使う識別文字列に変換する。
	/// </summary>
	static const char* CategoryId(CombatSeCategory category)
	{
		switch (category)
		{
		case CombatSeCategory::DirectImpact: return "direct_impact";
		case CombatSeCategory::EnemyImpact: return "enemy_impact";
		case CombatSeCategory::EnemyDefeat: return "enemy_defeat";
		case CombatSeCategory::ChainMilestone: return "chain_milestone";
		case CombatSeCategory::WallImpact: return "wall_impact";
        case CombatSeCategory::PlayerHurt: return "player_hurt";
        case CombatSeCategory::ShieldBlock: return "shield_block";
		default: return "unknown";
		}
	}

	/// <summary>
	/// 効果音の共通規則は静的処理として提供するため、インスタンスの生成を禁止する。
	/// </summary>
	CombatAudioRules() = delete;
};
