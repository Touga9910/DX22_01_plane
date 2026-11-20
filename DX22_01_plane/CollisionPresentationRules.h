#pragma once

#include <algorithm>
#include <cmath>

// Pure presentation tuning for collision feedback. These values never affect
// physics, damage, resources, or accepted-contact telemetry.
class CollisionPresentationRules final
{
public:
	enum class Strength
	{
		Weak,
		Medium,
		Strong,
	};

	static constexpr float MediumImpactSpeed = 2.0f;
	static constexpr float StrongImpactSpeed = 6.0f;
	static constexpr float MaximumPresentationSpeed = 16.0f;
	static constexpr int ChainMilestones[4] = { 3, 6, 10, 15 };

	static Strength Classify(float approachSpeed)
	{
		if (!std::isfinite(approachSpeed))
		{
			return Strength::Weak;
		}
		const float speed = std::clamp(
			approachSpeed,
			0.0f,
			MaximumPresentationSpeed);
		if (speed >= StrongImpactSpeed) return Strength::Strong;
		if (speed >= MediumImpactSpeed) return Strength::Medium;
		return Strength::Weak;
	}

    struct Shake { float lifetime=0, strength=0; };
    struct Offset { float x=0,z=0; };
    static Shake ImpactShake(Strength strength,bool secondary)
    {
        if(strength==Strength::Weak)return {};
        return {strength==Strength::Strong?.16f:.10f,
            strength==Strength::Strong?(secondary?.72f:.88f):(secondary?.30f:.42f)};
    }
    static Offset ShakeOffset(float phase,float lifetime,float strength)
    {
        const float fade=std::clamp(lifetime/.24f,0.0f,1.0f);
        return {std::sin(phase)*strength*fade,std::cos(phase*1.31f)*strength*fade};
    }
	static int ChainTier(int chainCount)
	{
		if (chainCount >= 15) return 4;
		if (chainCount >= 10) return 3;
		if (chainCount >= 6) return 2;
		if (chainCount >= 3) return 1;
		return 0;
	}

	static bool IsChainMilestone(int chainCount)
	{
		for (const int milestone : ChainMilestones)
		{
			if (chainCount == milestone) return true;
		}
		return false;
	}

	static int MaximumDecorations(int ringAmount)
	{
		switch (std::clamp(ringAmount, 0, 3))
		{
		case 0: return 0;
		case 1: return 4;
		case 3: return 12;
		default: return 8;
		}
	}

	static int MaximumSparks(int density)
	{
		switch (std::clamp(density, 0, 3))
		{
		case 0: return 0;
		case 1: return 32;
		case 3: return 96;
		default: return 64;
		}
	}

	static int MaximumSparkGenerationPerFrame(int density)
	{
		switch (std::clamp(density, 0, 3))
		{
		case 0: return 0;
		case 1: return 16;
		case 3: return 48;
		default: return 32;
		}
	}

	CollisionPresentationRules() = delete;
};
