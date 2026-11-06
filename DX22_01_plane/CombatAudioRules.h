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
		{ 0.025f, 0.72f, 0.24f, 0.90f, 1.04f, 1.00f, 0.96f, 4, 60 },
		{ 0.040f, 0.54f, 0.18f, 0.72f, 1.05f, 1.01f, 0.97f, 3, 45 },
		{ 0.060f, 0.92f, 0.72f, 1.00f, 1.02f, 1.00f, 0.98f, 3, 90 },
		{ 0.080f, 0.96f, 0.78f, 1.00f, 1.04f, 1.00f, 0.96f, 4, 100 },
		{ 0.045f, 0.45f, 0.16f, 0.62f, 1.05f, 1.01f, 0.98f, 2, 25 },
	}};

	static constexpr std::size_t Index(CombatSeCategory category)
	{
		return static_cast<std::size_t>(category);
	}

	static const CombatSeTuning& Get(CombatSeCategory category)
	{
		const std::size_t index = Index(category);
		return Tuning[index < Tuning.size() ? index : 0];
	}

	static float SanitizeSpeed(float speed)
	{
		return std::isfinite(speed)
			? std::clamp(speed, 0.0f,
				CollisionPresentationRules::MaximumPresentationSpeed)
			: 0.0f;
	}

	static float StrengthRatio(float speed)
	{
		return SanitizeSpeed(speed) /
			CollisionPresentationRules::MaximumPresentationSpeed;
	}

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

	static const char* CategoryId(CombatSeCategory category)
	{
		switch (category)
		{
		case CombatSeCategory::DirectImpact: return "direct_impact";
		case CombatSeCategory::EnemyImpact: return "enemy_impact";
		case CombatSeCategory::EnemyDefeat: return "enemy_defeat";
		case CombatSeCategory::ChainMilestone: return "chain_milestone";
		case CombatSeCategory::WallImpact: return "wall_impact";
		default: return "unknown";
		}
	}

	CombatAudioRules() = delete;
};
